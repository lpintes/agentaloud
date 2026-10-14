# Tvar thread/list pre zoznam rozhovorov projektu (claude-gui-lkk.7.11.1).
#
#   probe_codex_list.py survey      # vsetky vlakna: polia, zdroje, cwd
#   probe_codex_list.py cwd <dir>   # varianty filtra cwd (pismena, lomky)
#   probe_codex_list.py page <dir>  # strankovanie limit 2 cez nextCursor
#   probe_codex_list.py empty       # vlakno bez tahu: objavi sa v zozname?
#   probe_codex_list.py archive <id>  # archivuje, vypise zoznamy, vrati spat
#   probe_codex_list.py timing      # cena: spustenie + initialize + list
#
# Nahlady (preview) a mena sa do vystupu skracuju -- su to rozhovory
# pouzivatela, nie tvar protokolu.  Cely zaznam je v logu pod LOGDIR.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import collections
import json
import os
import sys
import time

from probe_codex_rpc import Client, LOGDIR


def short(s, n=40):
    if s is None:
        return None
    return s if len(s) <= n else s[:n] + "..."


def list_all(c, **params):
    """Vsetky stranky; vrati (vlakna, pocet stranok)."""
    out, cursor, pages = [], None, 0
    while True:
        p = dict(params)
        if cursor:
            p["cursor"] = cursor
        r = c.request("thread/list", p)
        if "error" in r:
            print("error:", r["error"])
            return out, pages
        pages += 1
        out += r["result"]["data"]
        cursor = r["result"].get("nextCursor")
        if not cursor:
            return out, pages


def source_name(src):
    if isinstance(src, dict):
        return next(iter(src))
    return src


def survey():
    c = Client("list-survey")
    c.initialize(experimental=True)
    r = c.request("thread/list", {"limit": 1, "sortKey": "updated_at"})
    res = r["result"]
    print("result keys:", sorted(res))
    if res["data"]:
        t = dict(res["data"][0])
        t["preview"] = short(t.get("preview"))
        t["name"] = short(t.get("name"))
        print("item:", json.dumps(t, ensure_ascii=False, indent=1))
    for label, extra in [
        ("default", {}),
        ("all sources", {"sourceKinds": [
            "cli", "vscode", "exec", "appServer", "subAgent",
            "subAgentReview", "subAgentCompact", "subAgentThreadSpawn",
            "subAgentOther", "unknown"]}),
        ("archived", {"archived": True}),
    ]:
        ts, pages = list_all(c, limit=50, sortKey="updated_at", **extra)
        print(f"\n== {label}: {len(ts)} vlakien, {pages} stranok")
        print(" source:", collections.Counter(source_name(t["source"])
                                              for t in ts))
        print(" parentThreadId set:",
              sum(1 for t in ts if t.get("parentThreadId")))
        print(" empty preview:", sum(1 for t in ts if not t.get("preview")))
        print(" name set:", sum(1 for t in ts if t.get("name")))
        print(" ephemeral:", sum(1 for t in ts if t.get("ephemeral")))
        print(" status:", collections.Counter(t["status"]["type"]
                                              for t in ts))
        print(" cwd:", collections.Counter(t["cwd"] for t in ts)
              .most_common(8))
        order = [t["updatedAt"] for t in ts]
        print(" updatedAt desc:", order == sorted(order, reverse=True))
    c.close()


def cwd_variants(d):
    c = Client("list-cwd")
    c.initialize(experimental=True)
    variants = [d, d.lower(), d.upper(), d.replace("\\", "/"),
                d.replace("/", "\\"), d.rstrip("\\/") + "\\",
                d.rstrip("\\/") + "/", [d, d.lower()]]
    for v in variants:
        ts, _ = list_all(c, cwd=v, limit=100)
        print(f"{json.dumps(v):70} -> {len(ts)}",
              sorted({t['cwd'] for t in ts}))
    c.close()


def page(d):
    c = Client("list-page")
    c.initialize(experimental=True)
    whole, _ = list_all(c, cwd=d, sortKey="updated_at", limit=100)
    paged, pages = list_all(c, cwd=d, sortKey="updated_at", limit=2)
    print("limit 100:", len(whole), " limit 2:", len(paged), "in", pages,
          "pages")
    print("same order:", [t["id"] for t in whole] == [t["id"] for t in paged])
    r = c.request("thread/list", {"cwd": d, "limit": 0})
    print("limit 0:", json.dumps(r)[:300])
    r = c.request("thread/list", {"cwd": d})
    print("bez limitu:", len(r["result"]["data"]), "nextCursor",
          bool(r["result"].get("nextCursor")))
    c.close()


def empty():
    d = "C:/b/codex-probe/list-empty"
    os.makedirs(d, exist_ok=True)
    c = Client("list-empty", cwd=d)
    c.initialize(experimental=True)
    r = c.request("thread/start", {"cwd": d})
    tid = r["result"]["thread"]["id"]
    print("started", tid)
    for sk in ("updated_at", "created_at"):
        ts, _ = list_all(c, cwd=d, sortKey=sk)
        print(f"same process, {sk}:", [t["id"] for t in ts])
    c.close()
    c = Client("list-empty2", cwd=d)
    c.initialize(experimental=True)
    ts, _ = list_all(c, cwd=d)
    print("new process:", [t["id"] for t in ts])
    c.close()


def archive(tid):
    c = Client("list-archive")
    c.initialize(experimental=True)
    r = c.request("thread/read", {"threadId": tid})
    d = r["result"]["thread"]["cwd"]
    print("cwd:", d)
    before, _ = list_all(c, cwd=d)
    print("pred:", tid in [t["id"] for t in before], len(before))
    print("archive:", c.request("thread/archive", {"threadId": tid}))
    after, _ = list_all(c, cwd=d)
    arch, _ = list_all(c, cwd=d, archived=True)
    print("po, default:", tid in [t["id"] for t in after], len(after))
    print("po, archived=true:", tid in [t["id"] for t in arch], len(arch))
    print("unarchive:", json.dumps(
        c.request("thread/unarchive", {"threadId": tid}))[:200])
    back, _ = list_all(c, cwd=d)
    print("vratene:", tid in [t["id"] for t in back])
    c.close()


def timing():
    for i in range(3):
        t0 = time.time()
        c = Client(f"list-timing{i}")
        r = c.initialize(experimental=True)
        t1 = time.time()
        c.request("thread/list", {"cwd": "C:/b/codex-probe/repo",
                                  "limit": 50, "sortKey": "updated_at"})
        t2 = time.time()
        c.close()
        t3 = time.time()
        print(f"init {t1 - t0:.3f}s  list {t2 - t1:.3f}s  "
              f"close {t3 - t2:.3f}s  spolu {t3 - t0:.3f}s")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "survey"
    arg = sys.argv[2] if len(sys.argv) > 2 else "C:/b/codex-probe/repo"
    {"survey": survey, "cwd": lambda: cwd_variants(arg),
     "page": lambda: page(arg), "empty": empty,
     "archive": lambda: archive(arg), "timing": timing}[cmd]()
    print("logy:", LOGDIR)


if __name__ == "__main__":
    main()
