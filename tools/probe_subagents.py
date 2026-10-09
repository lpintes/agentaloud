# Viac subagentov naraz: co CLI posle na stdout, ked hlavny agent spusti
# tri volania nastroja Agent v jednej sprave -- v popredi (A) alebo
# s run_in_background (B), a co spravi interrupt pocas behu na pozadi (C).
#
# probe_subagents.py <prazdny-priecinok> <scenar> [sekundy] [model]
#   fg        A: traja subagenti v popredi, caka na result
#   bg        B: na pozadi, po result nic neposle a caka [sekundy]
#   bg-int    C1: ako bg, interrupt 3 s po prvom result (mimo tahu)
#   bg-self   C2: ako bg, interrupt hned po system/init prveho samovolneho tahu
#   bg-stop   ako bg, 3 s po prvom result control_request stop_task na agenta 2
#   fg-bgall  ako fg, 4 s po task_started tretieho agenta control_request
#             background_tasks bez tool_use_id (ekvivalent Ctrl+B)
#   fg-bash   bez subagentov: jeden Bash v popredi, 2 s po jeho task_started
#             background_tasks bez tool_use_id (Ctrl+B na prikaz)
#   bg-bash   dva prikazy na pozadi; 3 s po result interrupt mimo tahu,
#             o 8 s stop_task na prvy prikaz
#
# Povolenia: can_use_tool -> allow.  Stdin sa zatvara az na konci.  Surovy
# zaznam ide do <priecinok>/raw-<scenar>.jsonl (mimo repozitara -- nesie
# cesty tohto stroja).
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import subprocess
import sys
import threading
import time

SUB_TASK = (
    "You are agent {n}. Repeat this exactly 5 times, for k = 1..5: run the "
    "Bash command `ping -n 4 127.0.0.1 > nul` (one Bash call per step, "
    "sequentially, never in parallel), and after each call write a short "
    "text message 'agent {n}: step k'. Then finish with 'agent {n}: done'.")


def prompt(background):
    flag = ("Set run_in_background to true on every one of the three Agent "
            "calls. After launching them, end your turn IMMEDIATELY with one "
            "short sentence; do not wait for them and do not check on them. "
            "When you are later notified that an agent finished, reply with "
            "one short sentence only."
            if background else
            # CLI 2.1.293: run_in_background chybajuci = na pozadi
            # (shouldRunAsync: wantsBackground !== false).  Popredie len
            # vyslovnym false.
            "Set run_in_background to false (explicitly, the boolean false) "
            "on every one of the three Agent calls. Wait for all three "
            "results, then reply with one short sentence.")
    tasks = " ".join("Agent %d prompt: \"%s\"" % (n, SUB_TASK.format(n=n))
                     for n in (1, 2, 3))
    return ("You MUST use the Agent tool. In ONE single message, emit exactly "
            "three Agent tool calls in parallel (subagent_type "
            "general-purpose, descriptions 'agent 1', 'agent 2', 'agent 3'). "
            "Do not run any Bash yourself. " + flag + " " + tasks)


BASH_PROMPT = (
    "Run the Bash command `ping -n 60 127.0.0.1` exactly once, in the "
    "foreground (run_in_background false). Then reply with its last line.")


BG_BASH_PROMPT = (
    "Run two Bash commands, both with run_in_background true: "
    "`ping -n 200 127.0.0.1` and `ping -n 201 127.0.0.1`. Do not wait for "
    "them; reply 'started' and end your turn. When later notified about "
    "them, reply with one short sentence only.")


def send(child, record):
    child.stdin.write(json.dumps(record) + "\n")
    child.stdin.flush()


def short(s, n=60):
    return str(s)[:n].replace("\n", " ").replace("\r", "")


