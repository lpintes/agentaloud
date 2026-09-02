"""Vyrobi zafixovane fixtury pre testy urovne 1 (claude-gui-lkk.3).

Fixtura je surovy stream z jednej realnej session, zbaveny volatilnych poli.
Nie je to nahrada za realne data -- JE to realne data, len take, ktore sa
smu zverejnit a ktore sa uz nikdy nezmenia.  Analogia je ROM z eureka-a4:
raz zachyteny artefakt, nie nieco, co sa regeneruje pri kazdom behu.

Skript teda NEBEZI v testoch.  Pusti sa rucne vtedy, ked sa zmeni format
CLI, a diff vyslednej fixtury je prave ta informacia, ktoru chces vidiet.

Preco sa fixtury nepisu rucne: rucne napisana fixtura testuje moju predstavu
o formate, nie format.  To je presne ten sposob zlyhania, ktory ma stat v ceste.

Beh:  python tools/make_fixtures.py [--out tests/fixtures]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

# Kazdy prompt je jeden tah v jednej session, takze fixtura obsahuje aj
# nadvaznost tahov, nielen izolovane spravy.  Vyber je taky, aby prisiel
# kazdy typ bloku, ktory model/transcript rozoznava.
TURNS = [
    # text bez nastroja
    "Odpovedz jednou vetou: na co je subor README.md?",
    # nastroj s vystupom
    "Precitaj subor hello.txt a povedz, co je v nom.",
    # nastroj, ktory zlyha -- tool_result s is_error
    "Spusti presne tento prikaz a nic ine: cat neexistujuci-subor.txt",
    # povolenie cez control kanal, ktore zamietneme
    "Spusti presne tento prikaz a nic ine: git commit --allow-empty -m fixture",
]

# Zamietame, nie schvalujeme: zamietnutie prejde cez control kanal rovnako ako
# schvalenie, ale nezanecha commit, takze fixtura sa da vyrobit znova rovnako.
DENY_MESSAGE = "Pouzivatel to zamietol."

FILES = {
    "hello.txt": "ahoj\ndruhy riadok\n",
    "README.md": "# Hrackarsky repozitar\n\nExistuje len kvoli fixturam.\n",
    "notes.md": "poznamka\n",
}

BASE_ARGS = [
    "-p", "--verbose",
    "--input-format", "stream-json",
    "--output-format", "stream-json",
    "--model", "haiku",
    "--tools", "Bash,Read,Edit",
]

# Dva profily, lebo su to dve rozne cesty a kazda ma vlastny tvar zaznamov.
#
# basic:  s --permission-prompt-tool stdio.  Povolenie sa pyta cez control
#         kanal a zamietnutie je nase rozhodnutie.
# denied: bez neho.  Z pravidla "ask" sa stane "deny" a na stream pride
#         system/permission_denied -- blok, ktory transcript zobrazuje vzdy
#         rozbaleny a ktory by inak zostal netestovany.
PROFILES = {
    "basic": {"args": ["--permission-prompt-tool", "stdio"], "turns": TURNS},
    "denied": {"args": [], "turns": TURNS[-1:]},
}

# Polia, ktorych hodnota sa meni od behu k behu a o formate nehovoria nic.
VOLATILE_SCALARS = {
    "timestamp", "duration_api_ms", "duration_ms", "total_cost_usd",
    "signature", "pid", "cwd", "resetsAt", "overageResetsAt",
}
# Polia s identifikatorom.  Nevyhadzuju sa, ale prepisuju sa dosledne, aby
# vazba tool_use -> tool_result prezila; prave tu vazbu testy kontroluju.
ID_FIELDS = {
    "session_id", "uuid", "request_id", "id", "tool_use_id",
    "parent_tool_use_id", "hook_id",
}


def make_repo():
    path = tempfile.mkdtemp(prefix="claudelens-fixture-")
    for name, text in FILES.items():
        with open(os.path.join(path, name), "w", encoding="utf-8") as handle:
            handle.write(text)
    for args in (["init", "-q", "."],
                 ["config", "user.email", "fixture@example.invalid"],
                 ["config", "user.name", "fixture"],
                 ["add", "."],
                 ["commit", "-q", "-m", "zaklad"]):
        subprocess.run(["git"] + args, cwd=path, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return path


class Capture:
    def __init__(self, cwd, extra_args):
        self.lines = []
        self.turn_done = threading.Event()
        self.proc = subprocess.Popen(
            ["claude"] + BASE_ARGS + extra_args, cwd=cwd,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, encoding="utf-8", bufsize=1)
        # Popen typuje rury ako Optional; s PIPE su vzdy otvorene.  Zuzenie
        # musi prebehnut na lokalnych premennych -- na atributoch ho typovac
        # cez hranicu metody neprenesie.
        stdin, stdout = self.proc.stdin, self.proc.stdout
        assert stdin is not None and stdout is not None
        self.stdin, self.stdout = stdin, stdout
        self.lock = threading.Lock()
        threading.Thread(target=self._read, daemon=True).start()

    def send(self, obj):
        with self.lock:
            self.stdin.write(json.dumps(obj, ensure_ascii=False) + "\n")
            self.stdin.flush()

    def _read(self):
        for line in self.stdout:
            line = line.strip()
            if not line:
                continue
            self.lines.append(line)
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if msg.get("type") == "control_request":
                self._answer(msg)
            elif msg.get("type") == "result":
                self.turn_done.set()

    def _answer(self, msg):
        request = msg.get("request", {})
        if request.get("subtype") == "can_use_tool":
            inner = {"behavior": "deny", "message": DENY_MESSAGE}
        else:
            inner = None
        if inner is None:
            self.send({"type": "control_response",
                       "response": {"subtype": "error",
                                    "request_id": msg.get("request_id"),
                                    "error": "nepodporovane vo fixturach"}})
            return
        self.send({"type": "control_response",
                   "response": {"subtype": "success",
                                "request_id": msg.get("request_id"),
                                "response": inner}})

    def turn(self, text, timeout=120):
        self.turn_done.clear()
        self.send({"type": "user", "message": {
            "role": "user",
            "content": [{"type": "text", "text": text}]}})
        if not self.turn_done.wait(timeout):
            raise TimeoutError("tah sa neskoncil v case: " + text[:40])

    def close(self):
        # Az teraz.  Stdin je control kanal -- viz Invarianty v CLAUDE.md.
        try:
            self.stdin.close()
        except OSError:
            pass
        deadline = time.time() + 15
        while time.time() < deadline and self.proc.poll() is None:
            time.sleep(0.2)
        if self.proc.poll() is None:
            self.proc.kill()


class Scrubber:
    """Prepisuje identifikatory na stabilne nahrady, aby fixtura bola po
    kazdom vyrobeni rovnaka tam, kde na tom zalezi."""

    def __init__(self):
        self.mapping = {}

    def id_for(self, value):
        if value in self.mapping:
            return self.mapping[value]
        # Prefix sa zachova, nech je v fixture vidno, o aky druh id ide.
        prefix = "toolu" if str(value).startswith("toolu_") else "id"
        replacement = "%s_%04d" % (prefix, len(self.mapping) + 1)
        self.mapping[value] = replacement
        return replacement

    def walk(self, node, key=None):
        if isinstance(node, dict):
            return {k: self.walk(v, k) for k, v in node.items()}
        if isinstance(node, list):
            return [self.walk(v, key) for v in node]
        if key in ID_FIELDS and isinstance(node, str) and node:
            return self.id_for(node)
        if key in VOLATILE_SCALARS:
            if isinstance(node, str):
                return "<scrubbed>"
            if isinstance(node, bool):
                return node
            if isinstance(node, (int, float)):
                return 0
        return node


def capture_profile(name):
    profile = PROFILES[name]
    repo = make_repo()
    print("[%s] hrackarsky repozitar: %s" % (name, repo))
    capture = Capture(repo, profile["args"])
    try:
        for text in profile["turns"]:
            print("  tah:", text[:60])
            capture.turn(text)
    finally:
        capture.close()
        shutil.rmtree(repo, ignore_errors=True)
    return capture.lines


def write_fixture(lines, out_dir, name):
    scrubber = Scrubber()
    os.makedirs(out_dir, exist_ok=True)
    target = os.path.join(out_dir, name + ".jsonl")
    written = 0
    with open(target, "w", encoding="utf-8", newline="\n") as handle:
        for line in lines:
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            # Hooky su vlastnostou stroja, na ktorom sa fixtura vyrobila,
            # nie formatu.  V transkripte sa aj tak nezobrazuju.
            if record.get("type") == "system" and \
               record.get("subtype", "").startswith("hook_"):
                continue
            handle.write(json.dumps(scrubber.walk(record), ensure_ascii=False,
                                    sort_keys=True) + "\n")
            written += 1
    print("zapisane %d zaznamov do %s" % (written, target))
    print("prepisanych identifikatorov:", len(scrubber.mapping))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join("tests", "fixtures"))
    parser.add_argument("--profile", action="append", choices=list(PROFILES),
                        help="opakovatelne; bez neho sa vyrobia vsetky")
    args = parser.parse_args()

    for name in args.profile or list(PROFILES):
        write_fixture(capture_profile(name), args.out, name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
