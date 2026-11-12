# Are record identities stable between the live stream and the session file
# on disk that --resume replays?  (uuid as a persistent bookmark anchor)
#
# probe_anchor_claude.py <empty-temp-dir>
#
# Phase 1: --session-id <new>, --replay-user-messages.  Prompt 1 says a
#          sentence and runs a slow Bash command that needs permission
#          (can_use_tool -> allow); prompt 2 goes ~2 s after the tool_use
#          (mid-turn).  Then the disk file is compared with the stream.
# Phase 2: --resume <id> with one more short prompt; the disk file is
#          compared with its phase-1 copy.
#
# Raw streams go to <dir>/raw1.jsonl and raw2.jsonl, disk copies to
# disk1.jsonl and disk2.jsonl (outside the repository -- they carry paths
# of this machine).
#
# Exploratory tool, not part of the product -- see CLAUDE.md.

import glob
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import uuid as uuidlib

ARGS = [
    "claude", "-p", "--verbose",
    "--input-format", "stream-json",
    "--output-format", "stream-json",
    "--permission-prompt-tool", "stdio",
    "--replay-user-messages",
    "--model", "haiku",
]

PROMPT_1 = (
    "First write one short sentence saying you will run a command. Then run "
    "this exact command with the Bash tool in the foreground (not in the "
    "background): ping -n 8 127.0.0.1 > out.txt  -- then reply with one "
    "short sentence saying it is done.")
PROMPT_2 = "Reply with the word BANANA"
PROMPT_3 = "Reply with the word CHERRY"


def send(child, lock, record):
    with lock:
        child.stdin.write(json.dumps(record) + "\n")
        child.stdin.flush()


def user_message(text):
    return {"type": "user", "message": {
        "role": "user", "content": [{"type": "text", "text": text}]}}


def short(record):
    kind = record.get("type")
    sub = record.get("subtype", "")
    out = kind + ("/" + sub if sub else "")
    msg = record.get("message")
    if isinstance(msg, dict):
        content = msg.get("content")
        if isinstance(content, str):
            out += " str:" + content[:40]
        elif isinstance(content, list):
            parts = []
            for b in content:
                t = b.get("type")
                if t == "text":
                    parts.append("text:" + b.get("text", "")[:40]
                                 .replace("\n", " "))
                else:
                    parts.append(t)
            out += " [" + " | ".join(parts) + "]"
    return out


def run(workdir, extra, prompt, mid_prompt, raw_name):
    child = subprocess.Popen(
        ARGS + extra, cwd=workdir, stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
        encoding="utf-8", shell=True)
    lock = threading.Lock()
    started = time.time()
    state = {"last": time.time(), "trigger": None, "sent2": None,
             "results": 0}
    records = []
    raw = open(os.path.join(workdir, raw_name), "w", encoding="utf-8")

    def read():
        for line in child.stdout:
            state["last"] = time.time()
            raw.write(line)
            raw.flush()
            try:
                record = json.loads(line)
            except ValueError:
                continue
            records.append(record)
            kind = record.get("type")
            if kind not in ("rate_limit_event", "stream_event"):
                print("%6.1fs %s uuid=%s" % (time.time() - started,
                                             short(record),
                                             record.get("uuid")), flush=True)
            if kind == "control_request":
                req = record["request"]
                if req.get("subtype") == "can_use_tool":
                    send(child, lock, {
                        "type": "control_response",
                        "response": {
                            "subtype": "success",
                            "request_id": record["request_id"],
                            "response": {
                                "behavior": "allow",
                                "updatedInput": req.get("input", {})}}})
            elif kind == "result":
                state["results"] += 1
            elif (mid_prompt and state["trigger"] is None
                  and kind == "assistant"
                  and any(b.get("type") == "tool_use" for b in
                          record["message"].get("content", []))):
                state["trigger"] = time.time() + 2.0

    threading.Thread(target=read, daemon=True).start()
    send(child, lock, {"type": "control_request", "request_id": "init-1",
                       "request": {"subtype": "initialize", "hooks": {}}})
    send(child, lock, user_message(prompt))

    deadline = time.time() + 240
    while time.time() < deadline:
        time.sleep(0.05)
        now = time.time()
        if (state["trigger"] is not None and state["sent2"] is None
                and now >= state["trigger"]):
            state["sent2"] = now
            print("%6.1fs >>> mid-turn prompt" % (now - started), flush=True)
            send(child, lock, user_message(mid_prompt))
        waiting = mid_prompt and state["sent2"] is None
        if not waiting and state["results"] > 0 and now - state["last"] > 20:
            break
    child.stdin.close()
    try:
        child.wait(timeout=20)
    except subprocess.TimeoutExpired:
        child.kill()
    time.sleep(1.0)
    raw.close()
    return records


def disk_file(session_id):
    hits = glob.glob(os.path.join(os.path.expanduser("~"), ".claude",
                                  "projects", "*", session_id + ".jsonl"))
    if len(hits) != 1:
        sys.exit("disk file not found uniquely: %r" % hits)
    return hits[0]


