# /clear ako v TUI: druhe thread/start v tom istom `codex app-server`
# (claude-gui-lkk.98).
#
#   probe_codex_clear.py clear [--unsub]     tah v A, potom thread/start B so
#                                            sessionStartSource clear a menom,
#                                            tah v B: pamata si B nieco z A?
#   probe_codex_clear.py clear-sub [--unsub] v A dvaja subagenti bez cakania,
#                                            hned po tahu /clear; co chodi z A
#   probe_codex_clear.py clear-bg [--unsub]  v A prikaz na pozadi (unified
#                                            exec), po tahu /clear; co chodi z A
#
# S --unsub sa po otvoreni B posle thread/unsubscribe na A.  TUI to nerobi;
# sonda meria, ci by to appke ubralo starosti, alebo ich pridalo.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import sys
import time

from probe_codex_rpc import Client, fresh_repo
from probe_codex_subagents import (SPAWN, accept_all, short, start_thread,
                                   summarize, turn, wait_main_completed)


def last_answer(c, tid, start):
    text = ""
    for _, m in c.notes[start:]:
        p = m.get("params") or {}
        if (m.get("method") == "item/completed" and p.get("threadId") == tid
                and p["item"].get("type") == "agentMessage"):
            text = p["item"].get("text", "")
    return text


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "clear"
    unsub = "--unsub" in sys.argv
    fresh_repo()
    c = Client("clear-" + mode + ("-unsub" if unsub else ""),
               on_request=accept_all)
    c.initialize(experimental=True)

    a = start_thread(c)
    start = len(c.notes)
    if mode == "clear":
        text = "Remember the word PELICAN. Reply only OK."
    elif mode == "clear-sub":
        text = SPAWN.format(n=20, m=30, tail="Do NOT wait for them; end "
                            "your turn immediately after spawning with "
                            "the word 'spawned'.")
    else:
        text = ("Call exec_command with cmd `ping -n 30 127.0.0.1` and "
                "yield_time_ms 1500, so the process keeps running in its "
                "session after the call returns. Do not use Start-Process, "
                "do not poll it with write_stdin, do not wait. Then end your "
                "turn with the word 'started'.")
    turn(c, a, text)
    wait_main_completed(c, a, start)
    print("== A answered:", last_answer(c, a, start))

    clear_at = len(c.notes)
    r = c.request("thread/start", {
        "cwd": "C:/b/codex-probe/repo", "model": "gpt-6-luna",
        "approvalPolicy": "never", "sandbox": "workspace-write",
        "sessionStartSource": "clear"})
    print("== thread/start clear ->", short(r, 2500))
    b = r["result"]["thread"]["id"]
    print("== B != A:", b != a)
    print("== thread/name/set ->", short(c.request(
        "thread/name/set", {"threadId": b, "name": "po clear"}), 400))
    if unsub:
        print("== thread/unsubscribe A ->", short(c.request(
            "thread/unsubscribe", {"threadId": a}), 400))

    if mode == "clear":
        start = len(c.notes)
        turn(c, b, "What word did I ask you to remember? If none, "
                   "reply only NONE.")
        wait_main_completed(c, b, start)
        print("== B answered:", last_answer(c, b, start))
    else:
        print("== observing 40 s after clear")
        time.sleep(40)

    print("== thread/loaded/list ->", short(c.request(
        "thread/loaded/list", {}), 800))
    for tid in (a, b):
        th = (c.request("thread/read", {"threadId": tid}) or {}).get(
            "result", {}).get("thread", {})
        print("== thread/read", short({k: th.get(k) for k in (
            "id", "name", "status", "source", "threadSource")}, 600))
    print("== everything since clear (M = B):")
    summarize(c, clear_at, b)
    c.close()


if __name__ == "__main__":
    main()
