# Does a slash command leave anything in the live stream that the session
# file on disk carries too, under the same uuid?  (bookmark anchor for a
# prompt that --replay-user-messages does not echo)
#
# probe_local_echo.py <empty-temp-dir>
#
# One process, --replay-user-messages: a plain prompt, /clear, /context, and
# /probe-gamma -- a skill written into <dir>/.claude/skills, so that the
# model runs a command of our own making and nothing else.  Prints every
# user/system record of the stream (uuid, flags, start of the content), then
# the same of every session file of the folder, oldest first.  At each
# result it also lists the slash command records already on disk (is the
# record written before the result?).  Raw stream in
# <dir>/raw.jsonl (outside the repository -- it carries paths of this
# machine).
#
# Exploratory tool, not part of the product -- see CLAUDE.md.

import glob
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
    "--replay-user-messages",
    "--model", "haiku",
]

SKILL = """---
name: probe-gamma
description: Probe skill. Use only when invoked by name.
---

Reply with exactly one word: gamma. Use no tools.
"""

STEPS = ["Reply with exactly one word: alpha. Use no tools.",
         "/clear", "/context", "/probe-gamma"]


def content_head(record):
    message = record.get("message")
    if not isinstance(message, dict):
        return ""
    content = message.get("content")
    if isinstance(content, str):
        return content[:70].replace("\n", " ")
    if isinstance(content, list):
        parts = []
        for block in content:
            if isinstance(block, dict):
                text = block.get("text") or block.get("content") or ""
                if not isinstance(text, str):
                    text = json.dumps(text)[:30]
                parts.append("%s:%s" % (block.get("type"),
                                        text[:40].replace("\n", " ")))
        return " | ".join(parts)
    return ""


def line(record):
    return "%-9s %-16s %-36s replay=%s meta=%s sid=%s  %s" % (
        record.get("type"), record.get("subtype", ""),
        record.get("uuid", ""), record.get("isReplay", ""),
        record.get("isMeta", ""), str(record.get("session_id", ""))[:8],
        content_head(record))


def session_folder(workdir):
    mangled = "".join(c if c.isalnum() else "-" for c in workdir)
    return os.path.join(os.path.expanduser("~"), ".claude", "projects",
                        mangled)


def commands_on_disk(workdir):
    # "<file head>:<command> <uuid head>" for every slash command record.
    found = []
    for path in glob.glob(os.path.join(session_folder(workdir), "*.jsonl")):
        with open(path, encoding="utf-8") as f:
            for text in f:
                try:
                    record = json.loads(text)
                except ValueError:
                    continue
                content = (record.get("message") or {}).get("content")
                if isinstance(content, str) and "<command-name>" in content:
                    name = content.split("<command-name>")[1]
                    name = name.split("</command-name>")[0]
                    found.append("%s:%s %s" % (
                        os.path.basename(path)[:8], name,
                        str(record.get("uuid", ""))[:8]))
    return found


def main():
    workdir = os.path.abspath(sys.argv[1])
    skills = os.path.join(workdir, ".claude", "skills", "probe-gamma")
    os.makedirs(skills, exist_ok=True)
    with open(os.path.join(skills, "SKILL.md"), "w", encoding="utf-8") as f:
        f.write(SKILL)

    child = subprocess.Popen(ARGS, cwd=workdir, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    results = []
    snapshots = []
    records = []
    done = threading.Event()

    def read():
        with open(os.path.join(workdir, "raw.jsonl"), "wb") as raw:
            for data in child.stdout:
                raw.write(data)
                try:
                    record = json.loads(data)
                except ValueError:
                    continue
                if record.get("type") == "control_request":
                    # Nothing here should need a tool; refuse whatever asks.
                    request = record.get("request_id")
                    reply = {"type": "control_response", "response": {
                        "subtype": "success", "request_id": request,
                        "response": {"behavior": "deny",
                                     "message": "probe denies tools"}}}
                    child.stdin.write((json.dumps(reply) + "\n").encode())
                    child.stdin.flush()
                    continue
                records.append(record)
                if record.get("type") == "result":
                    # Read before the step loop sees the result: is the
                    # command's disk record there already?
                    snapshots.append(commands_on_disk(workdir))
                    results.append(record)
        done.set()

    threading.Thread(target=read, daemon=True).start()
    for step in STEPS:
        before = len(results)
        message = {"type": "user", "message": {"role": "user",
                                               "content": step}}
        child.stdin.write((json.dumps(message) + "\n").encode())
        child.stdin.flush()
        deadline = time.time() + 90
        while len(results) == before and time.time() < deadline:
            time.sleep(0.2)
        print("-- sent %r, result %s" % (step, len(results) > before))
        if len(results) > before:
            print("   on disk at its result: %s" % snapshots[before])
    child.stdin.close()
    done.wait(30)

    print("== stream")
    for record in records:
        if record.get("type") in ("user", "system") and \
                record.get("subtype") not in ("status", "hook_started",
                                              "hook_response"):
            print(line(record))

    mangled = "".join(c if c.isalnum() else "-" for c in workdir)
    folder = os.path.join(os.path.expanduser("~"), ".claude", "projects",
                          mangled)
    for path in sorted(glob.glob(os.path.join(folder, "*.jsonl")),
                       key=os.path.getmtime):
        print("== disk", os.path.basename(path))
        with open(path, encoding="utf-8") as f:
            for text in f:
                record = json.loads(text)
                if record.get("type") in ("user", "system"):
                    print(line(record))


if __name__ == "__main__":
    main()
