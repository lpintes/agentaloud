# Subagenti a prikazy na pozadi v `codex app-server` (claude-gui-b8n.1).
#
#   probe_codex_subagents.py info       model/list (multiAgentVersion),
#                                       experimentalFeature/list, collaborationMode/list
#   probe_codex_subagents.py spawn      dvaja subagenti, hlavny na nich pocka
#   probe_codex_subagents.py spawn-nowait  dvaja subagenti, hlavny NEcaka a konci
#                                       tah; sleduje sa, co chodi po turn/completed
#   probe_codex_subagents.py spawn-int  dvaja dlhi subagenti, turn/interrupt pocas
#   probe_codex_subagents.py bgterm     dlhy prikaz na pozadi (unified exec),
#                                       potom thread/backgroundTerminals/list a terminate
#   probe_codex_subagents.py bgterm-int dlhy prikaz na pozadi, turn/interrupt, list,
#                                       potom thread/backgroundTerminals/clean
#   probe_codex_subagents.py spawn-kill dvaja subagenti, turn/interrupt na vlakno
#                                       jedneho z nich
#
# Najlacnejsi model, effort low, approvalPolicy never (ziadne dialogy).
# Volitelne dalsie argumenty idu ako `-c key=value` do app-servera.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import sys
import time

from probe_codex_rpc import Client, REPO, fresh_repo, text_input

MODEL = "gpt-6-luna"


def short(obj, n=400):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + "..."


def accept_all(client, msg):
    method = msg["method"]
    print(f"  REQUEST {method} {short(msg.get('params'), 300)}")
    if method == "item/tool/requestUserInput":
        client.respond(msg["id"], {"answers": {}})
    else:
        client.respond(msg["id"], {"decision": "accept"})


def summarize(c, start=0, main_tid=None):
    """Jeden riadok na spravu: cas, metoda, vlakno (M = hlavne), polozka."""
    tids = {}
    for t, m in c.notes[start:]:
        method = m.get("method", "<response>")
        if method in ("item/agentMessage/delta",
                      "item/reasoning/summaryTextDelta",
                      "item/commandExecution/outputDelta",
                      "item/reasoning/textDelta"):
            p = m["params"]
            tid = p.get("threadId", "")
            key = (method, tid, p.get("itemId"))
            # deltas skratit: len prva za polozku
            if key in tids:
                continue
            tids[key] = True
        p = m.get("params") or {}
        tid = p.get("threadId") or (p.get("thread") or {}).get("id") or ""
        tag = "M" if tid == main_tid else (tid[-6:] if tid else "-")
        extra = ""
        if "item" in p:
            it = p["item"]
            extra = f"{it.get('type')} {short(it, 600)}"
        elif method == "thread/started":
            th = p["thread"]
            extra = short({k: th.get(k) for k in (
                "id", "parentThreadId", "source", "agentNickname",
                "agentRole", "status")}, 600)
        elif method.startswith("turn/"):
            extra = short(p, 300)
        elif method == "<response>":
            extra = short(m, 600)
        else:
            extra = short(p, 300)
        print(f"{t:7.2f} {tag:>6} {method} {extra}")


def start_thread(c, extra_params=None):
    params = {"cwd": REPO, "model": MODEL, "approvalPolicy": "never",
              "sandbox": "workspace-write"}
    if extra_params:
        params.update(extra_params)
    r = c.request("thread/start", params)
    print("thread/start ->", short(r, 800))
    return r["result"]["thread"]["id"]


def turn(c, tid, text, extra=None):
    params = {"threadId": tid, "input": text_input(text), "effort": "low"}
    if extra:
        params.update(extra)
    r = c.request("turn/start", params)
    print("turn/start ->", short(r, 300))
    return r["result"]["turn"]["id"] if r and "result" in r else None


def wait_main_completed(c, tid, start, timeout=240):
    return c.wait_for(lambda m: m.get("method") == "turn/completed"
                      and m["params"].get("threadId") == tid, timeout, start)