def load(path):
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                out.append(json.loads(line))
    return out


def compare_stream_disk(stream, disk):
    by_uuid = {}
    for i, r in enumerate(disk):
        if r.get("uuid"):
            by_uuid.setdefault(r["uuid"], []).append((i, r))
    print("\n=== disk records ===")
    for i, r in enumerate(disk):
        extra = ""
        if r.get("type") == "attachment":
            att = r.get("attachment", {})
            extra = " att=" + str(att.get("type"))
            if att.get("type") == "queued_command":
                extra += " " + json.dumps({k: v for k, v in att.items()
                                           if k != "prompt"})[:200]
                extra += " prompt=" + json.dumps(att.get("prompt"))[:80]
        elif r.get("type") == "queue-operation":
            extra = " " + json.dumps({k: v for k, v in r.items()
                                      if k not in ("type", "sessionId")})[:200]
        print("%3d %s uuid=%s parent=%s msgid=%s%s" % (
            i, short(r), r.get("uuid"), r.get("parentUuid"),
            (r.get("message") or {}).get("id")
            if isinstance(r.get("message"), dict) else None, extra))

    print("\n=== stream assistant/user vs disk ===")
    for r in stream:
        kind = r.get("type")
        if kind not in ("assistant", "user"):
            continue
        u = r.get("uuid")
        hits = by_uuid.get(u, [])
        line = "%s uuid=%s replay=%s" % (short(r), u, r.get("isReplay"))
        if not hits:
            print(line + "  -> NO DISK RECORD WITH THIS UUID")
            # try by message.id / content
            mid = r["message"].get("id")
            if mid:
                same = [i for i, d in enumerate(disk)
                        if isinstance(d.get("message"), dict)
                        and d["message"].get("id") == mid]
                print("     disk records with same message.id: %r" % same)
            content = r["message"].get("content")
            same = [i for i, d in enumerate(disk)
                    if isinstance(d.get("message"), dict)
                    and d["message"].get("content") == content]
            print("     disk records with equal content: %r" % same)
            continue
        if len(hits) > 1:
            line += "  (%d disk records share it)" % len(hits)
        i, d = hits[0]
        sm, dm = r["message"], d.get("message", {})
        checks = {
            "type": d.get("type") == kind,
            "message.id": sm.get("id") == dm.get("id"),
            "content==": sm.get("content") == dm.get("content"),
            "parentUuid?": "parentUuid" in r,
        }
        if not checks["content=="]:
            sc, dc = sm.get("content"), dm.get("content")
            checks["content_shape"] = "%s vs %s" % (
                [b.get("type") for b in sc] if isinstance(sc, list) else
                "str", [b.get("type") for b in dc] if isinstance(dc, list)
                else ("str:" + str(dc)[:40]))
        print(line + "  -> disk #%d %s" % (i, checks))


def main():
    workdir = os.path.abspath(sys.argv[1])
    session_id = str(uuidlib.uuid4())
    print("session", session_id)
    s1 = run(workdir, ["--session-id", session_id], PROMPT_1, PROMPT_2,
             "raw1.jsonl")
    path = disk_file(session_id)
    shutil.copy(path, os.path.join(workdir, "disk1.jsonl"))
    d1 = load(path)
    compare_stream_disk(s1, d1)

    print("\n\n######## phase 2: --resume ########")
    s2 = run(workdir, ["--resume", session_id], PROMPT_3, None, "raw2.jsonl")
    shutil.copy(path, os.path.join(workdir, "disk2.jsonl"))
    d2 = load(path)
    compare_stream_disk(s2, d2)

    print("\n=== phase-1 disk vs phase-2 disk ===")
    print("lines: %d -> %d" % (len(d1), len(d2)))
    prefix = d2[:len(d1)] == d1
    print("phase-1 file is an unchanged prefix:", prefix)
    if not prefix:
        for i, (a, b) in enumerate(zip(d1, d2)):
            if a != b:
                print("  first difference at line", i)
                print("   before:", json.dumps(a)[:200])
                print("   after: ", json.dumps(b)[:200])
                break
    u1 = {r["uuid"]: r for r in d1 if r.get("uuid")}
    u2 = {}
    for r in d2:
        if r.get("uuid"):
            u2.setdefault(r["uuid"], []).append(r)
    missing = [u for u in u1 if u not in u2]
    changed = [u for u in u1 if u in u2 and u2[u][0] != u1[u]]
    dup = [u for u, v in u2.items() if len(v) > 1]
    print("phase-1 uuids missing after resume:", missing)
    print("phase-1 uuids whose record changed:", changed)
    print("uuids appearing more than once after resume:", dup)
    s1u = {r.get("uuid") for r in s1 if r.get("type") in ("assistant", "user")}
    s2u = {r.get("uuid") for r in s2 if r.get("type") in ("assistant", "user")}
    print("phase-2 stream reuses phase-1 uuids:", sorted(s1u & s2u))


if __name__ == "__main__":
    main()
