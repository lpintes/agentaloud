# Ako Claude CLI (headless, stream-json) preposiela MCP elicitaciu svojmu
# stdio klientovi, akej odpovede caka -- a co z nej dostane MCP server.
#
# MCP server je ten isty falosny `fakeel` z probe_elicit_form.py (nastroj
# `collect`, jedno volanie posle za sebou vsetky pripady "CASE <meno>:").
# Server sa da CLI cez --mcp-config + --strict-mcp-config, ~/.claude sa
# nemeni.  Sonda odpovie na can_use_tool pre mcp__fakeel__collect allow a na
# kazdu elicitaciu podla mena pripadu (ANSWERS z probe_elicit_form.py, obal
# prisposobeny CLI: bez _meta, content len ked nie je None).
#
#   probe_claude_elicit.py run     odpovede podla ANSWERS
#   probe_claude_elicit.py error   na kazdu elicitaciu control_response error
#   probe_claude_elicit.py bad     zle odpovede: action mimo enum, content ako
#                                  retazec, prazdna odpoved, bez response
#   probe_claude_elicit.py silent  na elicitaciu sa neodpovie vobec; po 60 s
#                                  sonda zavrie stdin (co spravi CLI?)
#   probe_claude_elicit.py interrupt  neodpovie, po 5 s posle interrupt
#
# tool_progress s heartbeat chodi kazdych 30 s, do limitu bez zaznamu sa
# preto nerata.
#
# POSIELA PROMPT, stoji kredit (haiku, jeden tah).  Pracovny priecinok
# C:/b/claude-elicit-probe/work/<rezim>, vzdy cerstvy.
# Logy: C:/b/claude-elicit-probe/logs/<rezim>.stream.jsonl (stdout CLI),
#       <rezim>.out.txt (to co vypise sonda), <rezim>.debug.log (--debug-file),
#       C:/b/elicit-probe/logs/el-mcp.log (strana MCP servera, pripisuje sa;
#       sonda vypise len cast z tohto behu).
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import shutil
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from probe_elicit_form import ANSWERS, MCP_LOG  # noqa: E402

ROOT = "C:/b/claude-elicit-probe"
LOGDIR = ROOT + "/logs"
IDLE_LIMIT = 90  # sekund bez noveho zaznamu = koniec

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PROMPT = ("Call the tool mcp__fakeel__collect exactly once, with no "
          "arguments, and quote its result verbatim. Do not use any other "
          "tool except ToolSearch if you need it to load that tool.")

# zle odpovede v rezime bad, po rade pre kazdu elicitaciu
BAD = [
    {"subtype": "success", "response": {"action": "maybe"}},
    {"subtype": "success", "response": {"action": "accept",
                                        "content": "not an object"}},
    {"subtype": "success", "response": {}},
    {"subtype": "success"},
]


def case_of(message):
    if message.startswith("CASE "):
        return message[5:].split(":")[0]
    return None


