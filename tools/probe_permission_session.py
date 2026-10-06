# "Povolit na tuto session" cez control kanal (claude-gui-lkk.61).
#
# Terminal ponuka pri povoleni "Yes, and don't ask again for ...".  Otazky:
#   1. Nesie 'can_use_tool' navrhy pravidiel (permission_suggestions) a v akom
#      tvare -- pre Bash aj Write/Edit?
#   2. Ked sa navrh vrati v 'updatedPermissions' s destination prepisanym na
#      "session", prestane sa CLI pytat na rovnake volanie v tej istej session?
#      A nezapise nic do settings?
#   3. Ked navrhy chybaju (ask pravidlo), zoberie CLI vlastne pravidlo -- s
#      ruleContent aj bez neho (cely nastroj)?
#
# POSIELA PROMPTY, stoji kredit (haiku, par kratkych tahov).  Pracovny
# priecinok je C:\b\perm-probe\<scenar>, vzdy cerstvy.  Prieskumny nastroj,
# nie sucast produktu (viz CLAUDE.md).
#
# Pouzitie:
#   python tools/probe_permission_session.py <scenar>
#   scenare: bash, write, rule, tool, none
#
# Pred a po sa porovnaju bajty ~/.claude/settings.json, settings.local.json
# a .claude/settings*.json v pracovnom priecinku.  Navrh s destination
# userSettings sa NIKDY neposle bez prepisu -- vsetky sa prepisu na session.

import hashlib
import json
import os
import shutil
import subprocess
import sys
import threading
import time

ROOT = r"C:\b\perm-probe"
IDLE_LIMIT = 120  # sekund bez noveho zaznamu = koniec s chybou

# Konzola je cp1250 a CLI posiela znaky mimo nej; padnutý print zabil
# citacie vlakno a sonda visela.
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HOME_CLAUDE = os.path.join(os.path.expanduser("~"), ".claude")

ARGS = [
    "claude", "-p", "--verbose",
    "--input-format", "stream-json",
    "--output-format", "stream-json",
    "--permission-prompt-tool", "stdio",
    "--model", "haiku",
]


def run_prompt(cmd):
    return ("Use the Bash tool to run exactly this command, verbatim, and "
            "nothing else: %s   Do not run any other command. If the tool "
            "is denied, just say so and stop." % cmd)


SCENARIOS = {
    # Navrhy CLI, prepisane na session; potom ten isty a podobny prikaz.
    "bash": {
        "policy": "suggest",
        "prompts": [run_prompt("echo hello > hello.txt"),
                    run_prompt("echo hello > hello.txt"),
                    run_prompt("echo world > other.txt")],
    },
    # Prikaz bez zapisu suboru: pyta sa len pre pravidlo, nie pre cestu.
    "ping": {
        "policy": "suggest",
        "prompts": [run_prompt("ping -n 1 127.0.0.1"),
                    run_prompt("ping -n 1 127.0.0.1"),
                    run_prompt("ping -n 2 127.0.0.1")],
    },
    "write": {
        "policy": "suggest",
        "prompts": [
            "Use the Write tool to create the file a.txt containing the "
            "single word alpha. Do nothing else.",
            "Use the Write tool to create the file b.txt containing the "
            "single word beta. Do nothing else.",
            "Use the Edit tool to replace alpha with gamma in a.txt. "
            "Read it first if required. Do nothing else.",
        ],
    },
    # Ask pravidlo v projekte: navrhy by mali chybat, posle sa vlastne
    # pravidlo s ruleContent.
    "rule": {
        "policy": "custom",
        "ask": ["Bash(echo:*)"],
        "prompts": [run_prompt("echo hello"),
                    run_prompt("echo hello"),
                    run_prompt("echo other")],
    },
    # Pravidlo bez ruleContent (cely nastroj), bez ask pravidla -- to ma
    # prednost pred allow (vid scenar rule).
    "tool": {
        "policy": "tool",
        "prompts": [run_prompt("ping -n 1 127.0.0.1"),
                    run_prompt("ping -n 2 127.0.0.1"),
                    run_prompt("tracert -h 1 -w 100 127.0.0.1")],
    },
    # Vlastne pravidlo s prefixom "<prve slovo> *".
    "prefix": {
        "policy": "prefix",
        "prompts": [run_prompt("ping -n 1 127.0.0.1"),
                    run_prompt("ping -n 2 127.0.0.1")],
    },
    # Kontrola: bez updatedPermissions sa CLI pyta znova.
    "none": {
        "policy": "plain",
        "prompts": [run_prompt("echo hello > hello.txt"),
                    run_prompt("echo hello > hello.txt")],
    },
    # Navrh CLI bez prepisu destination -- len pre lokalny priecinok,
    # aby bolo vidiet, co by zapisal (localSettings v docasnom priecinku).
    "asis": {
        "policy": "asis",
        "prompts": [run_prompt("echo hello > hello.txt")],
    },
}