def describe(record, agents):
    kind = record.get("type")
    sub = record.get("subtype", "")
    out = "%s/%s" % (kind, sub) if sub else str(kind)
    ptu = record.get("parent_tool_use_id")
    if ptu:
        out += " <%s>" % agents.get(ptu, ptu[-8:])
    if kind == "user":
        content = record.get("message", {}).get("content")
        if isinstance(content, str):
            out += " text: " + short(content)
        elif isinstance(content, list):
            parts = []
            for part in content:
                t = part.get("type")
                if t == "text":
                    parts.append("text: " + short(part.get("text", "")))
                elif t == "tool_result":
                    c = part.get("content")
                    if isinstance(c, list):
                        c = " ".join(x.get("text", "") for x in c
                                     if isinstance(x, dict))
                    tid = part.get("tool_use_id", "")
                    parts.append("tool_result(%s): %s" % (
                        agents.get(tid, tid[-6:]), short(c)))
                else:
                    parts.append(str(t))
            out += " [" + " | ".join(parts) + "]"
        for key in ("isReplay", "isSynthetic"):
            if record.get(key):
                out += " %s=%s" % (key, record.get(key))
    elif kind == "assistant":
        parts = []
        for b in record.get("message", {}).get("content", []):
            t = b.get("type", "?")
            if t == "text":
                parts.append("text: " + short(b.get("text", "")))
            elif t == "tool_use":
                inp = b.get("input", {})
                if b.get("name") in ("Agent", "Task"):
                    agents.setdefault(b.get("id"), "A:" + str(
                        inp.get("description")))
                    parts.append("tool_use %s(%s bg=%s) id=..%s" % (
                        b.get("name"), inp.get("description"),
                        inp.get("run_in_background"), b.get("id", "")[-6:]))
                else:
                    parts.append("tool_use %s(%s)" % (
                        b.get("name"), short(inp.get("command",
                                                     json.dumps(inp)), 40)))
            else:
                parts.append(t)
        out += " [" + " | ".join(parts) + "]"
    elif kind == "control_request":
        req = record.get("request", {})
        out += " " + req.get("subtype", "") + " " + str(req.get("tool_name", ""))
    elif kind == "control_response":
        resp = record.get("response", {})
        out += " " + resp.get("subtype", "") + " id=" + str(
            resp.get("request_id"))
        if str(resp.get("request_id", "")).startswith("ctl-"):
            out += " " + short(json.dumps(resp.get("response")), 200)
        if resp.get("error"):
            out += " error=" + short(resp.get("error"), 100)
    elif kind == "system" and sub in ("init", "hook_started", "hook_response"):
        pass
    elif kind == "system":
        rest = {k: v for k, v in record.items()
                if k not in ("type", "subtype", "uuid", "session_id",
                             "parent_tool_use_id")}
        if sub == "background_tasks_changed":
            tasks = rest.get("tasks", [])
            out += " n=%d %s" % (len(tasks), short(json.dumps(tasks), 400))
        else:
            out += " " + short(json.dumps(rest), 300)
    elif kind == "result":
        out += " is_error=%s" % record.get("is_error")
        if record.get("result"):
            out += " " + short(record.get("result"))
    return out


