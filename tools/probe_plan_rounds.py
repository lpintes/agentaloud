# How a plan returned several times is written down (claude-gui-lkk.68).
#
# A session resumed from disk has neither can_use_tool nor the plan file as it
# was then: the file is one per session and each revision overwrites it.  What
# it does have are the model's Write/Edit calls on that file.  This records,
# round by round, which plan can_use_tool carried, which plan the tool_use
# carried, and how the plan file was written (Write with the whole text, or
# Edit with a change), so the transcript can be rebuilt from those calls.
#
# Rounds: the plan is returned the way the app returns it (deny with
# interrupt: true, the turn ends), then the next prompt asks for a change.
# The last round approves.  Writes to the plan file are allowed (path contains
# 'plans'), everything else is denied.
#
# SENDS PROMPTS, costs credit (haiku, one turn per round).
# A research tool, not part of the product (see CLAUDE.md).
#
# Usage:
#   python tools/probe_plan_rounds.py [model]

import json
import subprocess
import sys
import tempfile
import threading
import time
import uuid

PROMPT = (
    "Plan creating a file bye.txt containing the word bye. Keep the plan to "
    "two short steps, then call ExitPlanMode right away without exploring."
)
# Small changes on purpose: a small change is where Edit is likelier than a
# rewrite with Write.
REVISIONS = [
    "Name the file farewell.txt instead.",
    "Only change step 2: verify the file with the Read tool.",
    "Make the content 'bye!' with an exclamation mark.",
]
STOP = "probe: plan returned, wait for the user."


def send(child, value):
    child.stdin.write(json.dumps(value) + "\n")
    child.stdin.flush()


def short(text, limit=200):
    text = (text or "").replace("\n", "\\n")
    return text if len(text) <= limit else text[:limit] + "..."


def prompt(child, text):
    send(child, {"type": "user",
                 "message": {"role": "user",
                             "content": [{"type": "text", "text": text}]}})


def main():
    model = sys.argv[1] if len(sys.argv) > 1 else "haiku"
    workdir = tempfile.mkdtemp(prefix="probe_plan_rounds_")
    session = str(uuid.uuid4())
    child = subprocess.Popen(
        ["claude", "-p", "--verbose",
         "--input-format", "stream-json", "--output-format", "stream-json",
         "--permission-prompt-tool", "stdio", "--session-id", session,
         "--model", model, "--permission-mode", "plan"],
        cwd=workdir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", shell=True)
    started = time.time()
    turn_over = threading.Event()
    exits = [0]

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
                for part in (record.get("message") or {}).get("content") or []:
                    if part.get("type") != "tool_use":
                        continue
                    data = part.get("input") or {}
                    name = part.get("name")
                    if name == "ExitPlanMode":
                        say("tool_use ExitPlanMode", part.get("id"),
                            "plan=%s" % short(data.get("plan")))
                    elif name in ("Write", "Edit", "MultiEdit"):
                        say("tool_use", name, part.get("id"),
                            json.dumps(data)[:400])
                    else:
                        say("tool_use", name)
            elif kind == "user":
                for part in (record.get("message") or {}).get("content") or []:
                    if isinstance(part, dict) and part.get("type") == "tool_result":
                        say("tool_result", part.get("tool_use_id"),
                            "is_error=%s" % part.get("is_error"),
                            "tool_use_result=%s" %
                            short(json.dumps(record.get("tool_use_result")), 200))
            elif kind == "control_request":
                request = record.get("request") or {}
                name = request.get("tool_name")
                data = request.get("input") or {}
                if name == "ExitPlanMode":
                    exits[0] += 1
                    say("can_use_tool ExitPlanMode #%d" % exits[0],
                        "plan=%s" % short(data.get("plan")))
                    if exits[0] <= len(REVISIONS):
                        answer(record, {"behavior": "deny", "message": STOP,
                                        "interrupt": True})
                        say("-> returned")
                    else:
                        answer(record, {"behavior": "allow",
                                        "updatedInput": data})
                        say("-> approved")
                elif "plans" in str(data.get("file_path", "")):
                    answer(record, {"behavior": "allow", "updatedInput": data})
                else:
                    answer(record, {"behavior": "deny", "message": "probe: no",
                                    "interrupt": True})
                    say("can_use_tool", name, "-> deny")
            elif kind == "result":
                say("result", record.get("subtype"),
                    "cost=%s" % record.get("total_cost_usd"))
                turn_over.set()

    threading.Thread(target=read, daemon=True).start()
    send(child, {"type": "control_request", "request_id": "init-1",
                 "request": {"subtype": "initialize", "hooks": {}}})
    for text in [PROMPT] + REVISIONS:
        turn_over.clear()
        say("prompt", text)
        prompt(child, text)
        if not turn_over.wait(240):
            say("!! turn did not end")
            break
    child.stdin.close()
    try:
        child.wait(30)
    except subprocess.TimeoutExpired:
        child.kill()
    print("workdir:", workdir)
    print("session:", session)


if __name__ == "__main__":
    main()
