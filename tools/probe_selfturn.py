# Samovolny tah: co CLI posle na stdout, ked sa tah zacne bez promptu od
# klienta -- typicky ked dobehne Bash spusteny s run_in_background a CLI
# modelu dorucí <task-notification>.
#
# probe_selfturn.py <prazdny-priecinok> [plain|interrupt|prompt2] [sekundy]
# ('prompt2' = ako plain, a po cakani este jeden prompt od klienta.)
#
# 'plain' po prvom 'result' nic neposle a caka.  'interrupt' posle
# control_request interrupt hned, ako po prvom 'result' pride system/init
# samovolneho tahu.  Povolenia: can_use_tool -> allow.
# Stdin sa zatvara az na konci.  Surovy zaznam ide do <priecinok>/raw.jsonl
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

PROMPT = (
    "Run this exact command with the Bash tool and run_in_background set to "
    "true: ping -n 12 127.0.0.1  -- then end your turn immediately without "
    "waiting for it or checking its output. When you are later notified that "
    "it finished, reply with one short sentence.")


def send(child, record):
    child.stdin.write(json.dumps(record) + "\n")
    child.stdin.flush()


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
        for key in ("isReplay", "isSynthetic", "parent_tool_use_id"):
            if record.get(key):
                out += " %s=%s" % (key, record.get(key))
    elif kind == "assistant":
        blocks = record.get("message", {}).get("content", [])
        out += " [" + ",".join(b.get("type", "?") for b in blocks) + "]"
    elif kind == "control_request":
        req = record.get("request", {})
        out += " " + req.get("subtype", "") + " " + str(req.get("tool_name", ""))
    elif kind == "control_response":
        resp = record.get("response", {})
        out += " " + resp.get("subtype", "") + " id=" + str(
            resp.get("request_id"))
    elif kind == "system" and sub == "status":
        out += " " + json.dumps({k: v for k, v in record.items()
                                 if k not in ("type", "subtype", "uuid",
                                              "session_id")})[:100]
    elif kind == "result":
        out += " is_error=%s" % record.get("is_error")
        if record.get("result"):
            out += " " + str(record.get("result"))[:60].replace("\n", " ")
    return out


def main():
    workdir = sys.argv[1]
    scenario = sys.argv[2] if len(sys.argv) > 2 else "plain"
    wait = float(sys.argv[3]) if len(sys.argv) > 3 else 45.0

    child = subprocess.Popen(
        ARGS, cwd=workdir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True, encoding="utf-8", shell=True)
    started = time.time()
    results = []
    lock = threading.Lock()
    state = {"interrupted": False}
    raw = open(os.path.join(workdir, "raw.jsonl"), "w", encoding="utf-8")

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
            print("%6.1fs %s" % (now, describe(record)), flush=True)
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
            elif kind == "result":
                results.append(now)
            elif (scenario == "interrupt" and results
                  and not state["interrupted"]
                  and kind == "system"
                  and record.get("subtype") == "init"):
                state["interrupted"] = True
                print("%6.1fs >>> interrupt" % now, flush=True)
                with lock:
                    send(child, {"type": "control_request",
                                 "request_id": "int-1",
                                 "request": {"subtype": "interrupt"}})

    threading.Thread(target=read, daemon=True).start()

    with lock:
        send(child, {"type": "control_request", "request_id": "init-1",
                     "request": {"subtype": "initialize", "hooks": {}}})
        send(child, {"type": "user", "message": {
            "role": "user", "content": [{"type": "text", "text": PROMPT}]}})

    deadline = time.time() + 120
    while not results and time.time() < deadline:
        time.sleep(0.2)
    print("%6.1fs --- prvy result, cakam %ss" % (time.time() - started, wait),
          flush=True)
    end = time.time() + wait
    while time.time() < end:
        time.sleep(0.2)
    if scenario == "prompt2":
        # Kontrola: posle system/init aj tah, ktory poslal klient?
        print("%6.1fs >>> druhy prompt" % (time.time() - started), flush=True)
        with lock:
            send(child, {"type": "user", "message": {
                "role": "user",
                "content": [{"type": "text", "text": "Say OK."}]}})
        time.sleep(15)
    print("%6.1fs --- zatvaram stdin" % (time.time() - started), flush=True)
    child.stdin.close()
    try:
        child.wait(timeout=20)
    except subprocess.TimeoutExpired:
        child.kill()
    time.sleep(0.5)
    raw.close()


if __name__ == "__main__":
    main()
