# Tvar odpovede na 'set_permission_mode' -- co presne CLI ozve, ked sa mu za
# behu prepne rezim povoleni (Shift+Tab v termináli, claude-gui-lkk.6.1).
#
# Otazka je uzka: je mod v odpovedi pod dvojitym 'response' ako pri initialize,
# alebo len pod jednym?  A usadi sa zmena naozaj -- druhy 'initialize' po nej
# to ma potvrdit v 'current_permission_mode'.  Odmerane, nie hadane; ziadny
# prompt sa neposiela, takze to nestoji kredit.
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


def rq(request_id, request):
    return json.dumps(
        {"type": "control_request", "request_id": request_id,
         "request": request}) + "\n"


def main():
    # probe_mode.py [mod] [sekundy]
    mode = sys.argv[1] if len(sys.argv) > 1 else "acceptEdits"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

    child = subprocess.Popen(
        ARGS, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", shell=True)

    lines = []
    started = time.time()

    def read():
        for line in child.stdout:
            lines.append((time.time() - started, line))

    threading.Thread(target=read, daemon=True).start()

    child.stdin.write(rq("init-1", {"subtype": "initialize", "hooks": {}}))
    child.stdin.flush()
    time.sleep(seconds / 2)
    child.stdin.write(rq("mode-1",
                         {"subtype": "set_permission_mode", "mode": mode}))
    child.stdin.flush()
    time.sleep(2.0)
    child.stdin.write(rq("init-2", {"subtype": "initialize", "hooks": {}}))
    child.stdin.flush()
    time.sleep(max(2.0, seconds / 2))
    child.kill()

    for when, line in lines:
        try:
            record = json.loads(line)
        except ValueError:
            print("NEJSON:", line[:200].rstrip())
            continue
        if record.get("type") != "control_response":
            continue
        print("=== %5.1fs" % when)
        print(json.dumps(record, indent=1, ensure_ascii=False)[:2000])
    if not lines:
        print("(nic neprislo)")


if __name__ == "__main__":
    main()