SPAWN = ("Use the spawn_agent tool to start exactly two subagents in parallel. "
         "Subagent 1 task: run shell command `ping -n {n} 127.0.0.1` and then "
         "reply 'agent 1 done'. Subagent 2 task: run `ping -n {m} 127.0.0.1` and "
         "then reply 'agent 2 done'. {tail}")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "info"
    cfg = []
    for kv in sys.argv[2:]:
        cfg += ["-c", kv]
    fresh_repo()
    c = Client("sub-" + mode, extra_args=cfg, on_request=accept_all)
    c.initialize(experimental=True)

    if mode == "info":
        r = c.request("model/list", {})
        for m in (r.get("result") or {}).get("data", []):
            print("model", m.get("id"), "multiAgentVersion",
                  m.get("multiAgentVersion"))
        r = c.request("experimentalFeature/list", {})
        for f in (r.get("result") or {}).get("data", []):
            name = f.get("name", "")
            if any(k in name for k in ("agent", "exec", "collab", "terminal",
                                       "sleep", "background", "fanout")):
                print("feature", short(f, 400))
        print("collaborationMode/list ->",
              short(c.request("collaborationMode/list", {}), 1500))
        c.close()
        return

    tid = start_thread(c)
    start = len(c.notes)

    if mode in ("spawn", "spawn-nowait", "spawn-int", "spawn-kill"):
        if mode == "spawn-kill":
            text = SPAWN.format(n=40, m=10, tail="Do NOT wait for them; end "
                                "your turn immediately after spawning with "
                                "the word 'spawned'.")
        elif mode == "spawn":
            text = SPAWN.format(n=4, m=6, tail="Wait for both and then report "
                                "in one sentence what each said.")
        elif mode == "spawn-nowait":
            text = SPAWN.format(n=15, m=25, tail="Do NOT wait for them; end "
                                "your turn immediately after spawning with "
                                "the word 'spawned'.")
        else:
            text = SPAWN.format(n=40, m=40, tail="Wait for both.")
        turn_id = turn(c, tid, text)
        if mode == "spawn-int":
            c.wait_for(lambda m: m.get("method") == "item/started"
                       and m["params"]["item"]["type"] == "commandExecution"
                       and m["params"].get("threadId") != tid, 120, start)
            time.sleep(4)
            print("== turn/interrupt", short(c.request(
                "turn/interrupt", {"threadId": tid, "turnId": turn_id})))
        wait_main_completed(c, tid, start)
        if mode == "spawn-kill":
            # vlakno subagenta s ping 40: jeho prikaz uz bezi
            ex = c.wait_for(lambda m: m.get("method") == "item/started"
                            and m["params"]["item"]["type"] ==
                            "commandExecution" and "-n 40" in
                            m["params"]["item"]["command"]
                            and m["params"].get("threadId") != tid, 120, start)
            if ex:
                time.sleep(3)
                sub, sub_turn = ex["params"]["threadId"], ex["params"]["turnId"]
                print("== interrupt subagent", sub, short(c.request(
                    "turn/interrupt", {"threadId": sub, "turnId": sub_turn})))
        print("== main turn completed; observing 45 s")
        time.sleep(45)
        r = c.request("thread/loaded/list", {})
        print("== thread/loaded/list", short(r, 800))
        for sub in (r or {}).get("result", {}).get("data", []):
            if sub == tid:
                continue
            th = (c.request("thread/read", {"threadId": sub}) or {}).get(
                "result", {}).get("thread", {})
            print("== thread/read", short({k: th.get(k) for k in (
                "id", "parentThreadId", "source", "threadSource",
                "agentNickname", "agentRole", "status", "ephemeral")}, 800))
    elif mode in ("bgterm", "bgterm-int"):
        text = ("Call exec_command with cmd `ping -n 40 127.0.0.1` and "
                "yield_time_ms 1500, so the process keeps running in its "
                "session after the call returns. Do not use Start-Process, "
                "do not poll it with write_stdin, do not wait. Then end your "
                "turn with the word 'started'.")
        if mode == "bgterm-int":
            text = ("Call exec_command with cmd `ping -n 40 127.0.0.1` and "
                    "yield_time_ms 1500 (do not use Start-Process, leave it "
                    "running). Then call exec_command with cmd "
                    "`ping -n 30 127.0.0.1` and wait for it to finish.")
        turn_id = turn(c, tid, text)
        if mode == "bgterm-int":
            time.sleep(25)
            print("== list before interrupt", short(c.request(
                "thread/backgroundTerminals/list", {"threadId": tid}), 1200))
            print("== turn/interrupt", short(c.request(
                "turn/interrupt", {"threadId": tid, "turnId": turn_id})))
        wait_main_completed(c, tid, start)
        time.sleep(2)
        r = c.request("thread/backgroundTerminals/list", {"threadId": tid})
        print("== list after turn", short(r, 1200))
        data = (r or {}).get("result", {}).get("data", [])
        if data and mode == "bgterm-int":
            print("== clean", short(c.request(
                "thread/backgroundTerminals/clean", {"threadId": tid}), 600))
        if data and mode == "bgterm":
            time.sleep(5)
            print("== terminate", short(c.request(
                "thread/backgroundTerminals/terminate",
                {"threadId": tid, "processId": data[0]["processId"]}), 600))
            time.sleep(3)
            print("== list after terminate", short(c.request(
                "thread/backgroundTerminals/list", {"threadId": tid}), 1200))
        print("== observing 25 s")
        time.sleep(25)
        print("== list at end", short(c.request(
            "thread/backgroundTerminals/list", {"threadId": tid}), 1200))

    print("--- summary ---")
    summarize(c, start, tid)
    c.close()


if __name__ == "__main__":
    main()
