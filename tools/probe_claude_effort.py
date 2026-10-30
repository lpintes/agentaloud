# Reasoning effort of a running Claude session: what apply_flag_settings
# {effortLevel} answers, what get_settings and the handshake say about it,
# which effort the API request really carries, and whether it survives
# --resume (claude-gui-lkk.87).
#
# probe_claude_effort.py <empty-dir> <step>... [-- extra CLI args]
#
# Steps, run in order:
#   effort:<level>  apply_flag_settings {effortLevel: <level>}; "null" clears
#   model:<model>   set_model
#   settings        get_settings, printed whole
#   prompt          "Reply with the word ok", wait for its result
#   tool:<level>    prompt that runs one Bash command; the effort is sent right
#                   after the assistant record with the tool_use.  Waits.
#   wait:<sec>      just listen
#
# The initialize answer's models are printed whole.  Raw stream goes to
# <dir>/raw.jsonl (outside the repo -- it carries paths of this machine).
#
# Research tool, not part of the product -- see CLAUDE.md.

import json
import os
import subprocess
import sys
import threading
import time

ARGS = [
    "claude", "-p", "--verbose",
    "--input-format", "stream-json",
    "--output-format", "stream-json",
    "--permission-prompt-tool", "stdio",
]

PROMPT_OK = "Reply with the word ok"
PROMPT_TOOL = ("Run this exact command with the Bash tool: echo hi  -- then "
               "reply with the word ok")


def user_message(text):
    return {"type": "user", "message": {
        "role": "user", "content": [{"type": "text", "text": text}]}}


def effort_request(request_id, level):
    return {"type": "control_request", "request_id": request_id, "request": {
        "subtype": "apply_flag_settings",
        "settings": {"effortLevel": None if level == "null" else level}}}


def main():
    argv = sys.argv[1:]
    extra = []
    if "--" in argv:
        extra = argv[argv.index("--") + 1:]
        argv = argv[:argv.index("--")]
    workdir, steps = argv[0], argv[1:]

    child = subprocess.Popen(
        ARGS + extra, cwd=workdir, stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        encoding="utf-8", shell=True)
    started = time.time()
    lock = threading.Lock()
    state = {"results": 0, "session": None, "mid": None}
    raw = open(os.path.join(workdir, "raw.jsonl"), "w", encoding="utf-8")
    counter = [0]

    def stamp():
        return "%6.1fs" % (time.time() - started)

    def send(record, label=None):
        with lock:
            child.stdin.write(json.dumps(record) + "\n")
            child.stdin.flush()
        if label:
            print("%s >>> %s" % (stamp(), label), flush=True)

    def next_id(prefix):
        counter[0] += 1
        return "%s-%d" % (prefix, counter[0])

    def read():
        for line in child.stdout:
            raw.write(line)
            raw.flush()
            try:
                record = json.loads(line)
            except ValueError:
                print("%s NONJSON %s" % (stamp(), line[:200].rstrip()))
                continue
            kind = record.get("type")
            sub = record.get("subtype", "")
            if record.get("session_id"):
                state["session"] = record["session_id"]
            if kind == "assistant":
                msg = record.get("message", {})
                print("%s assistant model=%s" % (stamp(), msg.get("model")),
                      flush=True)
                if state["mid"] and any(b.get("type") == "tool_use"
                                        for b in msg.get("content", [])):
                    level, state["mid"] = state["mid"], None
                    send(effort_request(next_id("effort"), level),
                         "effort %s (mid-turn)" % level)
            elif kind == "control_request":
                req = record["request"]
                if req.get("subtype") == "can_use_tool":
                    send({"type": "control_response", "response": {
                        "subtype": "success",
                        "request_id": record["request_id"],
                        "response": {"behavior": "allow",
                                     "updatedInput": req.get("input", {})}}})
            elif kind == "control_response":
                response = record.get("response", {})
                inner = response.get("response") or {}
                if "models" in inner:
                    print("%s initialize models=%s" % (stamp(), json.dumps(
                        inner["models"], indent=1)), flush=True)
                    print("%s initialize keys=%s" % (stamp(), sorted(inner)),
                          flush=True)
                else:
                    print("%s control_response %s" % (
                        stamp(), json.dumps(record)[:6000]), flush=True)
            elif kind == "system" and sub == "init":
                print("%s system/init model=%s keys=%s" % (
                    stamp(), record.get("model"), sorted(record)), flush=True)
            elif kind == "system":
                print("%s system/%s %s" % (
                    stamp(), sub, json.dumps(record)[:500]), flush=True)
            elif kind == "result":
                state["results"] += 1
                print("%s result/%s is_error=%s result=%r" % (
                    stamp(), sub, record.get("is_error"),
                    str(record.get("result"))[:80]), flush=True)
            elif kind == "user":
                print("%s user %s" % (stamp(), json.dumps(
                    record.get("message", {}).get("content"))[:300]),
                    flush=True)

    threading.Thread(target=read, daemon=True).start()
    threading.Thread(
        target=lambda: [print("%s STDERR %s" % (stamp(), l.rstrip()))
                        for l in child.stderr], daemon=True).start()

    send({"type": "control_request", "request_id": next_id("init"),
          "request": {"subtype": "initialize", "hooks": {}}}, "initialize")
    time.sleep(3.0)

    def wait_result(before, limit=120):
        deadline = time.time() + limit
        while state["results"] <= before and time.time() < deadline:
            time.sleep(0.05)
        time.sleep(1.0)

    for step in steps:
        name, _, arg = step.partition(":")
        if name == "effort":
            send(effort_request(next_id("effort"), arg), "effort " + arg)
            time.sleep(2.0)
        elif name == "model":
            send({"type": "control_request", "request_id": next_id("model"),
                  "request": {"subtype": "set_model", "model": arg}},
                 "set_model " + arg)
            time.sleep(2.0)
        elif name == "settings":
            send({"type": "control_request", "request_id": next_id("get"),
                  "request": {"subtype": "get_settings"}}, "get_settings")
            time.sleep(2.0)
        elif name == "prompt":
            before = state["results"]
            send(user_message(PROMPT_OK), "prompt ok")
            wait_result(before)
        elif name == "tool":
            before = state["results"]
            state["mid"] = arg
            send(user_message(PROMPT_TOOL), "prompt tool")
            wait_result(before)
        elif name == "wait":
            time.sleep(float(arg))
        else:
            sys.exit("unknown step " + step)

    print("%s --- closing stdin, results=%d, session=%s" % (
        stamp(), state["results"], state["session"]), flush=True)
    child.stdin.close()
    try:
        child.wait(timeout=20)
    except subprocess.TimeoutExpired:
        child.kill()
    time.sleep(0.5)
    raw.close()


if __name__ == "__main__":
    main()
