# Obnovenie threadu v novom procese `codex app-server` a jeden dalsi tah v nom.
#
#   probe_codex_resume.py [rezim] [--turn]
#
# Vezme id threadu, ktore zapisal probe_codex_turn.py <rezim>, a posle
# thread/resume.  Odpoved ma niest predchadzajuce tahy ako ThreadItem -- teda
# v tvare streamu, nie v tvare rollout suboru na disku.  S --turn posle este
# jeden tah s effort medium a summary detailed, aby bolo vidiet, ci reasoning
# nesie text (pri effort low ziadny reasoning item neprisiel).
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import sys

from probe_codex_rpc import Client, LOGDIR, text_input


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "basic"
    turn = "--turn" in sys.argv
    tid = open(f"{LOGDIR}/{mode}.thread").read().strip()
    c = Client(f"resume-{mode}")
    c.initialize(experimental=True)
    r = c.request("thread/resume", {"threadId": tid})
    res = r.get("result") or r
    th = res.get("thread", {})
    print("resume keys:", sorted(res.keys()) if isinstance(res, dict) else res)
    print("thread.id same:", th.get("id") == tid, "status:", th.get("status"))
    for t in th.get("turns", []):
        print("turn", t["id"], t["status"], t.get("itemsView"))
        for it in t["items"]:
            print("   ", json.dumps(it, ensure_ascii=False)[:300])
    r = c.request("thread/turns/list", {"threadId": tid})
    print("turns/list:", json.dumps(r, ensure_ascii=False)[:1500])
    if turn:
        start = len(c.notes)
        c.request("turn/start", {
            "threadId": tid, "effort": "medium", "summary": "detailed",
            "input": text_input("Čo si odpovedal v predchádzajúcom ťahu? "
                                "A koľko je 17*23? Odpovedz krátko.")})
        done = c.wait_method("turn/completed", 180, start)
        print("turn/completed:", json.dumps(done, ensure_ascii=False)[:800])
    c.close()


if __name__ == "__main__":
    main()