def cli_answer(case):
    a = ANSWERS.get(case)
    if a is None:
        return {"action": "decline"}
    out = {"action": a["action"]}
    if a.get("content") is not None:
        out["content"] = a["content"]
    return out


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "run"
    os.makedirs(LOGDIR, exist_ok=True)
    workdir = os.path.join(ROOT, "work", mode)
    shutil.rmtree(workdir, ignore_errors=True)
    os.makedirs(workdir)
    out = open(f"{LOGDIR}/{mode}.out.txt", "w", encoding="utf-8")

    def p(*a):
        s = " ".join(str(x) for x in a)
        print(s)
        out.write(s + "\n")
        out.flush()

    py = sys.executable.replace("\\", "/")
    me = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                      "probe_elicit_form.py").replace("\\", "/")
    mcp = {"mcpServers": {"fakeel": {"type": "stdio", "command": py,
                                     "args": [me, "mcp", "run"]}}}
    exe = shutil.which("claude")
    args = [exe, "-p", "--verbose",
            "--input-format", "stream-json",
            "--output-format", "stream-json",
            "--permission-prompt-tool", "stdio",
            "--replay-user-messages",
            "--model", "haiku",
            "--mcp-config", json.dumps(mcp), "--strict-mcp-config",
            "--debug-file", f"{LOGDIR}/{mode}.debug.log"]
    mcp_start = os.path.getsize(MCP_LOG) if os.path.exists(MCP_LOG) else 0

    child = subprocess.Popen(
        args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", cwd=workdir)
    log = open(f"{LOGDIR}/{mode}.stream.jsonl", "w", encoding="utf-8")
    started = time.time()
    lock = threading.Lock()
    result = threading.Event()
    dead = threading.Event()
    last = [time.time()]
    bad_i = [0]

    def send(value):
        line = json.dumps(value, ensure_ascii=False)
        p(">>> " + line)
        with lock:
            child.stdin.write(line + "\n")
            child.stdin.flush()

    def on_elicitation(rid, req):
        case = case_of(req.get("message", ""))
        if mode == "silent":
            p("--- (neodpovedam, o 60 s zavriem stdin)")

            def close_later():
                time.sleep(60)
                p("--- zatvaram stdin")
                with lock:
                    child.stdin.close()
            threading.Thread(target=close_later, daemon=True).start()
            return
        if mode == "interrupt":
            p("--- (neodpovedam, o 5 s interrupt)")

            def interrupt_later():
                time.sleep(5)
                send({"type": "control_request", "request_id": "int-1",
                      "request": {"subtype": "interrupt"}})
            threading.Thread(target=interrupt_later, daemon=True).start()
            return
        if mode == "error":
            send({"type": "control_response",
                  "response": {"subtype": "error", "request_id": rid,
                               "error": "probe refuses elicitation"}})
            return
        if mode == "bad":
            body = dict(BAD[bad_i[0] % len(BAD)])
            bad_i[0] += 1
            body["request_id"] = rid
            send({"type": "control_response", "response": body})
            return
        send({"type": "control_response",
              "response": {"subtype": "success", "request_id": rid,
                           "response": cli_answer(case)}})

    def read_loop():
        for line in child.stdout:
            if '"heartbeat":true' not in line:
                last[0] = time.time()
            log.write(line)
            log.flush()
            try:
                record = json.loads(line)
            except ValueError:
                p("NEJSON:", line[:300].rstrip())
                continue
            kind = record.get("type")
            t = "%6.1fs" % (time.time() - started)
            if kind == "control_request":
                req = record.get("request") or {}
                p(f"=== {t} control_request (cely zaznam):")
                p(line.rstrip())
                rid = record.get("request_id")
                sub = req.get("subtype")
                if sub == "can_use_tool":
                    name = req.get("tool_name")
                    if name in ("mcp__fakeel__collect", "ToolSearch"):
                        resp = {"behavior": "allow",
                                "updatedInput": req.get("input") or {}}
                    else:
                        resp = {"behavior": "deny",
                                "message": "probe allows only collect"}
                    send({"type": "control_response",
                          "response": {"subtype": "success",
                                       "request_id": rid, "response": resp}})
                elif sub == "elicitation":
                    on_elicitation(rid, req)
                else:
                    send({"type": "control_response",
                          "response": {"subtype": "error", "request_id": rid,
                                       "error": f"probe: no {sub}"}})
            elif kind == "control_response":
                p(f"=== {t} control_response {line.rstrip()[:1500]}")
            elif kind == "assistant":
                for c in record["message"].get("content", []):
                    if c.get("type") == "tool_use":
                        p(f"=== {t} tool_use {c.get('name')} "
                          f"{json.dumps(c.get('input'))[:300]}")
                    elif c.get("type") == "text":
                        p(f"=== {t} text: {c.get('text', '')[:1500]}")
            elif kind == "user":
                content = record["message"].get("content")
                if isinstance(content, list):
                    for c in content:
                        if isinstance(c, dict) and \
                                c.get("type") == "tool_result":
                            body = c.get("content")
                            if not isinstance(body, str):
                                body = json.dumps(body, ensure_ascii=False)
                            p(f"=== {t} tool_result is_error="
                              f"{c.get('is_error')}: {body[:3000]}")
            elif kind == "system":
                sub = record.get("subtype")
                if sub == "init":
                    p(f"=== {t} system/init mcp_servers="
                      f"{json.dumps(record.get('mcp_servers'))}")
                else:
                    p(f"=== {t} system/{sub} {line.rstrip()[:1500]}")
            elif kind == "result":
                p(f"=== {t} result {record.get('subtype')} "
                  f"{str(record.get('result'))[:500]}")
                result.set()
            elif kind != "stream_event":
                p(f"=== {t} {kind} {line.rstrip()[:600]}")

    def read():
        try:
            read_loop()
        except Exception as e:  # spadnute vlakno = visiaca sonda
            p("!!! citacie vlakno spadlo: %r" % e)
        finally:
            dead.set()
            result.set()

    threading.Thread(target=read, daemon=True).start()
    send({"type": "control_request", "request_id": "init-0",
          "request": {"subtype": "initialize", "hooks": {}}})
    send({"type": "user", "message": {
        "role": "user", "content": [{"type": "text", "text": PROMPT}]}})
    while not result.wait(1.0):
        if time.time() - last[0] > IDLE_LIMIT:
            p(f"!!! {IDLE_LIMIT} s bez zaznamu")
            break
    time.sleep(1.0)
    try:
        child.stdin.close()
        child.wait(30)
    except (subprocess.TimeoutExpired, OSError):
        child.kill()
    log.close()
    p("stderr:", child.stderr.read()[:2000])
    p("--- el-mcp.log z tohto behu ---")
    with open(MCP_LOG, encoding="utf-8") as f:
        f.seek(mcp_start)
        for ln in f.read().splitlines():
            p(ln[:3000])


if __name__ == "__main__":
    main()
