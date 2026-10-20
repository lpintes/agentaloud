# Vstup od pouzivatela pocas beziaceho tahu v `codex app-server`.
#
#   probe_codex_midturn.py steer      turn/steer (najprv so zlym expectedTurnId,
#                                     potom so spravnym) ~2 s po starte prikazu
#   probe_codex_midturn.py start      turn/start namiesto steer pocas tahu
#   probe_codex_midturn.py interrupt  turn/steer a hned turn/interrupt
#   probe_codex_midturn.py idle       turn/steer, ked ziadny tah nebezi
#
# Kazdy rezim je jeden tah na najlacnejsom modeli s effort low; na kazdu
# ziadost o povolenie sa odpovie accept.  Log v C:/b/codex-probe/logs/
# midturn-<rezim>.log, poznamky v tools/probe_codex_midturn.notes.md.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import sys
import time

# python -I nedava priecinok skriptu do sys.path; tools/ je nas, nie cudzi.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_codex_rpc import Client, text_input

MODEL = "gpt-6-luna"
WORKDIR = "C:/b/codex-probe/midturn-repo"
EXTRA = {"effort": "low", "summary": "detailed"}
PROMPT = ("Spusti v shelli príkaz `ping -n 8 127.0.0.1`, počkaj, kým skončí, "
          "a potom jednou vetou povedz, koľko paketov sa vrátilo.")
STEER = "Also reply with the word BANANA."


def short(obj, n=400):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + "..."


def accept_all(client, msg):
    client.respond(msg["id"], {"decision": "accept"})


def is_command_started(m):
    return (m.get("method") == "item/started"
            and m["params"]["item"]["type"] == "commandExecution")


def summary(c):
    print("--- sequence ---")
    for t, m in c.notes:
        method = m.get("method")
        p = m.get("params", {})
        if method in ("turn/started", "turn/completed"):
            turn = p.get("turn", {})
            print(f"{t:7.2f} {method} turn={turn.get('id')} "
                  f"status={turn.get('status')} err={turn.get('error')}")
        elif method in ("item/started", "item/completed"):
            item = p["item"]
            extra = ""
            if item["type"] == "userMessage":
                extra = short(item.get("content"), 200)
            elif item["type"] == "agentMessage":
                extra = short(item.get("text"), 300)
            elif item["type"] == "commandExecution":
                extra = f"status={item.get('status')}"
            if item["type"] != "reasoning" or method == "item/completed":
                print(f"{t:7.2f} {method} {item['type']} turn={p.get('turnId')}"
                      f" {extra}")
        elif method == "error":
            print(f"{t:7.2f} error {short(p, 300)}")
        elif "id" in m and "method" in m:
            print(f"{t:7.2f} REQUEST {method}")
        elif "id" in m and "method" not in m:
            pass
    print("--- method counts ---")
    counts = {}
    for _, m in c.notes:
        k = m.get("method", "<response>")
        counts[k] = counts.get(k, 0) + 1
    for k, v in counts.items():
        print(f"{v:4d} {k}")


def steer(c, tid, turn, text=STEER):
    r = c.request("turn/steer", {"threadId": tid, "expectedTurnId": turn,
                                 "input": text_input(text)})
    print(f"{c.t():7.2f} turn/steer(expected={turn}) -> {short(r)}")
    return r


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "steer"
    os.makedirs(WORKDIR, exist_ok=True)
    c = Client("midturn-" + mode, on_request=accept_all, cwd=WORKDIR)
    c.initialize(experimental=True)
    r = c.request("thread/start", {"cwd": WORKDIR, "model": MODEL,
                                   "approvalPolicy": "untrusted",
                                   "sandbox": "read-only"})
    tid = r["result"]["thread"]["id"]
    print("thread", tid)

    if mode == "idle":
        params = {"threadId": tid, "input": text_input("Odpovedz slovom ahoj.")}
        params.update(EXTRA)
        r = c.request("turn/start", params)
        turn = r["result"]["turn"]["id"]
        c.wait_method("turn/completed", 120)
        print(f"{c.t():7.2f} turn 1 completed")
        steer(c, tid, turn)
        steer(c, tid, "bogus-turn-id")
        time.sleep(5.0)
        summary(c)
        c.close()
        return

    params = {"threadId": tid, "input": text_input(PROMPT)}
    params.update(EXTRA)
    r = c.request("turn/start", params)
    print(f"{c.t():7.2f} turn/start -> {short(r, 300)}")
    turn = r["result"]["turn"]["id"]
    if not c.wait_for(is_command_started, 120):
        print("command never started")
    time.sleep(2.0)

    if mode == "steer":
        steer(c, tid, "bogus-turn-id")
        steer(c, tid, turn)
    elif mode == "start":
        p2 = {"threadId": tid, "input": text_input(STEER)}
        p2.update(EXTRA)
        r2 = c.request("turn/start", p2)
        print(f"{c.t():7.2f} turn/start #2 -> {short(r2)}")
    elif mode == "interrupt":
        steer(c, tid, turn)
        time.sleep(0.5)
        ri = c.request("turn/interrupt", {"threadId": tid, "turnId": turn})
        print(f"{c.t():7.2f} turn/interrupt -> {short(ri)}")

    c.wait_method("turn/completed", 180)
    print(f"{c.t():7.2f} first turn/completed")
    # Prip. druhy tah (zaradeny vstup) -- pockaj, ci este nieco pride.
    time.sleep(20.0)
    if mode == "interrupt":
        # Prenesie sa zahodeny steer do dalsieho tahu?
        p3 = {"threadId": tid, "input": text_input("Reply with one word: ok.")}
        p3.update(EXTRA)
        r3 = c.request("turn/start", p3)
        print(f"{c.t():7.2f} turn/start #2 -> {short(r3, 200)}")
        n = len(c.notes)
        c.wait_method("turn/completed", 120, n)
    summary(c)
    c.close()


if __name__ == "__main__":
    main()