def watched(workdir):
    paths = [os.path.join(HOME_CLAUDE, "settings.json"),
             os.path.join(HOME_CLAUDE, "settings.local.json"),
             os.path.join(workdir, ".claude", "settings.json"),
             os.path.join(workdir, ".claude", "settings.local.json")]
    out = {}
    for p in paths:
        try:
            with open(p, "rb") as f:
                data = f.read()
            out[p] = (hashlib.sha256(data).hexdigest(), data)
        except FileNotFoundError:
            out[p] = None
    return out


def to_session(suggestions):
    result = []
    for s in suggestions or []:
        s = dict(s)
        s["destination"] = "session"
        result.append(s)
    return result


def respond(policy, request):
    tool = request.get("tool_name")
    tool_input = request.get("input") or {}
    allow = {"behavior": "allow", "updatedInput": tool_input}
    if policy == "suggest":
        allow["updatedPermissions"] = to_session(
            request.get("permission_suggestions"))
    elif policy == "asis":
        sugg = request.get("permission_suggestions") or []
        for s in sugg:
            if s.get("destination") not in ("session", "localSettings"):
                print("!!! navrh s destination %r -- neposielam asis"
                      % s.get("destination"))
                return {"behavior": "allow", "updatedInput": tool_input}
        allow["updatedPermissions"] = sugg
    elif policy == "custom":
        content = tool_input.get("command", "")
        allow["updatedPermissions"] = [{
            "type": "addRules",
            "rules": [{"toolName": tool, "ruleContent": content}],
            "behavior": "allow", "destination": "session"}]
    elif policy == "prefix":
        content = tool_input.get("command", "").split(" ")[0] + " *"
        allow["updatedPermissions"] = [{
            "type": "addRules",
            "rules": [{"toolName": tool, "ruleContent": content}],
            "behavior": "allow", "destination": "session"}]
    elif policy == "tool":
        allow["updatedPermissions"] = [{
            "type": "addRules", "rules": [{"toolName": tool}],
            "behavior": "allow", "destination": "session"}]
    return allow


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "bash"
    sc = SCENARIOS[name]
    workdir = os.path.join(ROOT, name)
    shutil.rmtree(workdir, ignore_errors=True)
    os.makedirs(workdir)
    if sc.get("ask"):
        os.makedirs(os.path.join(workdir, ".claude"))
        with open(os.path.join(workdir, ".claude", "settings.json"), "w",
                  encoding="utf-8") as f:
            json.dump({"permissions": {"ask": sc["ask"]}}, f)
    before = watched(workdir)

    child = subprocess.Popen(
        ARGS, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", cwd=workdir,
        shell=True)

    log = open(os.path.join(workdir, "stream.jsonl"), "w", encoding="utf-8")
    started = time.time()
    lock = threading.Lock()
    result = threading.Event()
    turn = [0]

    def send(value):
        with lock:
            child.stdin.write(json.dumps(value) + "\n")
            child.stdin.flush()

    last = [time.time()]
    dead = threading.Event()

    def read():
        try:
            read_loop()
        except Exception as e:  # spadnute vlakno = visiaca sonda
            print("!!! citacie vlakno spadlo: %r" % e)
        finally:
            dead.set()
            result.set()

    def read_loop():
        for line in child.stdout:
            last[0] = time.time()
            log.write(line)
            log.flush()
            try:
                record = json.loads(line)
            except ValueError:
                print("NEJSON:", line[:300].rstrip())
                continue
            kind = record.get("type")
            t = time.time() - started
            if kind == "control_request":
                request = record.get("request") or {}
                print("=== %5.1fs tah %d control_request %s"
                      % (t, turn[0], request.get("subtype")))
                print(json.dumps(request, indent=1, ensure_ascii=False))
                if request.get("subtype") == "can_use_tool":
                    resp = respond(sc["policy"], request)
                    print("--- odpoved:")
                    print(json.dumps(resp, indent=1, ensure_ascii=False))
                    send({"type": "control_response",
                          "response": {"subtype": "success",
                                       "request_id": record.get("request_id"),
                                       "response": resp}})
            elif kind == "control_response":
                print("=== %5.1fs control_response" % t)
                print(json.dumps(record, ensure_ascii=False)[:1500])
            elif kind == "assistant":
                for c in record["message"].get("content", []):
                    if c.get("type") == "tool_use":
                        print("=== %5.1fs tah %d tool_use %s %s"
                              % (t, turn[0], c.get("name"),
                                 json.dumps(c.get("input"),
                                            ensure_ascii=False)[:300]))
                    elif c.get("type") == "text":
                        print("=== %5.1fs tah %d text: %s"
                              % (t, turn[0], c.get("text", "")[:300]))
            elif kind == "user":
                for c in (record["message"].get("content") or []):
                    if isinstance(c, dict) and c.get("type") == "tool_result":
                        body = c.get("content")
                        if not isinstance(body, str):
                            body = json.dumps(body, ensure_ascii=False)
                        print("=== %5.1fs tah %d tool_result is_error=%s: %s"
                              % (t, turn[0], c.get("is_error"), body[:300]))
            elif kind == "system" and record.get("subtype") in (
                    "permission_denied", "status"):
                print("=== %5.1fs system %s" % (t, json.dumps(
                    record, ensure_ascii=False)[:600]))
            elif kind == "result":
                print("=== %5.1fs tah %d result %s %s"
                      % (t, turn[0], record.get("subtype"),
                         json.dumps(record.get("permission_denials"),
                                    ensure_ascii=False)[:600]))
                result.set()

    threading.Thread(target=read, daemon=True).start()
    send({"type": "control_request", "request_id": "init-0",
          "request": {"subtype": "initialize", "hooks": {}}})
    for i, prompt in enumerate(sc["prompts"]):
        turn[0] = i + 1
        result.clear()
        send({"type": "user",
              "message": {"role": "user",
                          "content": [{"type": "text", "text": prompt}]}})
        last[0] = time.time()
        while not result.wait(1.0):
            if time.time() - last[0] > IDLE_LIMIT:
                break
        if dead.is_set() or not result.is_set():
            print("!!! tah %d nedobehol (%s)" % (
                i + 1, "citanie skoncilo" if dead.is_set()
                else "%d s bez zaznamu" % IDLE_LIMIT))
            child.kill()
            break
    time.sleep(1.0)
    child.stdin.close()
    try:
        child.wait(30)
    except subprocess.TimeoutExpired:
        child.kill()
    log.close()

    after = watched(workdir)
    print("=== settings")
    for p in before:
        b, a = before[p], after[p]
        same = (b is None and a is None) or (b and a and b[0] == a[0])
        print("%s %s" % ("bez zmeny" if same else "ZMENENE", p))
        if not same and a is not None:
            print(a[1].decode("utf-8", "replace")[:2000])
    print("stderr:", child.stderr.read()[:2000])


if __name__ == "__main__":
    main()
