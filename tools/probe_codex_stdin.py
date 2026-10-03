# Kedy sa smie zavriet stdin `codex app-server`?  V Claude je stdin control
# kanal a zavriet ho pred `result` znamena zamietnutie (invariant 1).  Tu sa
# zavrie stdin hned po turn/started a meria sa: skonci proces hned, dobehne
# tah, a co zostane v rollout subore na disku (task_complete / turn_aborted?).
#
# approvalPolicy never + read-only, aby sa nikto nemusel na nic pytat.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import time

from probe_codex_rpc import Client, REPO, fresh_repo, text_input


def main():
    fresh_repo()
    c = Client("stdin")
    c.initialize()
    r = c.request("thread/start", {"cwd": REPO, "model": "gpt-6-luna",
                                   "approvalPolicy": "never",
                                   "sandbox": "read-only"})
    th = r["result"]["thread"]
    start = len(c.notes)
    c.request("turn/start", {"threadId": th["id"], "effort": "low",
                             "input": text_input("Spusti `echo ahoj` a "
                                                 "odpovedz jedným slovom.")})
    c.wait_method("turn/started", 30, start)
    t0 = time.time()
    c.child.stdin.close()
    c.write_log("#", "stdin closed right after turn/started")
    try:
        code = c.child.wait(120)
    except Exception:
        code = "timeout"
    print(f"exit {code} after {time.time() - t0:.2f}s")
    print("turn/completed seen:",
          any(m.get("method") == "turn/completed" for _, m in c.notes))
    time.sleep(1.0)
    path = th["path"]
    print("rollout exists:", os.path.exists(path))
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            rec = json.loads(line)
            print("  ", rec["type"], rec.get("payload", {}).get("type", ""))


if __name__ == "__main__":
    main()
