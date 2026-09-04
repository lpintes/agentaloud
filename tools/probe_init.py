# Co CLI povie SAMO OD SEBA, este pred prvym promptom.
#
# Otazka je uzka: vie sa model (a rezim povoleni) uz pri starte, alebo az
# z 'system/init', ktory chodi na zaciatku tahu?  Odpoved sa neda precitat
# z dokumentacie a hadat sa neda vobec, tak sa CLI spusti presne tak, ako ho
# spusta proto::Session, posle sa control_request 'initialize' a niekolko
# sekund sa len pocuva.  Ziadny prompt sa neposiela, takze to nestoji kredit.
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


# Dlhe zoznamy zavadzaju od otazky, ktora sa pyta na jedno pole.  Nahradzuju
# sa poctom, nech je vidiet, ze tam boli.
NOISY = ("tools", "skills", "agents", "plugins", "slash_commands",
         "mcp_servers", "commands", "output", "stdout", "memory_paths",
         "available_output_styles", "additionalContext")


def prune(value):
    if isinstance(value, dict):
        return {k: ("(%d poloziek)" % len(v)) if k in NOISY and
                isinstance(v, (list, str)) else prune(v)
                for k, v in value.items()}
    return value


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    child = subprocess.Popen(
        ARGS, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", shell=True)

    lines = []

    def read():
        for line in child.stdout:
            lines.append(line)

    threading.Thread(target=read, daemon=True).start()
    child.stdin.write(json.dumps(INITIALIZE) + "\n")
    child.stdin.flush()
    time.sleep(seconds)
    child.kill()

    for line in lines:
        try:
            record = json.loads(line)
        except ValueError:
            print("NEJSON:", line[:200].rstrip())
            continue
        kind = record.get("type")
        subtype = record.get("subtype", "")
        print("=== %s %s" % (kind, subtype))
        print(json.dumps(prune(record), indent=1, ensure_ascii=False)[:4000])
    if not lines:
        print("(nic neprislo)")


if __name__ == "__main__":
    main()
