# Are Codex item ids stable between the live stream and thread/resume history?
#
#   probe_anchor_codex.py live     one turn: prompt -> shell command -> steer
#                                  mid-command; records every item id
#   probe_anchor_codex.py resume   NEW app-server, thread/resume + turns/list,
#                                  compares ids/types/order with the live run
#   probe_anchor_codex.py all      both
#
# Question behind it: can an item id serve as a persistent bookmark anchor?
# Logs in C:/b/codex-probe/logs/anchor-*.log, notes in
# tools/probe_anchor_codex.notes.md.
#
# Exploration tool, not part of the product -- see CLAUDE.md.

import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_codex_rpc import Client, LOGDIR, text_input

MODEL = "gpt-6-luna"
WORKDIR = "C:/b/codex-probe/anchor-repo"
EXTRA = {"effort": "low", "summary": "detailed"}
PROMPT = ("Run the shell command `ping -n 6 127.0.0.1`, wait for it to "
          "finish, then say in one sentence how many packets came back.")
STEER = "Also end your reply with the word BANANA."
LIVE = f"{LOGDIR}/anchor.live.json"
THREAD = f"{LOGDIR}/anchor.thread"


def short(obj, n=160):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + "..."


def accept_all(client, msg):
    client.respond(msg["id"], {"decision": "accept"})


def describe(item):
    t = item.get("type")
    if t == "userMessage":
        return short(item.get("content"), 120) + f" clientId={item.get('clientId')}"
    if t == "agentMessage":
        return short(item.get("text"), 100) + f" phase={item.get('phase')}"
    if t == "commandExecution":
        return f"status={item.get('status')} cmd={short(item.get('command'), 60)}"
    return ""


def live():
    os.makedirs(WORKDIR, exist_ok=True)
    c = Client("anchor-live", on_request=accept_all, cwd=WORKDIR)
    c.initialize(experimental=True)
    r = c.request("thread/start", {"cwd": WORKDIR, "model": MODEL,
                                   "approvalPolicy": "untrusted",
                                   "sandbox": "read-only"})
    tid = r["result"]["thread"]["id"]
    print("thread", tid)
    with open(THREAD, "w") as f:
        f.write(tid)
    params = {"threadId": tid, "input": text_input(PROMPT),
              "clientUserMessageId": "probe-prompt-1"}
    params.update(EXTRA)
    r = c.request("turn/start", params)
    print("turn/start ->", short(r, 300))
    turn = r["result"]["turn"]["id"]
    if not c.wait_for(lambda m: m.get("method") == "item/started"
                      and m["params"]["item"]["type"] == "commandExecution",
                      120):
        print("command never started")
    time.sleep(1.5)
    rs = c.request("turn/steer", {"threadId": tid, "expectedTurnId": turn,
                                  "input": text_input(STEER),
                                  "clientUserMessageId": "probe-steer-1"})
    print("turn/steer ->", short(rs, 300))
    c.wait_method("turn/completed", 180)
    time.sleep(3.0)
    items = []
    for t, m in c.notes:
        method = m.get("method")
        if method in ("item/started", "item/completed"):
            p = m["params"]
            it = p["item"]
            items.append({"t": round(t, 2), "ev": method.split("/")[1],
                          "id": it.get("id"), "type": it.get("type"),
                          "turnId": p.get("turnId"), "desc": describe(it)})
        elif method in ("turn/started", "turn/completed"):
            tu = m["params"]["turn"]
            items.append({"t": round(t, 2), "ev": method, "id": tu.get("id"),
                          "status": tu.get("status")})
    for it in items:
        print(it)
    with open(LIVE, "w", encoding="utf-8") as f:
        json.dump({"thread": tid, "turn": turn, "items": items}, f,
                  ensure_ascii=False, indent=1)
    c.close()


