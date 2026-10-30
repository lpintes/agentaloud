# Zmena modelu v `codex app-server`: turn/start.model, thread/settings/update,
# turn/settings/update, zmena pocas tahu, thread/resume a effort.
#
#   probe_codex_model.py list      model/list (modely, effort, predvoleny)
#   probe_codex_model.py turn      tri tahy: bez modelu, s model=B, bez modelu
#   probe_codex_model.py settings  thread/settings/update model medzi tahmi
#   probe_codex_model.py midturn   pocas tahu thread/settings/update,
#                                  turn/settings/update a turn/start s modelom
#   probe_codex_model.py midbad    pocas tahu thread/settings/update na
#                                  neexistujuci model: zlyha bezaci tah?
#   probe_codex_model.py midstep   to iste cez turn/settings/update
#                                  (--enable step_model_switching)
#   probe_codex_model.py pending   thread/settings/update bez dalsieho tahu,
#                                  vypise id na resume
#   probe_codex_model.py resume <threadId>   novy proces, thread/resume
#   probe_codex_model.py effort    zmena modelu s/bez effort, neplatny effort
#   probe_codex_model.py effortreset  effort null a zmena rezimu s effortom
#
# Ktory model tah naozaj pouzil, hovori rollout na disku (`turn_context`
# s polom model pre kazdy tah); cestu dava thread.path.  Log v
# C:/b/codex-probe/logs/model-<rezim>.log, poznamky v
# tools/probe_codex_model.notes.md.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import sys
import time

# python -I nedava priecinok skriptu do sys.path; tools/ je nas, nie cudzi.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_codex_rpc import Client, text_input

WORKDIR = "C:/b/codex-probe/model-repo"
MODEL_A = os.environ.get("MODEL_A", "gpt-6-luna")
MODEL_B = os.environ.get("MODEL_B", "gpt-5.6-luna")
PROMPT = "Reply with the word ok."
LONG = ("Run the shell command `ping -n 8 127.0.0.1`, wait for it to finish, "
        "then reply with the word ok.")


def short(obj, n=600):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + "..."


def accept_all(client, msg):
    client.respond(msg["id"], {"decision": "accept"})


def watched(c, start=0):
    """Notifications that could mention a model."""
    for t, m in c.notes[start:]:
        method = m.get("method", "")
        if method in ("thread/settings/updated", "model/rerouted",
                      "model/verification", "turn/started", "turn/completed",
                      "error", "thread/tokenUsage/updated",
                      "configWarning") or "model" in method.lower():
            print(f"  {t:7.2f} {method} {short(m.get('params'), 700)}")
        elif method == "item/completed" and \
                m["params"]["item"]["type"] == "agentMessage":
            print(f"  {t:7.2f} agentMessage "
                  f"{short(m['params']['item'].get('text'), 200)}")


def rollout_models(path):
    """(turn_id, model, effort) from turn_context lines of the rollout."""
    out = []
    if not path or not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            if rec.get("type") == "turn_context":
                p = rec.get("payload", {})
                cm = (p.get("collaboration_mode") or {}).get("settings", {})
                out.append((p.get("turn_id"), p.get("model"),
                            cm.get("reasoning_effort")))
            elif rec.get("type") == "event_msg" and \
                    rec.get("payload", {}).get("type") == \
                    "thread_settings_applied":
                ts = rec["payload"].get("thread_settings", {})
                out.append(("thread_settings_applied", ts.get("model"),
                            ts.get("reasoning_effort",
                                   ts.get("effort"))))
    return out


def start_thread(c, model=MODEL_A, **extra):
    params = {"cwd": WORKDIR, "model": model,
              "approvalPolicy": "never", "sandbox": "read-only"}
    params.update(extra)
    r = c.request("thread/start", params)
    res = r["result"]
    print("thread/start ->", short({k: res.get(k) for k in
                                    ("model", "reasoningEffort")}))
    return res["thread"]["id"], res["thread"].get("path")


def turn(c, tid, text=PROMPT, wait=True, **extra):
    params = {"threadId": tid, "input": text_input(text)}
    params.update(extra)
    n = len(c.notes)
    r = c.request("turn/start", params)
    print(f"{c.t():7.2f} turn/start {short(extra)} -> {short(r, 300)}")
    if wait:
        c.wait_method("turn/completed", 180, n)
        time.sleep(0.5)
        watched(c, n)
    return r


def settings(c, tid, **fields):
    params = {"threadId": tid}
    params.update(fields)
    n = len(c.notes)
    r = c.request("thread/settings/update", params)
    print(f"{c.t():7.2f} thread/settings/update {short(fields)} -> "
          f"{short(r, 400)}")
    time.sleep(1.0)
    watched(c, n)
    return r


def show_rollout(path):
    print("--- rollout turn_context ---")
    for tc in rollout_models(path):
        print("  ", tc)


def mode_list(c):
    r = c.request("model/list", {"includeHidden": False})
    for m in r["result"]["data"]:
        print(m["model"], "default" if m.get("isDefault") else "",
              "defaultEffort=" + str(m.get("defaultReasoningEffort")),
              [e["reasoningEffort"] for e in m["supportedReasoningEfforts"]])
    print(short(r["result"]["data"][0], 2000))


def mode_turn(c):
    tid, path = start_thread(c)
    print("thread", tid)
    turn(c, tid)
    turn(c, tid, model=MODEL_B)
    turn(c, tid)
    r = c.request("thread/read", {"threadId": tid})
    print("thread/read ->", short(r, 500))
    show_rollout(path)


