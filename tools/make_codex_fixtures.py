"""Vyrobi fixtury Codexu (tests/fixtures/codex-*.jsonl) zo zaznamov sond.

Zdroj nie je novy beh CLI, ale logy, ktore zapisali sondy
tools/probe_codex_*.py (claude-gui-lkk.44.1) -- riadok '<' je to, co poslal
`codex app-server`, riadok '>' to, co sme poslali my.  Do fixtury ide len '<':
fixtura je stream servera, presne to, co adapter cita.  Nase odpovede
(schvalenie, odpoved na otazku) v nej nie su a nemaju byt -- tie pise adapter,
nie testy.

Preco nie novy beh: kazdy tah minie kredit na OpenAI (free plan) a logy uz
nesu vsetko, co adapter potrebuje -- povolenie aj zamietnutie, upravu suboru,
otazku v rezime plan, prerusenie, premyslanie aj obnovenie threadu.  Ked sa
zmeni format Codexu, pustia sa sondy znova a potom tento skript.

Kazdy log je jeden proces a jeden thread, a tak aj jedna fixtura.  Spajat ich
by nedavalo nic navyse a zviazalo by to fixtury, ktore sa daju menit osobitne
(rovnaky dovod ako vlastny Scrubber pre kazdu v make_fixtures.py).

Do fixtury nesmie vojst to, co Codex poskladal z tohto stroja: meno uctu
v cestach, meno pocitaca a id instalacie (remoteControl/status/changed).
Identifikatory threadov a poloziek su UUID, ktore Codex vyraba nahodne, a nic
neprezradzaju -- zostavaju, aby vazba itemId -> item prezila bez prepisovania.

Beh:  python tools/make_codex_fixtures.py [--logs C:/b/codex-probe/logs]
"""
import argparse
import json
import os
import re
import sys

# log -> meno fixtury.  Vyber je taky, aby prisiel kazdy tvar, ktory adapter
# rozoznava; zvysne logy (basic, noanswer, stdin, noturn, resume-basic) su
# podmnoziny tychto.
LOGS = {
    "multi": "codex-multi",          # tri prikazy, schvalene aj zamietnute
    "edit": "codex-edit",            # fileChange a jeho povolenie bez diffu
    "ask": "codex-ask",              # item/tool/requestUserInput v rezime plan
    "interrupt": "codex-interrupt",  # turn/interrupt pocas prikazu
    "reasoning-high": "codex-reasoning",  # polozka reasoning so suhrnom
    "resume-multi": "codex-resume",  # thread/resume s plnou historiou
}

# Polia, ktorych obsah je tento stroj, nie protokol.
MACHINE_FIELDS = {"serverName", "installationId"}

LINE = re.compile(r"^\s*[0-9.]+ < (.*)$")


def path_rewrites():
    home = os.path.expanduser("~")
    return [
        (re.compile(re.escape(home), re.IGNORECASE), r"C:\\Users\\user"),
        (re.compile(re.escape(home.replace("\\", "/")), re.IGNORECASE),
         "C:/Users/user"),
    ]


class Scrubber:
    def __init__(self):
        self.paths = path_rewrites()
        self.rewritten = 0

    def walk(self, node, key=None):
        if isinstance(node, dict):
            return {k: self.walk(v, k) for k, v in node.items()}
        if isinstance(node, list):
            return [self.walk(v, key) for v in node]
        if key in MACHINE_FIELDS and isinstance(node, str):
            return "<scrubbed>"
        if isinstance(node, str):
            for pattern, replacement in self.paths:
                node, count = pattern.subn(replacement, node)
                self.rewritten += count
        return node


def write_fixture(log_path, out_dir, name):
    scrubber = Scrubber()
    target = os.path.join(out_dir, name + ".jsonl")
    written = 0
    with open(log_path, encoding="utf-8") as source, \
         open(target, "w", encoding="utf-8", newline="\n") as handle:
        for line in source:
            match = LINE.match(line.rstrip("\r\n"))
            if not match:
                continue
            try:
                record = json.loads(match.group(1))
            except json.JSONDecodeError:
                continue
            handle.write(json.dumps(scrubber.walk(record), ensure_ascii=False,
                                    sort_keys=True) + "\n")
            written += 1
    print("zapisane %d zaznamov do %s, prepisanych ciest %d"
          % (written, target, scrubber.rewritten))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--logs", default="C:/b/codex-probe/logs")
    parser.add_argument("--out", default=os.path.join("tests", "fixtures"))
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for log, name in LOGS.items():
        path = os.path.join(args.logs, log + ".log")
        if not os.path.exists(path):
            print("chyba %s -- pusti najprv sondy tools/probe_codex_*.py" % path)
            return 1
        write_fixture(path, args.out, name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
