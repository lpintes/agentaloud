# Prompt pocas tahu: co CLI spravi, ked klient zapise druhy 'user' zaznam
# (ten isty tvar ako Session::SendPrompt), kym prvy tah este bezi.
#
# probe_midturn.py <prazdny-priecinok> mid|mid-text|interrupt
#
# 'mid'       prompt 1 spusti pomaly Bash v popredi; prompt 2 ide ~2 s po
#             tool_use.
# 'mid-text'  prompt 1 chce dlhsi text bez nastroja; prompt 2 ide ~1 s po
#             system/init.
# 'interrupt' ako 'mid', a ~1 s po prompte 2 control_request interrupt
#             (tvar ako MakeInterrupt).
#
# Povolenia: can_use_tool -> allow.  Stdin sa zatvara az na konci, ked
# 25 s nepride ziadny zaznam.  Surovy zaznam ide do <priecinok>/raw.jsonl
# (mimo repozitara -- nesie cesty tohto stroja).
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

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
    "--model", "haiku",
]

PROMPT_TOOL = (
    "Run this exact command with the Bash tool in the foreground (not in the "
    "background): ping -n 8 127.0.0.1  -- then reply with one short sentence "
    "saying how many replies came back.")

PROMPT_TEXT = (
    "Write an essay of about 700 words on the history of the bicycle. Do not "
    "use any tools.")

PROMPT_2 = "Reply with the word BANANA"
PROMPT_3 = "Also reply with the word CHERRY"


def send(child, record):
    child.stdin.write(json.dumps(record) + "\n")
    child.stdin.flush()


def user_message(text):
    return {"type": "user", "message": {
        "role": "user", "content": [{"type": "text", "text": text}]}}


def describe(record):
    kind = record.get("type")
    sub = record.get("subtype", "")
    out = "%s/%s" % (kind, sub) if sub else str(kind)
    if kind == "user":
        content = record.get("message", {}).get("content")
        if isinstance(content, str):
            out += " text: " + content[:80].replace("\n", " ")
        elif isinstance(content, list):
            parts = []
            for part in content:
                t = part.get("type")
                if t == "text":
                    parts.append("text: " + part.get("text", "")[:80]
                                 .replace("\n", " "))
                elif t == "tool_result":
                    c = part.get("content")
                    if isinstance(c, list):
                        c = " ".join(x.get("text", "") for x in c
                                     if isinstance(x, dict))
                    parts.append("tool_result: " + str(c)[:60]
                                 .replace("\n", " "))
                else:
                    parts.append(str(t))
            out += " [" + " | ".join(parts) + "]"
        out += " keys=" + ",".join(sorted(record.keys()))
        flags = {k: v for k, v in record.items()
                 if k not in ("type", "message", "session_id", "uuid",
                              "timestamp", "parent_tool_use_id",
                              "tool_use_result")}
        if record.get("parent_tool_use_id"):
            flags["parent_tool_use_id"] = record["parent_tool_use_id"]
        if flags:
            out += " flags=" + json.dumps(flags)[:120]
    elif kind == "assistant":
        blocks = record.get("message", {}).get("content", [])
        kinds = []
        for b in blocks:
            if b.get("type") == "text":
                text = b.get("text", "")
                kinds.append("text(%d%s): %s" % (
                    len(text), ",BANANA" if "BANANA" in text.upper() else "",
                    text[:50].replace("\n", " ")))
            else:
                kinds.append(b.get("type", "?"))
        out += " [" + " | ".join(kinds) + "]"
    elif kind == "control_request":
        req = record.get("request", {})
        out += " " + req.get("subtype", "") + " " + str(req.get("tool_name", ""))
    elif kind == "control_response":
        resp = record.get("response", {})
        out += " " + resp.get("subtype", "") + " id=" + str(
            resp.get("request_id"))
    elif kind == "system" and sub not in ("init", "thinking_tokens"):
        out += " " + json.dumps({k: v for k, v in record.items()
                                 if k not in ("type", "subtype", "uuid",
                                              "session_id")})[:160]
    elif kind == "result":
        out += " is_error=%s turns=%s" % (record.get("is_error"),
                                         record.get("num_turns"))
        if record.get("result"):
            text = str(record.get("result"))
            out += " (%d%s) %s" % (len(text),
                                   ",BANANA" if "BANANA" in text.upper()
                                   else "",
                                   text[:60].replace("\n", " "))
    elif kind not in ("rate_limit_event", "stream_event"):
        out += " " + json.dumps(record)[:160]
    return out