def mode_settings(c):
    tid, path = start_thread(c)
    turn(c, tid)
    settings(c, tid, model=MODEL_B)
    turn(c, tid)
    settings(c, tid, model="no-such-model-xyz")
    turn(c, tid)
    show_rollout(path)


def mode_midturn(c):
    tid, path = start_thread(c)
    n = len(c.notes)
    r = turn(c, tid, LONG, wait=False)
    tu = r["result"]["turn"]["id"]
    c.wait_for(lambda m: m.get("method") == "item/started" and
               m["params"]["item"]["type"] == "commandExecution", 120, n)
    time.sleep(1.0)
    settings(c, tid, model=MODEL_B)
    r2 = c.request("turn/settings/update",
                   {"threadId": tid, "turnId": tu, "model": MODEL_B})
    print(f"{c.t():7.2f} turn/settings/update -> {short(r2)}")
    r3 = c.request("turn/start", {"threadId": tid, "model": MODEL_A,
                                  "input": text_input("Also say BANANA.")})
    print(f"{c.t():7.2f} turn/start during turn (model A) -> {short(r3)}")
    r4 = c.request("turn/steer", {"threadId": tid, "expectedTurnId": tu,
                                  "model": MODEL_A,
                                  "input": text_input("And say KIWI.")})
    print(f"{c.t():7.2f} turn/steer with model -> {short(r4)}")
    c.wait_method("turn/completed", 180, n)
    time.sleep(15.0)
    watched(c, n)
    turn(c, tid)
    r = c.request("turn/settings/update",
                  {"threadId": tid, "turnId": tu, "model": MODEL_A})
    print(f"{c.t():7.2f} turn/settings/update after turn -> {short(r)}")
    show_rollout(path)


def mode_midbad(c, step):
    """A bogus model set mid-turn: if the running turn's next inference used
    it, the turn would fail with model_not_found.  step=True goes through
    turn/settings/update (needs --enable step_model_switching)."""
    tid, path = start_thread(c)
    n = len(c.notes)
    r = turn(c, tid, LONG, wait=False)
    tu = r["result"]["turn"]["id"]
    c.wait_for(lambda m: m.get("method") == "item/started" and
               m["params"]["item"]["type"] == "commandExecution", 120, n)
    time.sleep(1.0)
    if step:
        r2 = c.request("turn/settings/update",
                       {"threadId": tid, "turnId": tu,
                        "model": os.environ.get("STEP_MODEL", "no-such-model-xyz")})
        print(f"{c.t():7.2f} turn/settings/update -> {short(r2)}")
    else:
        settings(c, tid, model="no-such-model-xyz")
    c.wait_method("turn/completed", 180, n)
    time.sleep(1.0)
    watched(c, n)
    turn(c, tid)
    show_rollout(path)


def mode_pending(c):
    """thread/settings/update with no turn after it, then close; resume the
    printed thread id to see whether the change survived."""
    tid, path = start_thread(c)
    turn(c, tid)
    settings(c, tid, model=MODEL_B, effort="low")
    print("resume this:", tid)
    show_rollout(path)


def mode_resume(c, tid):
    n = len(c.notes)
    r = c.request("thread/resume", {"threadId": tid, "excludeTurns": True})
    res = r.get("result", {})
    print("thread/resume ->", short({k: res.get(k) for k in
                                     ("model", "reasoningEffort")}),
          short(r.get("error")))
    time.sleep(1.0)
    watched(c, n)
    turn(c, tid)
    show_rollout(res.get("thread", {}).get("path"))


def mode_effort(c):
    tid, path = start_thread(c)
    turn(c, tid, effort="high")
    settings(c, tid, model=MODEL_B)
    settings(c, tid, model=MODEL_A, effort="low")
    settings(c, tid, effort="ultra")
    turn(c, tid)
    turn(c, tid, model=MODEL_B, effort="bogus-effort")
    show_rollout(path)


def collaboration(model, effort):
    return {"mode": "default",
            "settings": {"model": model, "reasoning_effort": effort,
                         "developer_instructions": None}}


def mode_effortreset(c):
    """Da sa effort vratit na predvoleny, a zhodi ho zmena rezimu?"""
    tid, path = start_thread(c)
    settings(c, tid, effort="low")
    turn(c, tid)
    settings(c, tid, effort=None)
    turn(c, tid)
    settings(c, tid, effort="high")
    settings(c, tid, collaborationMode=collaboration(MODEL_A, None))
    turn(c, tid)
    settings(c, tid, effort="low")
    settings(c, tid, collaborationMode=collaboration(MODEL_A, "low"))
    turn(c, tid)
    settings(c, tid, collaborationMode=collaboration(MODEL_A, "medium"))
    turn(c, tid)
    show_rollout(path)


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "list"
    os.makedirs(WORKDIR, exist_ok=True)
    extra = ["--enable", "step_model_switching"] if mode == "midstep" else []
    c = Client("model-" + mode, extra_args=extra, on_request=accept_all,
               cwd=WORKDIR)
    c.initialize(experimental=True)
    if mode == "midbad":
        mode_midbad(c, step=False)
    elif mode == "midstep":
        mode_midbad(c, step=True)
    elif mode == "pending":
        mode_pending(c)
    elif mode == "list":
        mode_list(c)
    elif mode == "turn":
        mode_turn(c)
    elif mode == "settings":
        mode_settings(c)
    elif mode == "midturn":
        mode_midturn(c)
    elif mode == "resume":
        mode_resume(c, sys.argv[2])
    elif mode == "effort":
        mode_effort(c)
    elif mode == "effortreset":
        mode_effortreset(c)
    c.close()


if __name__ == "__main__":
    main()
