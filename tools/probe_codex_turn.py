# Tvar tahu v `codex app-server`: text, reasoning, prikaz, uprava suboru,
# ziadost o povolenie a odpoved na nu, chyba nastroja, koniec tahu, usage.
#
#   probe_codex_turn.py basic      echo ahoj (accept)
#   probe_codex_turn.py multi      viacriadkovy prompt s diakritikou, CRLF vystup,
#                                  zlyhany prikaz (accept) a zamietnuty (decline)
#   probe_codex_turn.py edit       vytvorenie suboru (fileChange approval)
#   probe_codex_turn.py interrupt  dlhy prikaz, turn/interrupt zvonku
#   probe_codex_turn.py noanswer   ziadost o povolenie bez odpovede, potom interrupt
#   probe_codex_turn.py ask        plan mode + request_user_input
#
# Kazdy rezim je jeden alebo dva tahy na najlacnejsom modeli s effort low.
# Id threadu sa zapise do C:/b/codex-probe/logs/<rezim>.thread, nech ho
# probe_codex_resume.py vie obnovit.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import sys
import time

from probe_codex_rpc import Client, LOGDIR, REPO, fresh_repo, text_input

MODEL = "gpt-6-luna"


def short(obj, n=500):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + "..."


class Policy:
    """Odpovede na ziadosti servera v poradi prichodu; None = neodpovedat."""

    def __init__(self, decisions):
        self.decisions = list(decisions)
        self.seen = []

    def __call__(self, client, msg):
        self.seen.append(msg)
        method = msg["method"]
        if method == "item/tool/requestUserInput":
            qs = msg["params"]["questions"]
            answers = {q["id"]: {"answers": [q["options"][0]["label"]
                                             if q.get("options") else "caj"]}
                       for q in qs}
            client.respond(msg["id"], {"answers": answers})
            return
        decision = self.decisions.pop(0) if self.decisions else "decline"
        if decision is None:
            return
        client.respond(msg["id"], {"decision": decision})


def run_turn(c, tid, text, extra=None, timeout=180.0, during=None):
    start = len(c.notes)
    params = {"threadId": tid, "input": text_input(text)}
    if extra:
        params.update(extra)
    r = c.request("turn/start", params)
    print("turn/start ->", short(r, 300))
    turn_id = r["result"]["turn"]["id"] if r and "result" in r else None
    if during:
        during(c, tid, turn_id, start)
    done = c.wait_method("turn/completed", timeout, start)
    print("turn/completed:", short(done, 800))
    return turn_id


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "basic"
    fresh_repo()
    decisions = {
        "basic": ["accept"],
        "multi": ["accept", "accept", "decline", "decline"],
        "edit": ["accept", "accept"],
        "interrupt": ["accept"],
        "noanswer": [None],
        "ask": [],
    }[mode]
    policy = Policy(decisions)
    c = Client(mode, on_request=policy)
    c.initialize(experimental=True)
    r = c.request("thread/start", {
        "cwd": REPO, "model": MODEL, "approvalPolicy": "untrusted",
        "sandbox": "workspace-write" if mode == "edit" else "read-only"})
    tid = r["result"]["thread"]["id"]
    with open(f"{LOGDIR}/{mode}.thread", "w") as f:
        f.write(tid)
    print("thread", tid)
    extra = {"effort": "low", "summary": "detailed"}

    if mode == "basic":
        run_turn(c, tid, "Spusti v shelli príkaz `echo ahoj` a odpovedz "
                         "jedným slovom, čo vypísal.", extra)
    elif mode == "multi":
        run_turn(c, tid,
                 "Toto je viacriadkový prompt.\nDruhý riadok: žľťčáéíô.\r\n"
                 "Úloha: spusti postupne tri príkazy, každý zvlášť:\n"
                 "1. `cmd /c \"echo prvy& echo druhy\"`\n"
                 "2. `git log`\n"
                 "3. `whoami`\n"
                 "Potom stručne povedz, čo každý vrátil.", extra)
    elif mode == "edit":
        run_turn(c, tid, "Vytvor v aktuálnom adresári súbor caj.txt s jedným "
                         "riadkom: Čaj je lepší než káva. Použi nástroj "
                         "apply_patch, nie shell. Nič iné nerob.",
                 extra)
    elif mode in ("interrupt", "noanswer"):
        def during(c, tid, turn_id, start):
            wait = 40.0 if mode == "noanswer" else 8.0
            if mode == "interrupt":
                c.wait_method("item/commandExecution/outputDelta", 60, start) \
                    or c.wait_for(lambda m: m.get("method") == "item/started"
                                  and m["params"]["item"]["type"]
                                  == "commandExecution", 60, start)
                time.sleep(3.0)
            else:
                c.wait_for(lambda m: "id" in m and "method" in m, 90, start)
                time.sleep(wait)
            print("sending turn/interrupt")
            print("interrupt ->", short(c.request("turn/interrupt", {
                "threadId": tid, "turnId": turn_id})))
        run_turn(c, tid, "Spusti v shelli príkaz `ping -n 30 127.0.0.1` "
                         "a počkaj, kým skončí.", extra, during=during)
    elif mode == "ask":
        ask_extra = dict(extra)
        ask_extra["collaborationMode"] = {
            "mode": "plan",
            "settings": {"model": MODEL, "reasoning_effort": "low",
                         "developer_instructions": None}}
        run_turn(c, tid, "Skôr než čokoľvek navrhneš, opýtaj sa ma cez nástroj "
                         "request_user_input jednou otázkou s dvoma možnosťami, "
                         "či pijem radšej čaj alebo kávu. Potom odpovedz jednou "
                         "vetou.", ask_extra)

    time.sleep(1.0)
    print("--- requests from server ---")
    for m in policy.seen:
        print(short(m, 1200))
    print("--- method counts ---")
    counts = {}
    for _, m in c.notes:
        k = m.get("method", "<response>")
        counts[k] = counts.get(k, 0) + 1
    for k, v in counts.items():
        print(f"{v:4d} {k}")
    c.close()


if __name__ == "__main__":
    main()
