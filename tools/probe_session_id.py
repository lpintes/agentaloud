# Co CLI urobi s '--session-id', ktore uz existuje?
#
# Otazka je jedna a rozhoduje o claude-gui-lkk.21: ked si id urci appka sama,
# musi vediet, ci to iste id znamena aj "obnov tuto session", alebo ci CLI
# druhy start s tym istym id odmietne.  Z '--help' sa to necita ("Use a
# specific session ID for the conversation") a hadat sa to neda.
#
# Meria sa to na jednorazovom projekte v %TEMP%, nie na skutocnom, aby sa
# nezapisovalo do prepisov, ktore niekomu patria.
#
# Beh (stoji kredit -- styri kratke tahy):
#
#   python tools/probe_session_id.py
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import time
import uuid

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

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

# Slovo, ktore sa v prvom tahu povie a v druhom pyta.  Ci sa session obnovila,
# sa neda zistit z toho, ze CLI nespadlo -- iba z toho, ci si ho pamata.
SECRET = "MODRA"


def session_dir(path):
    """Adresar, v ktorom CLI drzi session daneho projektu (viz sessions.py)."""
    home = os.path.join(os.path.expanduser("~"), ".claude", "projects")
    return os.path.join(home, re.sub(r"[^A-Za-z0-9]", "-", os.path.abspath(path)))


def run(cwd, extra, prompt, seconds=180.0):
    """Jeden beh CLI s danymi prepinacmi.  Vracia (zaznamy, stderr, kod)."""
    child = subprocess.Popen(
        ARGS + extra, cwd=cwd,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", shell=True)

    records = []
    started = time.time()
    done = threading.Event()

    def read():
        for line in child.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except ValueError:
                records.append((time.time() - started, {"NEJSON": line[:300]}))
                continue
            records.append((time.time() - started, record))
            if record.get("type") == "result":
                done.set()
        done.set()

    errors = []
    threading.Thread(target=lambda: errors.append(child.stderr.read()),
                     daemon=True).start()
    threading.Thread(target=read, daemon=True).start()

    try:
        child.stdin.write(json.dumps(INITIALIZE) + "\n")
        child.stdin.flush()
        message = {"type": "user",
                   "message": {"role": "user",
                               "content": [{"type": "text", "text": prompt}]}}
        child.stdin.write(json.dumps(message) + "\n")
        child.stdin.flush()
    except OSError as error:
        # Odmietnute id znamena, ze proces je uz mrtvy, kym mu pisem.
        records.append((0.0, {"ZAPIS ZLYHAL": str(error)}))

    done.wait(seconds)
    # Stdin sa zatvara az po 'result' -- invariant 1.
    try:
        child.stdin.close()
    except OSError:
        pass
    try:
        child.wait(20)
    except subprocess.TimeoutExpired:
        child.kill()
    return records, "".join(e or "" for e in errors), child.returncode


def report(label, records, stderr, code):
    print("\n===== %s  (navratovy kod %s)" % (label, code))
    for when, record in records:
        kind = record.get("type", "?")
        subtype = record.get("subtype", "")
        text = ""
        if kind == "assistant":
            blocks = record.get("message", {}).get("content", [])
            text = " ".join(b.get("text", "") for b in blocks
                            if b.get("type") == "text")
        elif kind == "result":
            text = str(record.get("result", ""))[:300]
        elif kind == "system" and subtype == "init":
            # Pri obnoveni je prave toto odpoved: pod ktorym id session bezi.
            text = "session_id=%s" % record.get("session_id")
        elif "NEJSON" in record or "ZAPIS ZLYHAL" in record:
            text = json.dumps(record, ensure_ascii=False)[:300]
        print("  %5.1fs %-18s %-22s %s" % (when, kind, subtype, text[:200]))
    if stderr.strip():
        print("  STDERR: %s" % stderr.strip()[:800])
    if not records:
        print("  (nic neprislo)")


def main():
    project = os.path.join(tempfile.gettempdir(), "claudelens-sid")
    os.makedirs(project, exist_ok=True)
    sid = str(uuid.uuid4())
    print("projekt: %s" % project)
    print("session id: %s" % sid)
    print("subory session: %s" % session_dir(project))

    ASK = "Ake slovo si si mal zapamatat? Odpovedz jednym slovom."

    records, stderr, code = run(
        project, ["--session-id", sid],
        "Zapamataj si slovo %s. Odpovedz jednym slovom: ok." % SECRET)
    report("PRVY BEH: --session-id", records, stderr, code)

    on_disk = os.path.join(session_dir(project), sid + ".jsonl")
    print("\nsubor na disku po prvom behu: %s (%s)" %
          (on_disk, "je" if os.path.exists(on_disk) else "NIE JE"))

    records, stderr, code = run(project, ["--session-id", sid], ASK)
    report("DRUHY BEH: to iste --session-id", records, stderr, code)

    # Tie dva dalsie behy patria k claude-gui-lkk.7.2 a .7.3: appka odteraz
    # posiela --session-id vzdy, takze co robi spolu s --resume nie je
    # akademicka otazka.
    records, stderr, code = run(project, ["--resume", sid], ASK)
    report("TRETI BEH: --resume", records, stderr, code)

    forked = str(uuid.uuid4())
    print("\ndalsie id pre stvrty beh: %s" % forked)
    records, stderr, code = run(
        project, ["--resume", sid, "--session-id", forked], ASK)
    report("STVRTY BEH: --resume + ine --session-id", records, stderr, code)

    print("\nOdpoved '%s' znamena, ze session pokracuje; cokolvek ine znamena "
          "novu session alebo odmietnutie." % SECRET)


if __name__ == "__main__":
    main()
