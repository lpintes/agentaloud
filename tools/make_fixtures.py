"""Vyrobi zafixovane fixtury pre testy urovne 2 (claude-gui-lkk.3).

Fixtura je surovy stream z jednej realnej session, zbaveny volatilnych poli.
Nie je to nahrada za realne data -- JE to realne data, len take, ktore sa
smu zverejnit a ktore sa uz nikdy nezmenia.  Analogia je ROM z eureka-a4:
raz zachyteny artefakt, nie nieco, co sa regeneruje pri kazdom behu.

Skript teda NEBEZI v testoch.  Pusti sa rucne vtedy, ked sa zmeni format
CLI, a diff vyslednej fixtury je prave ta informacia, ktoru chces vidiet.

Preco sa fixtury nepisu rucne: rucne napisana fixtura testuje moju predstavu
o formate, nie format.  To je presne ten sposob zlyhania, ktory ma stat v ceste.

Okrem streamu sa zachytava aj SUBOR SESSION NA DISKU (disk.jsonl).  Nie je to
ten isty format: disk ma vyse desat vlastnych typov zaznamov, nema system/init
ani result, a spolocne su len user a assistant.  Prave z neho sa obnovuje
historia po --resume, takze bez zafixovanej fixtury by tu cestu testoval len
soak nad sukromnym korpusom -- teda nikto, kto soak nepusta.

Beh:  python tools/make_fixtures.py [--out tests/fixtures]
"""
import argparse
import datetime
import glob
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
#
# Diskovy subor sa berie len z basic.  Je to ten isty rozhovor, len prepisany
# CLI do vlastneho formatu, takze druhy by nepridal ziadny tvar navyse -- a
# stal by dalsi beh.
PROFILES = {
    "basic": {"args": ["--permission-prompt-tool", "stdio"], "turns": TURNS,
              "disk": True},
    "denied": {"args": [], "turns": TURNS[-1:], "disk": False},
}

# Polia, ktorych hodnota sa meni od behu k behu a o formate nehovoria nic.
VOLATILE_SCALARS = {
    "duration_api_ms", "duration_ms", "total_cost_usd",
    "signature", "pid", "cwd", "resetsAt", "overageResetsAt",
}

