# Co nesie ExitPlanMode po prepracovani planu a ci deny s interrupt ukonci tah
# (claude-gui-lkk.67).
#
# Dve otazky:
#   1. Nesie tool_use v zazname 'assistant' ten isty plan ako input v
#      can_use_tool?  Podozrenie: CLI do can_use_tool dosadi plan zo suboru
#      planFilePath, kym tool_use ostane so starym vstupom modelu, a prepis by
#      potom ukazal iny plan nez dialog (invariant 13).
#   2. Ukonci deny s polom interrupt: true tah hned (type: result bez dalsieho
#      'assistant')?  Podla PermissionResult v SDK by pole malo existovat.
#
# Priebeh: start v plan.  Prvy ExitPlanMode sa zamietne s textom 'pomenuj
# subor farewell.txt', model plan prepracuje; druhy sa zamietne s interrupt.
# Zapis do suboru planu sa povoli (cesta obsahuje 'plans'), vsetko ostatne deny.
#
# POSIELA PROMPT, stoji kredit (haiku, jeden tah).
# Prieskumny nastroj, nie sucast produktu (viz CLAUDE.md).
#
# Pouzitie:
#   python tools/probe_plan_revise.py [sekundy] [model]

import json
import subprocess
import sys
import tempfile
import threading
import time

PROMPT = (
    "Plan creating a file bye.txt containing the word bye. Keep the plan to "
    "two short steps, then call ExitPlanMode right away without exploring."
)
REVISE = "Name the file farewell.txt instead."
STOP = "probe: plan returned, wait for the user."


def send(child, value):
    child.stdin.write(json.dumps(value) + "\n")
    child.stdin.flush()


def short(text, limit=160):
    text = (text or "").replace("\n", "\\n")
    return text if len(text) <= limit else text[:limit] + "..."


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 240.0
    model = sys.argv[2] if len(sys.argv) > 2 else "haiku"
    workdir = tempfile.mkdtemp(prefix="probe_plan_revise_")
    child = subprocess.Popen(
        ["claude", "-p", "--verbose",
         "--input-format", "stream-json", "--output-format", "stream-json",
         "--permission-prompt-tool", "stdio",
         "--model", model, "--permission-mode", "plan"],
        cwd=workdir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", shell=True)
    started = time.time()
    done = threading.Event()
    exits = [0]
    interrupted_at = [None]

    def say(*parts):
        print("%6.1fs" % (time.time() - started), *parts, flush=True)

    def answer(record, body):
        send(child, {"type": "control_response",
                     "response": {"subtype": "success",
                                  "request_id": record.get("request_id"),
                                  "response": body}})

    def read():
        for line in child.stdout:
            try:
                record = json.loads(line)
            except ValueError:
                continue
            kind = record.get("type")
            if kind == "assistant":
                message = record.get("message") or {}
                for part in message.get("content") or []:
                    if part.get("type") == "tool_use":
                        data = part.get("input") or {}
                        say("assistant tool_use", part.get("name"),
                            "keys=%s" % sorted(data.keys()),
                            "plan=%s" % short(data.get("plan")),
                            "file=%s" % data.get("file_path", "-"))
                    elif part.get("type") == "text":
                        say("assistant text", short(part.get("text")))
                if interrupted_at[0] is not None:
                    say("!! assistant AFTER interrupt deny")
            elif kind == "user":
                message = record.get("message") or {}
                for part in message.get("content") or []:
                    if isinstance(part, dict) and part.get("type") == "tool_result":
                        content = part.get("content")
                        if isinstance(content, list):
                            content = " ".join(c.get("text", "") for c in content
                                               if isinstance(c, dict))
                        say("tool_result", "is_error=%s" % part.get("is_error"),
                            short(content, 300))
            elif kind == "control_request":
                request = record.get("request") or {}
                name = request.get("tool_name")
                data = request.get("input") or {}
                if name == "ExitPlanMode":
                    exits[0] += 1
                    say("can_use_tool ExitPlanMode #%d" % exits[0],
                        "keys=%s" % sorted(data.keys()),
                        "plan=%s" % short(data.get("plan")),
                        "planFilePath=%s" % data.get("planFilePath"))
                    path = data.get("planFilePath")
                    if path:
                        try:
                            with open(path, encoding="utf-8") as handle:
                                say("plan file", short(handle.read()))
                        except OSError as error:
                            say("plan file unreadable", error)
                    if exits[0] == 1:
                        answer(record, {"behavior": "deny", "message": REVISE})
                    else:
                        interrupted_at[0] = time.time()
                        answer(record, {"behavior": "deny", "message": STOP,
                                        "interrupt": True})
                        say("-> deny interrupt=true")
                elif "plans" in str(data.get("file_path", "")):
                    say("can_use_tool", name, "-> allow (plan file)")
                    answer(record, {"behavior": "allow", "updatedInput": data})
                else:
                    say("can_use_tool", name, "-> deny", short(json.dumps(data)))
                    answer(record, {"behavior": "deny", "message": "probe: no"})
            elif kind == "result":
                after = ("%.1fs after interrupt deny" %
                         (time.time() - interrupted_at[0])
                         if interrupted_at[0] else "")
                say("result", record.get("subtype"),
                    "is_error=%s" % record.get("is_error"), after,
                    "cost=%s" % record.get("total_cost_usd"))
                done.set()

    threading.Thread(target=read, daemon=True).start()
    send(child, {"type": "control_request", "request_id": "init-1",
                 "request": {"subtype": "initialize", "hooks": {}}})
    send(child, {"type": "user",
                 "message": {"role": "user",
                             "content": [{"type": "text", "text": PROMPT}]}})
    done.wait(seconds)
    child.stdin.close()
    try:
        child.wait(30)
    except subprocess.TimeoutExpired:
        child.kill()
    print("workdir:", workdir)


if __name__ == "__main__":
    main()
