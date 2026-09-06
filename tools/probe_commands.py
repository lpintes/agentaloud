# Co CLI naozaj povie o slash prikazoch a v ktorom zazname.
#
# Otazka je uzka a ma dve polovice:
#
#   1. 'system/init' nesie 'slash_commands' ako HOLE MENA (odmerane na
#      tests/fixtures/basic.jsonl -- 76 retazcov, ziadny popis).  Nesie
#      odpoved na 'initialize' viac, teda popis a argumentHint, ako tvrdi
#      navrh kroku 6?
#   2. Ak ano, ako presne sa to pole vola a co je v jednej polozke?  Od toho
#      zavisi, ci sa da dialog Ctrl+/ postavit s popismi, alebo len s menami.
#
# Ziadny prompt sa neposiela, takze to NESTOJI KREDIT.  Prieskumny nastroj,
# nie sucast produktu -- viz CLAUDE.md.
#
# Pouzitie:
#   python tools/probe_commands.py [sekundy] [dalsie prepinace pre claude...]

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


def describe(name, value, limit=3):
    print("--- %s: %s" % (name, type(value).__name__), end="")
    if isinstance(value, list):
        print(", %d poloziek" % len(value))
        for item in value[:limit]:
            print("    " + json.dumps(item, ensure_ascii=False)[:400])
    elif isinstance(value, dict):
        print(", kluce: %s" % ", ".join(sorted(value)[:20]))
        for key in sorted(value)[:limit]:
            print("    %s: %s" %
                  (key, json.dumps(value[key], ensure_ascii=False)[:400]))
    else:
        print(" = %s" % json.dumps(value, ensure_ascii=False)[:400])


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    child = subprocess.Popen(
        ARGS + sys.argv[2:], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", shell=True)

    lines = []
    started = time.time()

    def read():
        for line in child.stdout:
            lines.append((time.time() - started, line))

    threading.Thread(target=read, daemon=True).start()
    child.stdin.write(json.dumps(INITIALIZE) + "\n")
    child.stdin.flush()
    time.sleep(seconds)
    child.kill()

    # Kazde pole, ktore by mohlo o prikazoch hovorit -- hlada sa podla mena
    # rekurzivne, aby sa neprehliadlo zanorene o uroven nizsie.
    WANTED = ("slash_commands", "terminal_slash_commands", "commands",
              "skills", "supportedDialogKinds", "capabilities")

    def walk(node, path):
        if isinstance(node, dict):
            for key, value in node.items():
                if key in WANTED:
                    describe(path + "." + key if path else key, value)
                walk(value, path + "." + key if path else key)
        elif isinstance(node, list):
            for item in node:
                walk(item, path + "[]")

    for when, line in lines:
        try:
            record = json.loads(line)
        except ValueError:
            print("NEJSON:", line[:200].rstrip())
            continue
        print("=== %5.1fs %s %s" %
              (when, record.get("type"), record.get("subtype", "")))
        walk(record, "")
    if not lines:
        print("(nic neprislo)")


if __name__ == "__main__":
    main()