def main():
    workdir = sys.argv[1]
    scenario = sys.argv[2]
    wait = float(sys.argv[3]) if len(sys.argv) > 3 else 90.0
    model = sys.argv[4] if len(sys.argv) > 4 else "haiku"
    background = scenario.startswith("bg")
    # fg-bgall: prompt ako fg; agentov v popredi presunie az control_request.

    args = ["claude", "-p", "--verbose",
            "--input-format", "stream-json",
            "--output-format", "stream-json",
            "--permission-prompt-tool", "stdio",
            "--model", model]
    # Sonda sa casto spusta z vnutra Claude Code; jeho premenne (CLAUDECODE,
    # CLAUDE_CODE_ENTRYPOINT=sdk-cli, CHILD_SESSION ...) by zmenili spravanie
    # CLI oproti appke.  Nastavenia z settings.json env si CLI nacita samo.
    keep = ("CLAUDE_CODE_GIT_BASH_PATH",)
    env = {k: v for k, v in os.environ.items()
           if not (k.startswith("CLAUDE") and k not in keep)}
    child = subprocess.Popen(
        args, cwd=workdir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True, encoding="utf-8", shell=True,
        env=env)
    started = time.time()
    results = []
    agents = {}
    lock = threading.Lock()
    state = {"interrupted": False}
    raw = open(os.path.join(workdir, "raw-%s.jsonl" % scenario), "w",
               encoding="utf-8")

    def interrupt(now, why):
        state["interrupted"] = True
        print("%6.1fs >>> interrupt (%s)" % (now, why), flush=True)
        with lock:
            send(child, {"type": "control_request", "request_id": "int-1",
                         "request": {"subtype": "interrupt"}})

    agent_tasks = []
    shell_tasks = []

    def control(now, request, why):
        print("%6.1fs >>> %s (%s)" % (now, request["subtype"], why),
              flush=True)
        with lock:
            send(child, {"type": "control_request",
                         "request_id": "ctl-" + request["subtype"],
                         "request": request})

    def read():
        for line in child.stdout:
            now = time.time() - started
            raw.write(line)
            raw.flush()
            try:
                record = json.loads(line)
            except ValueError:
                print("%6.1fs NEJSON %s" % (now, line[:100].rstrip()))
                continue
            print("%6.1fs %s" % (now, describe(record, agents)), flush=True)
            kind = record.get("type")
            if kind == "control_request":
                req = record["request"]
                if req.get("subtype") == "can_use_tool":
                    with lock:
                        send(child, {
                            "type": "control_response",
                            "response": {
                                "subtype": "success",
                                "request_id": record["request_id"],
                                "response": {
                                    "behavior": "allow",
                                    "updatedInput": req.get("input", {})}}})
            # Nie podla tool_use: task local_bash vznika az sekundy po nom
            # (namerane 8 s) a background_tasks pred nim prejde ako success {}
            # bez ucinku.
            elif (scenario == "fg-bash" and kind == "system"
                  and record.get("subtype") == "task_started"
                  and record.get("task_type") == "local_bash"
                  and not state.get("bash")):
                state["bash"] = True
                threading.Timer(2.0, lambda: control(
                    time.time() - started, {"subtype": "background_tasks"},
                    "Bash v popredi")).start()
            elif kind == "result":
                results.append(now)
            elif (kind == "system" and record.get("subtype") == "task_started"
                  and record.get("task_type") == "local_bash"):
                shell_tasks.append(record.get("task_id"))
            elif (kind == "system" and record.get("subtype") == "task_started"
                  and record.get("task_type") == "local_agent"):
                agent_tasks.append(record.get("task_id"))
                if scenario == "fg-bgall" and len(agent_tasks) == 3:
                    threading.Timer(4.0, lambda: control(
                        time.time() - started,
                        {"subtype": "background_tasks"},
                        "vsetky v popredi")).start()
            elif (scenario == "bg-self" and results
                  and not state["interrupted"]
                  and kind == "system" and record.get("subtype") == "init"):
                interrupt(now, "po init samovolneho tahu")

    threading.Thread(target=read, daemon=True).start()

    with lock:
        send(child, {"type": "control_request", "request_id": "init-1",
                     "request": {"subtype": "initialize", "hooks": {}}})
        send(child, {"type": "user", "message": {
            "role": "user",
            "content": [{"type": "text", "text": BASH_PROMPT
                         if scenario == "fg-bash" else BG_BASH_PROMPT
                         if scenario == "bg-bash" else prompt(background)}]}})

    deadline = time.time() + 240
    while not results and time.time() < deadline:
        time.sleep(0.2)
    print("%6.1fs --- prvy result, cakam %ss" % (time.time() - started, wait),
          flush=True)
    end = time.time() + wait
    if scenario == "bg-int":
        time.sleep(3)
        interrupt(time.time() - started, "mimo tahu")
    if scenario == "bg-bash":
        time.sleep(3)
        interrupt(time.time() - started, "mimo tahu, bezia dva prikazy")
        time.sleep(8)
        if shell_tasks:
            control(time.time() - started,
                    {"subtype": "stop_task", "task_id": shell_tasks[0]},
                    "prikaz 1 = " + shell_tasks[0])
    if scenario == "bg-stop" and len(agent_tasks) > 1:
        time.sleep(3)
        control(time.time() - started,
                {"subtype": "stop_task", "task_id": agent_tasks[1]},
                "agent 2 = " + agent_tasks[1])
    while time.time() < end:
        time.sleep(0.2)
    print("%6.1fs --- zatvaram stdin" % (time.time() - started), flush=True)
    child.stdin.close()
    try:
        child.wait(timeout=30)
    except subprocess.TimeoutExpired:
        child.kill()
    time.sleep(0.5)
    raw.close()


if __name__ == "__main__":
    main()
