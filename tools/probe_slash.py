# Da sa slash prikaz poslat headless session ako obycajny text promptu?
#
# Cely dialog Ctrl+/ (claude-gui-lkk.6) stoji na tom, ze prikaz vlozeny do
# promptu CLI naozaj VYKONA -- nie ze ho posle modelu ako vetu zacinajucu
# lomkou.  Z dokumentacie sa to precitat neda a hadat sa to neda vobec, tak sa
# posle jeden a pozrie sa, co pride spat:
#
#   1. Vykona sa prikaz, alebo to skonci ako obycajny prompt?
#   2. Ako vyzeraju zaznamy, ktore z toho vzniknu?  Invariant 15 hovori, ze
#      vypis slash prikazu sa v 'user' zazname pozna podla znacky na zaciatku
#      -- tu sa da odmerat, ako ta znacka vyzera na STREAME.
#   3. Skonci to normalnym 'result', teda vie appka poznat koniec tahu rovnako
#      ako pri kazdom inom?
#
# Prikaz sa vybera na prikazovom riadku; '/context' je lokalny, takze to
# nestoji tah modelu.  S niecim, co model rozbehne, to kredit stat bude.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.
#
# Pouzitie:
#   python tools/probe_slash.py [prikaz] [sekundy]

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


def prompt(text):
    return {"type": "user",
            "message": {"role": "user",
                        "content": [{"type": "text", "text": text}]}}


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else "/context"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 45.0

    child = subprocess.Popen(
        ARGS, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", shell=True)

    lines = []
    started = time.time()

    def read():
        for line in child.stdout:
            lines.append((time.time() - started, line))

    threading.Thread(target=read, daemon=True).start()
    child.stdin.write(json.dumps(prompt(command)) + "\n")
    child.stdin.flush()

    # Konci sa hned po 'result', nech sa neceka zbytocne -- ale najviac tolko,
    # kolko sa povedalo.
    while time.time() - started < seconds:
        if any('"type":"result"' in line or '"type": "result"' in line
               for _, line in lines):
            time.sleep(1.0)
            break
        time.sleep(0.5)
    child.kill()

    # Konzola je na Windows cp1250 a zaznam nesie sipky aj em dashe -- print by
    # spadol na UnicodeEncodeError a odmerany vysledok by sa stratil.  Preto sa
    # pise do suboru v UTF-8 a na obrazovku ide len zhrnutie.
    out = open("probe_slash.out", "w", encoding="utf-8")
    for when, line in lines:
        try:
            record = json.loads(line)
        except ValueError:
            out.write("NEJSON: %s\n" % line[:200].rstrip())
            continue
        head = "=== %5.1fs %s %s" % (when, record.get("type"),
                                     record.get("subtype", ""))
        out.write(head + "\n")
        out.write(json.dumps(record, ensure_ascii=False, indent=1)[:6000] +
                  "\n")
        print(head.encode("ascii", "replace").decode("ascii"))
    out.close()
    print("cele zaznamy v probe_slash.out")
    if not lines:
        print("(nic neprislo)")


if __name__ == "__main__":
    main()
