# Ktore startovacie prepinace headless CLI naozaj PRIJME.
#
# Otazka je uzka: CLI sa spusti presne tak, ako ho spusta proto::Session,
# s jednym prepinacom navyse (napriklad '--permission-mode manual' alebo
# '--chrome'), posle sa control_request 'initialize' a pocuva sa.  Bud proces
# hned skonci a povie na stderr preco, alebo odpovie na initialize a v odpovedi
# je 'current_permission_mode'.  Prompt sa neposiela, takze to nestoji kredit
# -- okrem volby '--prompt', ktora posle jeden trivialny ťah.
#
# probe_cli_args.py <cwd> [--prompt TEXT] [--wait S] -- <prepinace pre claude...>
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
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

INITIALIZE = {
    "type": "control_request",
    "request_id": "init-0",
    "request": {"subtype": "initialize", "hooks": {}},
}

NOISY = ("tools", "skills", "agents", "plugins", "slash_commands",
         "mcp_servers", "commands", "output", "stdout", "memory_paths",
         "available_output_styles", "additionalContext", "models", "account")


def prune(value):
    if isinstance(value, dict):
        return {k: ("(%d poloziek)" % len(v)) if k in NOISY and
                isinstance(v, (list, str, dict)) else prune(v)
                for k, v in value.items()}
    return value


def main():
    argv = sys.argv[1:]
    cwd = argv.pop(0)
    prompt = None
    wait = 40.0
    while argv and argv[0] != "--":
        flag = argv.pop(0)
        if flag == "--prompt":
            prompt = argv.pop(0)
        elif flag == "--wait":
            wait = float(argv.pop(0))
    extra = argv[1:] if argv else []

    child = subprocess.Popen(
        ARGS + extra, cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", shell=True)
    print("ARGS:", " ".join(ARGS + extra))

    records = []
    errs = []
    started = time.time()
    got_init = threading.Event()
    got_result = threading.Event()

    def read():
        for line in child.stdout:
            records.append((time.time() - started, line))
            try:
                record = json.loads(line)
            except ValueError:
                continue
            if record.get("type") == "control_response":
                got_init.set()
            if record.get("type") == "result":
                got_result.set()

    def read_err():
        for line in child.stderr:
            errs.append((time.time() - started, line))

    threading.Thread(target=read, daemon=True).start()
    threading.Thread(target=read_err, daemon=True).start()
    try:
        child.stdin.write(json.dumps(INITIALIZE) + "\n")
        child.stdin.flush()
    except OSError as error:
        print("STDIN:", error)

    deadline = started + wait
    while time.time() < deadline and child.poll() is None \
            and not got_init.is_set():
        time.sleep(0.1)

    if prompt is not None and got_init.is_set() and child.poll() is None:
        message = {"type": "user", "message": {
            "role": "user", "content": [{"type": "text", "text": prompt}]}}
        child.stdin.write(json.dumps(message, ensure_ascii=False) + "\n")
        child.stdin.flush()
        deadline = time.time() + 90
        while time.time() < deadline and child.poll() is None \
                and not got_result.is_set():
            time.sleep(0.1)

    time.sleep(1.0)
    code = child.poll()
    if code is None:
        child.kill()
        print("EXIT: (zabity po %.1fs, zil)" % (time.time() - started))
    else:
        print("EXIT: %d po %.1fs" % (code, time.time() - started))

    for when, line in errs:
        print("STDERR %5.1fs: %s" % (when, line.rstrip()))
    for when, line in records:
        try:
            record = json.loads(line)
        except ValueError:
            print("NEJSON %5.1fs: %s" % (when, line[:300].rstrip()))
            continue
        print("=== %5.1fs %s %s" % (when, record.get("type"),
                                    record.get("subtype", "")))
        print(json.dumps(prune(record), indent=1, ensure_ascii=False)[:3000])
    if not records:
        print("(na stdout nic neprislo)")


if __name__ == "__main__":
    main()
