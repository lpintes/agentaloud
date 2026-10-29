# control_request 'set_model': what the CLI answers, whether the next turn
# (system/init, assistant.message.model) follows it, what happens during a
# running turn, and whether it survives --resume (claude-gui-lkk.86).
#
# probe_set_model.py <empty-dir> <step>... [-- extra CLI args]
#
# Steps, run in order:
#   set:<model>   control_request set_model with "model": <model>
#   set:null      ... with "model": null
#   set:omit      ... without the "model" key
#   prompt        "Reply with the word ok", wait for its result
#   tool:<model>  prompt that runs one Bash command; set_model <model> is
#                 sent right after the assistant record with the tool_use,
#                 so the second API call of the same turn shows whether the
#                 change applies mid-turn.  Waits for the result.
#   init          a second initialize (does its response name the model?)
#   wait:<sec>    just listen
#
# Permissions: can_use_tool -> allow.  Raw stream goes to <dir>/raw.jsonl
# (outside the repo -- it carries paths of this machine).
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

NOISY = ("tools", "skills", "agents", "plugins", "slash_commands",
         "mcp_servers", "commands", "memory_paths", "output_style",
         "available_output_styles", "account", "cwd", "terminal_slash_commands",
         "fast_mode_state", "apiKeySource")


def prune(value):
    if isinstance(value, dict):
        return {k: ("(%s)" % type(v).__name__) if k in NOISY else prune(v)
                for k, v in value.items()}
    if isinstance(value, list):
        return [prune(v) for v in value]
    return value


def user_message(text):
    return {"type": "user", "message": {
        "role": "user", "content": [{"type": "text", "text": text}]}}


def set_model_request(request_id, arg):
    request = {"subtype": "set_model"}
    if arg == "null":
        request["model"] = None
    elif arg != "omit":
        request["model"] = arg
    return {"type": "control_request", "request_id": request_id,
            "request": request}


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
    state = {"results": 0, "session": None, "midset": None}
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
                parts = []
                for b in msg.get("content", []):
                    if b.get("type") == "text":
                        parts.append("text: " + b.get("text", "")[:60])
                    else:
                        parts.append(b.get("type", "?"))
                print("%s assistant model=%s parent=%s [%s]" % (
                    stamp(), msg.get("model"),
                    record.get("parent_tool_use_id"), " | ".join(parts)),
                    flush=True)
                if (state["midset"] and any(
                        b.get("type") == "tool_use"
                        for b in msg.get("content", []))):
                    arg, state["midset"] = state["midset"], None
                    send(set_model_request(next_id("model"), arg),
                         "set_model %s (mid-turn)" % arg)
            elif kind == "control_request":
                req = record["request"]
                print("%s control_request %s" % (
                    stamp(), json.dumps(prune(req))[:300]), flush=True)
                if req.get("subtype") == "can_use_tool":
                    send({"type": "control_response", "response": {
                        "subtype": "success",
                        "request_id": record["request_id"],
                        "response": {"behavior": "allow",
                                     "updatedInput": req.get("input", {})}}})
            elif kind == "control_response":
                print("%s control_response %s" % (
                    stamp(), json.dumps(prune(record))[:3000]), flush=True)
            elif kind == "system" and sub == "init":
                print("%s system/init model=%s permissionMode=%s" % (
                    stamp(), record.get("model"),
                    record.get("permissionMode")), flush=True)
            elif kind == "result":
                state["results"] += 1
                print("%s result/%s is_error=%s modelUsage=%s result=%r" % (
                    stamp(), sub, record.get("is_error"),
                    list((record.get("modelUsage") or {}).keys()),
                    str(record.get("result"))[:80]), flush=True)
            elif kind in ("stream_event", "rate_limit_event"):
                pass
            elif kind == "user":
                print("%s user (%s)" % (stamp(), ",".join(sorted(record))),
                      flush=True)
            else:
                print("%s %s/%s %s" % (stamp(), kind, sub, json.dumps(
                    prune(record))[:400]), flush=True)

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
        if name == "set":
            send(set_model_request(next_id("model"), arg), "set_model " + arg)
            time.sleep(2.0)
        elif name == "prompt":
            before = state["results"]
            send(user_message(PROMPT_OK), "prompt ok")
            wait_result(before)
        elif name == "tool":
            before = state["results"]
            state["midset"] = arg
            send(user_message(PROMPT_TOOL), "prompt tool")
            wait_result(before)
        elif name == "init":
            send({"type": "control_request", "request_id": next_id("init"),
                  "request": {"subtype": "initialize", "hooks": {}}},
                 "initialize")
            time.sleep(2.0)
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