def dump_turns(label, turns):
    out = []
    print(f"--- {label}: {len(turns)} turn(s)")
    for tu in turns:
        print(" turn", tu.get("id"), tu.get("status"), tu.get("itemsView"))
        for it in tu.get("items", []):
            print("   ", it.get("id"), it.get("type"), describe(it))
            out.append((tu.get("id"), it.get("id"), it.get("type")))
    return out


def compare(label, live_items, hist):
    order = []
    for it in live_items:
        if it["ev"] == "completed" and it["id"] not in [o[0] for o in order]:
            order.append((it["id"], it["type"]))
    hist_ids = [(h[1], h[2]) for h in hist]
    print(f"=== compare {label}")
    print(" live (completed order):", order)
    print(" hist:                  ", hist_ids)
    print(" missing in hist:", [o for o in order if o not in hist_ids])
    print(" extra in hist:  ", [h for h in hist_ids if h not in order])
    common = [o for o in order if o in hist_ids]
    print(" same order of common:", common == [h for h in hist_ids if h in order])


def resume():
    data = json.load(open(LIVE, encoding="utf-8"))
    tid = data["thread"]
    c = Client("anchor-resume", on_request=accept_all, cwd=WORKDIR)
    c.initialize(experimental=True)
    r = c.request("thread/resume", {"threadId": tid,
                                    "initialTurnsPage": {"itemsView": "full"}})
    res = r.get("result") or r
    th = res.get("thread", {})
    print("resume keys:", sorted(res.keys()))
    print("thread.id after resume:", th.get("id"), "same:", th.get("id") == tid)
    h1 = dump_turns("thread.turns", th.get("turns", []))
    page = res.get("initialTurnsPage") or {}
    h2 = dump_turns("initialTurnsPage(full)", page.get("data", page.get("turns", [])))
    r = c.request("thread/turns/list", {"threadId": tid, "itemsView": "full"})
    res3 = r.get("result") or r
    print("turns/list keys:", sorted(res3.keys()) if isinstance(res3, dict) else res3)
    h3 = dump_turns("turns/list(full)", res3.get("data", []) if isinstance(res3, dict) else [])
    for label, h in (("thread.turns", h1), ("initialTurnsPage", h2),
                     ("turns/list", h3)):
        if h:
            compare(label, data["items"], h)
    c.close()


def default_view():
    # thread/resume without initialTurnsPage: what does the default
    # ("summary") view keep?  Then one more turn with effort medium, so a
    # reasoning item exists, and a third process resumes again.
    data = json.load(open(LIVE, encoding="utf-8"))
    tid = data["thread"]
    c = Client("anchor-default", on_request=accept_all, cwd=WORKDIR)
    c.initialize(experimental=True)
    r = c.request("thread/resume", {"threadId": tid})
    th = (r.get("result") or r).get("thread", {})
    dump_turns("thread.turns (default view)", th.get("turns", []))
    start = len(c.notes)
    c.request("turn/start", {
        "threadId": tid, "effort": "medium", "summary": "detailed",
        "input": text_input("Think step by step: what is 17*23? Reply with "
                            "the number only.")})
    c.wait_method("turn/completed", 180, start)
    time.sleep(2.0)
    live2 = []
    for _, m in c.notes[start:]:
        if m.get("method") == "item/completed":
            it = m["params"]["item"]
            live2.append((it.get("id"), it.get("type")))
            print(" live2", it.get("id"), it.get("type"), describe(it))
    c.close()
    c = Client("anchor-resume2", on_request=accept_all, cwd=WORKDIR)
    c.initialize(experimental=True)
    r = c.request("thread/resume", {"threadId": tid,
                                    "initialTurnsPage": {"itemsView": "full"}})
    th = (r.get("result") or r).get("thread", {})
    h = dump_turns("thread.turns after 2nd turn", th.get("turns", []))
    hist = [(x[1], x[2]) for x in h]
    print(" missing in hist:", [o for o in live2 if o not in hist])
    c.close()


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "all"
    if mode in ("live", "all"):
        live()
    if mode in ("resume", "all"):
        resume()
    if mode == "default":
        default_view()


if __name__ == "__main__":
    main()
