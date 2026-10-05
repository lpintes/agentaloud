"""Vypise session daneho projektu -- aj tie, ktore 'claude --resume' zamlci.

Session spustena headless ('claude -p', teda aj kazda z AgentAloud) sa na disk
zapisuje ako ktorakolvek ina, ale nie je z terminalu dosiahnutelna:

  * picker '--resume' hlada podla TITULKU a titulok robia zaznamy 'ai-title',
    ktore vznikaju iba v interaktivnom TUI.  Headless session ziadny nema,
    takze v pickeri nie je a nikdy nebude;
  * 'claude -c' sa v adresari, ktory ma zaznamy v ~/.claude/history.jsonl,
    riadi nimi -- a do history.jsonl zapisuje iba interaktivne napisany prompt.

Zostava teda jediny handle, session id, a ten sa nema kde odcitat.  Tento
skript ho vytiahne z disku a rovno vypise prikaz, ktorym sa session obnovi.

Nie je sucastou produktu.  Je to to iste, co tools/spike_control.py -- nastroj
na prieskum spravania CLI, pustany rucne.

Beh:  python tools/sessions.py [cesta-k-projektu] [--all] [--limit N]
"""
import argparse
import datetime
import json
import os
import re
import sys

HOME_PROJECTS = os.path.join(
    os.path.expanduser("~"), ".claude", "projects")

# Prompty su po slovensky a windowsova konzola ma predvolene cp852.  Bez tohto
# vypadne z vypisu prave to, podla coho sa session poznava -- a zlyha to ticho,
# lebo nahradne znaky vyzeraju ako poskodeny subor, nie ako zla kodova stranka.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def encode(path):
    """Meno adresara, pod ktorym CLI drzi session daneho projektu.

    Overene na dvoch realnych pripadoch: 'C:\\vcs\\github.com\\lpintes\\claude-gui'
    -> 'C--vcs-github-com-lpintes-claude-gui'.  Kazdy znak mimo [A-Za-z0-9] sa
    meni na pomlcku, takze aj ':' aj '\\' aj '.' -- odtial to dvojite '--' hned
    za pismenom disku.
    """
    return re.sub(r"[^A-Za-z0-9]", "-", os.path.abspath(path))


def read(path):
    """Zhrnutie jednej session.  Cita cely subor, lebo to podstatne je na konci.

    Vracia None pre subor, z ktoreho sa neda precitat ani jeden zaznam -- taky
    nema co ponuknut a v zozname by len zavadzal.
    """
    summary = {
        "id": os.path.splitext(os.path.basename(path))[0],
        "path": path,
        "mtime": os.path.getmtime(path),
        "last": None,      # cas posledneho zaznamu, ktory cas nesie
        "prompt": None,    # prvy prompt cloveka, teda o com to bolo
        "messages": 0,
        "titled": False,   # ma 'ai-title', cize picker ju ponukne
        "cwd": None,
    }
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except ValueError:
                continue
            kind = record.get("type")
            if kind == "ai-title":
                summary["titled"] = True
            if record.get("timestamp"):
                summary["last"] = record["timestamp"]
            if summary["cwd"] is None and record.get("cwd"):
                summary["cwd"] = record["cwd"]
            if kind in ("user", "assistant"):
                summary["messages"] += 1
            if summary["prompt"] is None and kind == "user":
                summary["prompt"] = first_text(record)
    return summary if summary["messages"] else None


def first_text(record):
    """Text, ktory clovek naozaj napisal, alebo None.

    Zaznam 'user' nesie aj vysledky nastrojov a vypisy slash prikazov; ani
    jedno nie je prompt a ani jedno nepomoze session spoznat.
    """
    message = record.get("message")
    if not isinstance(message, dict):
        return None
    content = message.get("content")
    text = None
    if isinstance(content, str):
        text = content
    elif isinstance(content, list):
        for part in content:
            if isinstance(part, dict) and part.get("type") == "text":
                text = part.get("text")
                break
    if not text or text.lstrip().startswith("<"):
        return None
    return " ".join(text.split())


def local(stamp):
    """ISO cas z JSONL (UTC) na miestny, lebo pouzivatel hlada podla hodin."""
    if not stamp:
        return "?"
    try:
        moment = datetime.datetime.fromisoformat(stamp.replace("Z", "+00:00"))
    except ValueError:
        return stamp
    return moment.astimezone().strftime("%Y-%m-%d %H:%M")


def collect(directory):
    entries = []
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".jsonl"):
            continue
        summary = read(os.path.join(directory, name))
        if summary:
            entries.append(summary)
    entries.sort(key=lambda item: item["last"] or "", reverse=True)
    return entries


def report(entries, limit):
    for entry in entries[:limit]:
        mark = "picker" if entry["titled"] else "SKRYTA"
        print("{}  {}  {:>4} sprav  [{}]".format(
            local(entry["last"]), entry["id"], entry["messages"], mark))
        if entry["prompt"]:
            print("    {}".format(entry["prompt"][:100]))
        print("    claude -r {}".format(entry["id"]))
        print()


def main():
    parser = argparse.ArgumentParser(
        description=(__doc__ or "").splitlines()[0])
    parser.add_argument("project", nargs="?", default=".",
                        help="adresar projektu (predvolene aktualny)")
    parser.add_argument("--all", action="store_true",
                        help="vsetky projekty, nielen tento")
    parser.add_argument("--limit", type=int, default=20,
                        help="kolko session vypisat (predvolene 20)")
    arguments = parser.parse_args()

    if arguments.all:
        directories = [os.path.join(HOME_PROJECTS, name)
                       for name in sorted(os.listdir(HOME_PROJECTS))]
        directories = [item for item in directories if os.path.isdir(item)]
    else:
        directory = os.path.join(HOME_PROJECTS, encode(arguments.project))
        if not os.path.isdir(directory):
            # Nie chyba: projekt, v ktorom este nikto nic nespustil, ziadny
            # adresar nema.  Ale povedat to treba, inak to vyzera ako prazdny
            # zoznam, cize ako "vsetko sa stratilo".
            print("Pre {} zatial ziadne session ({} neexistuje).".format(
                os.path.abspath(arguments.project), directory))
            return 0
        directories = [directory]

    for directory in directories:
        entries = collect(directory)
        if not entries:
            continue
        if arguments.all:
            print("=== {} ===".format(entries[0]["cwd"] or directory))
        report(entries, arguments.limit)
    return 0


if __name__ == "__main__":
    sys.exit(main())