# Timestamp sa NEnahradzuje retazcom "<scrubbed>", hoci volatilny je.  Diskovy
# format je jediny, kde na case zalezi: podla neho sa vybera najnovsia session
# a hlada sa odzadu prvy zaznam, ktory cas nesie (invariant 15).  Fixtura s
# necitatelnym casom by tu vlastnost otestovat nedala.  Kazdy odlisny cas teda
# dostane stabilnu nahradu v poradi, v akom sa prvy raz objavil -- co je
# poradie zapisu, takze rastie rovnako ako originaly.
STAMP_EPOCH = datetime.datetime(2026, 1, 1, tzinfo=datetime.timezone.utc)
# Polia s identifikatorom.  Nevyhadzuju sa, ale prepisuju sa dosledne, aby
# vazba tool_use -> tool_result prezila; prave tu vazbu testy kontroluju.
#
# Druha polovica zoznamu je diskova a v streame nie je ani jedno z tych poli:
# subor session je retaz zaznamov previazana cez parentUuid a nesie vlastne
# id promptu, poziadavky a zaznamu, z ktoreho vysledok nastroja pochadza.
# Mena su camelCase, nie snake_case ako v streame -- su to dva formaty, nie
# jeden (viz kKnownDiskOnlyTypes v tests/test_main.cpp).  Zistene pozretim
# skutocneho suboru, nie z dokumentacie.
ID_FIELDS = {
    "session_id", "uuid", "request_id", "id", "tool_use_id",
    "parent_tool_use_id", "hook_id",
    "sessionId", "parentUuid", "promptId", "requestId",
    "sourceToolAssistantUUID", "leafUuid",
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


def disk_session_path(session_id):
    """Subor, do ktoreho CLI zapisalo tuto session, alebo None.

    Hlada sa podla ID cez glob, nie skladanim mena adresara z cesty
    hrackarskeho repozitara: to je docasny adresar, ktoreho meno si Windows
    moze podat aj v kratkom 8.3 tvare, a kluc by potom nesedel.  ID je UUID,
    takze glob cez vsetky projekty nemoze trafit cudziu session.
    """
    home = os.environ.get("CLAUDE_CONFIG_DIR") or \
        os.path.join(os.path.expanduser("~"), ".claude")
    found = glob.glob(os.path.join(home, "projects", "*",
                                   session_id + ".jsonl"))
    return found[0] if found else None


class Capture:
    def __init__(self, cwd, extra_args):
        self.lines = []
        self.session_id = None
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
            # Surove, este pred Scrubberom -- pod tymto menom lezi subor na
            # disku.  Appka si ho od verzie s --session-id urcuje sama, tu ho
            # urcuje CLI, takze sa da zistit jedine zo streamu.
            if self.session_id is None and msg.get("session_id"):
                self.session_id = msg["session_id"]
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
        self.stamps = {}

    def stamp_for(self, value):
        if value not in self.stamps:
            when = STAMP_EPOCH + datetime.timedelta(seconds=len(self.stamps))
            # Tak, ako to pise CLI: ISO 8601 v UTC so 'Z' a milisekundami.
            # Tvar je to, na com zalezi -- LatestSession ho triedi ako text
            # a nikdy neparsuje.
            self.stamps[value] = when.strftime("%Y-%m-%dT%H:%M:%S.000Z")
        return self.stamps[value]

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
        if key == "timestamp" and isinstance(node, str) and node:
            return self.stamp_for(node)
        if key in VOLATILE_SCALARS:
            if isinstance(node, str):
                return "<scrubbed>"
            if isinstance(node, bool):
                return node
            if isinstance(node, (int, float)):
                return 0
        return node


def capture_profile(name):
    """Vrati (riadky streamu, riadky diskoveho suboru)."""
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

    disk = []
    if profile["disk"]:
        # Az po close(): dovtedy proces bezi a subor nemusi byt dopisany.
        path = capture.session_id and disk_session_path(capture.session_id)
        if not path:
            print("  POZOR: subor session sa nenasiel, disk.jsonl sa nepise")
        else:
            print("  subor session:", path)
            # Zostava tam, kde je.  Je to skutocna session v adresari
            # pouzivatela a mazanie cudzich suborov nie je uloha tohto
            # skriptu -- adresar projektu po hrackarskom repozitari sa da
            # zmazat rukou, ked prekaza.
            with open(path, encoding="utf-8") as handle:
                disk = [line.strip() for line in handle if line.strip()]
    return capture.lines, disk


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
            # To iste, ale naliehavejsie, plati pre attachment na disku: nesie
            # kontext, ktory CLI poskladalo z tohto stroja -- globalny
            # CLAUDE.md pouzivatela, jeho e-mail, cely prompt_snapshot.  Bolo
            # to 186 z 209 kB fixtury a do verejneho repozitara to nepatri.
            # O formate nehovori nic, co by testy videli: ReadSessionRecords
            # prepusta len user a assistant, a to, ze typ attachment existuje,
            # je zapisane v kKnownDiskOnlyTypes.
            if record.get("type") == "attachment":
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
        stream, disk = capture_profile(name)
        # Kazdy subor ma vlastny Scrubber, hoci zdielany by dal rovnake id
        # v oboch a diff by sa cital lepsie.  Zdielanie by ich zviazalo:
        # fixtura je artefakt, ktory sa da zahodit a nahradit starsim, a to sa
        # uz raz stalo -- pri pregenerovani basic.jsonl v nom model nepremyslal
        # a fixtura prisla o bloky Thinking, takze sa vratila povodna a nova
        # zostala len na disku.  So spolocnym cislovanim by tym prestala platit
        # aj ta druha.
        write_fixture(stream, args.out, name)
        if disk:
            write_fixture(disk, args.out, "disk")
    return 0


if __name__ == "__main__":
    sys.exit(main())