def main():
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    positional = [a for a in sys.argv[1:] if not a.startswith("--")]
    workdir = positional[0]
    scenario = positional[1] if len(positional) > 1 else "mid"
    if scenario not in ("mid", "mid-text", "interrupt"):
        sys.exit("scenar: mid | mid-text | interrupt")
    args = list(ARGS)
    if "--replay" in flags:
        args.append("--replay-user-messages")
    # '--two': hned za promptom 2 este prompt 3.
    two = "--two" in flags

    child = subprocess.Popen(
        args, cwd=workdir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True, encoding="utf-8", shell=True)
    started = time.time()
    lock = threading.Lock()
    state = {"last": time.time(), "trigger": None, "sent2": None,
             "results": 0, "session": None}
    raw = open(os.path.join(workdir, "raw.jsonl"), "w", encoding="utf-8")

    def read():
        for line in child.stdout:
            now = time.time() - started
            state["last"] = time.time()
            raw.write(line)
            raw.flush()
            try:
                record = json.loads(line)
            except ValueError:
                print("%6.1fs NEJSON %s" % (now, line[:100].rstrip()))
                continue
            print("%6.1fs %s" % (now, describe(record)), flush=True)
            kind = record.get("type")
            if record.get("session_id"):
                state["session"] = record["session_id"]
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
            elif kind == "result":
                state["results"] += 1
            elif state["trigger"] is None:
                if (scenario in ("mid", "interrupt") and kind == "assistant"
                        and any(b.get("type") == "tool_use" for b in
                                record["message"].get("content", []))):
                    state["trigger"] = time.time() + 2.0
                elif (scenario == "mid-text" and kind == "system"
                      and record.get("subtype") == "init"):
                    state["trigger"] = time.time() + 1.0

    threading.Thread(target=read, daemon=True).start()

    with lock:
        send(child, {"type": "control_request", "request_id": "init-1",
                     "request": {"subtype": "initialize", "hooks": {}}})
        send(child, user_message(
            PROMPT_TEXT if scenario == "mid-text" else PROMPT_TOOL))

    interrupted = False
    deadline = time.time() + 240
    while time.time() < deadline:
        time.sleep(0.05)
        now = time.time()
        if (state["trigger"] is not None and state["sent2"] is None
                and now >= state["trigger"]):
            state["sent2"] = now
            print("%6.1fs >>> prompt 2 (results=%d)" % (
                now - started, state["results"]), flush=True)
            with lock:
                send(child, user_message(PROMPT_2))
            if two:
                time.sleep(0.5)
                print("%6.1fs >>> prompt 3" % (time.time() - started),
                      flush=True)
                with lock:
                    send(child, user_message(PROMPT_3))
        if (scenario == "interrupt" and not interrupted
                and state["sent2"] is not None
                and now >= state["sent2"] + 1.0):
            interrupted = True
            print("%6.1fs >>> interrupt (results=%d)" % (
                now - started, state["results"]), flush=True)
            with lock:
                send(child, {"type": "control_request",
                             "request_id": "stop-1",
                             "request": {"subtype": "interrupt",
                                         "reason": "interrupt"}})
        if (state["sent2"] is not None and state["results"] > 0
                and now - state["last"] > 25):
            break
    print("%6.1fs --- zatvaram stdin, results=%d, session=%s" % (
        time.time() - started, state["results"], state["session"]),
        flush=True)
    child.stdin.close()
    try:
        child.wait(timeout=20)
    except subprocess.TimeoutExpired:
        child.kill()
    time.sleep(0.5)
    raw.close()


if __name__ == "__main__":
    main()
