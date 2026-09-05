# Ako sa headless CLI pyta pouzivatela na vyber z moznosti.
#
# Otazka je uzka a odpoved sa neda vycitat z minifikovanej binarky s istotou:
# ked model zavola AskUserQuestion, pride to ako 'can_use_tool' (a odpoved sa
# vracia v 'updatedInput'), alebo ako 'request_user_dialog' (vlastny subtyp
# s vlastnym tvarom)?  Od toho zavisi, ci ClaudeLens potrebuje novu vetvu
# v control kanali, alebo len lepsi dialog na mieste, kde dnes stoji MessageBox.
#
# Na rozdiel od probe_init.py toto POSIELA PROMPT, takze to stoji kredit --
# jeden kratky tah.  Prieskumny nastroj, nie sucast produktu (viz CLAUDE.md).
#
# Pouzitie:
#   python tools/probe_ask.py [sekundy] [dalsie prepinace pre claude...]
#
# Kazdy control_request sa vypise cely.  Na can_use_tool pre AskUserQuestion
# sa odpovie 'allow' s updatedInput, do ktoreho sa dopise 'answers' -- prva
# moznost kazdej otazky.  Ci to CLI prijme, vidno na tool_result, ktory po tom
# pride: bud v nom je zvolena moznost, alebo staznost na chybajucu odpoved.

import json
import subprocess
import sys
import tempfile
import threading
import time

ARGS = [
    "claude", "-p", "--verbose",
    "--input-format", "stream-json",
    "--output-format", "stream-json",
    "--permission-prompt-tool", "stdio",
]

PROMPT = (
    "Zavolaj nastroj AskUserQuestion a spytaj sa ma jednu otazku s tromi "
    "moznostami: ci mam radsej caj, kavu alebo vodu. Nic ine nerob, ziadne "
    "subory necitaj. Ked odpoviem, iba zopakuj, co som zvolil."
)

# Dlhe zoznamy zavadzaju od toho, na co sa tu pozeram.
NOISY = ("tools", "skills", "agents", "plugins", "slash_commands",
         "mcp_servers", "commands", "output_style", "memory_paths",
         "available_output_styles", "additionalContext", "models")


def prune(value):
    if isinstance(value, dict):
        return {k: ("(%d poloziek)" % len(v)) if k in NOISY and
                isinstance(v, (list, str)) else prune(v)
                for k, v in value.items()}
    if isinstance(value, list):
        return [prune(v) for v in value]
    return value


def answers_for(tool_input):
    """Prva moznost kazdej otazky, v tvare, v akom to schema nastroja chce."""
    answers = {}
    for question in tool_input.get("questions", []):
        options = question.get("options") or []
        if not options:
            continue
        label = options[0].get("label")
        if question.get("multiSelect"):
            answers[question["question"]] = [label]
        else:
            answers[question["question"]] = label
    return answers


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 90.0
    workdir = tempfile.mkdtemp(prefix="probe_ask_")

    child = subprocess.Popen(
        ARGS + sys.argv[2:], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", cwd=workdir,
        shell=True)

    lines = []
    started = time.time()
    write_lock = threading.Lock()
    done = threading.Event()

    def send(value):
        with write_lock:
            child.stdin.write(json.dumps(value) + "\n")
            child.stdin.flush()

    def read():
        for line in child.stdout:
            lines.append((time.time() - started, line))
            try:
                record = json.loads(line)
            except ValueError:
                continue
            if record.get("type") == "result":
                done.set()
            if record.get("type") != "control_request":
                continue
            request = record.get("request") or {}
            if request.get("subtype") != "can_use_tool":
                # request_user_dialog a elicitation: nechat tak a pozriet sa,
                # co sa stane.  Chybova odpoved sa podla binarky aj tak
                # zahadzuje a dialog zostava zaparkovany.
                continue
            tool_input = request.get("input") or {}
            updated = dict(tool_input)
            if request.get("tool_name") == "AskUserQuestion":
                updated["answers"] = answers_for(tool_input)
            send({"type": "control_response",
                  "response": {"subtype": "success",
                               "request_id": record.get("request_id"),
                               "response": {"behavior": "allow",
                                            "updatedInput": updated}}})

    threading.Thread(target=read, daemon=True).start()
    send({"type": "control_request", "request_id": "init-0",
          "request": {"subtype": "initialize", "hooks": {}}})
    # Odpoved na initialize chodi az po hookoch; prompt sa posiela hned, CLI
    # si ho odlozi.
    send({"type": "user",
          "message": {"role": "user",
                      "content": [{"type": "text", "text": PROMPT}]}})

    done.wait(seconds)
    time.sleep(1.0)
    child.kill()

    print("pracovny adresar:", workdir)
    for when, line in lines:
        try:
            record = json.loads(line)
        except ValueError:
            print("NEJSON:", line[:300].rstrip())
            continue
        kind = record.get("type")
        subtype = record.get("subtype", "")
        if kind == "control_request":
            subtype = (record.get("request") or {}).get("subtype", "")
        print("=== %5.1fs %s %s" % (when, kind, subtype))
        print(json.dumps(prune(record), indent=1, ensure_ascii=True)[:6000])
    if not lines:
        print("(nic neprislo)")
    print("stderr:", child.stderr.read()[:2000])


if __name__ == "__main__":
    main()
