# Co sa zmeni, ked sa prihlasime o druhy dialogov.
#
# CLI na subtype 'request_user_dialog' zlyhava ZATVORENE: druh posle len
# klientovi, ktory ho vymenoval v initialize.supportedDialogKinds.  My zatial
# nevymenuvame ziadny, takze ta vetva je nedosiahnutelna -- viz invariant 12
# v CLAUDE.md a claude-gui-lkk.25.
#
# Otazka, na ktoru tato sonda odpoveda, je uzka a ma dve polovice:
#
#   1. Ked sa prihlasime o 'permission_bash', pride povolenie na Bash ako
#      request_user_dialog namiesto can_use_tool?  Alebo popri nom?  Alebo sa
#      nezmeni nic a tie druhy su len pre TUI?
#   2. Ak pride: co je v payloade navyse oproti can_use_tool?  Od toho zavisi,
#      ci je claude-gui-lkk.25 maly krok, alebo iny vstup do kroku 6.
#
# POSIELA PROMPT, takze to stoji kredit -- jeden kratky tah.  Prieskumny
# nastroj, nie sucast produktu (viz CLAUDE.md).
#
# Pouzitie:
#   python tools/probe_dialog.py [sekundy] [dalsie prepinace pre claude...]
#
# Baseline netreba merat znova: bez supportedDialogKinds je to obycajny
# can_use_tool a je to zachytene v tools/probe_ask.py a v teste
# TestAskUserQuestionRoundTrip.

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

# Druhy najdene v binarke.  Prihlasujeme sa o tie, ktore vieme na tomto behu
# vyvolat -- Bash povolenie je jedno stlacenie, refusal_fallback_prompt sa na
# povel vyvolat neda (nastava, ked API odmietne a ponukne fallback).
#
# POZOR: prihlasit sa smie len o druh, ktory sa naozaj vie zobrazit.  Host,
# ktory dostane druh, ktory nedeklaroval, nan NESMIE odpovedat vobec; chybova
# odpoved sa zahadzuje a dialog zostane zaparkovany az do svojho deadline.
# Tu je to sonda, takze deklarujeme viac, nez appka vie -- a preto sa to robi
# v jednorazovom adresari a nie v ostrej session.
DIALOG_KINDS = [
    "permission_bash",
    "permission_file",
    "permission_prompt",
    "permission_ask_user_question",
]

# `echo ahoj` sa NEPYTA -- odmerane 2026-09-05, prebehlo bez jedineho
# control_requestu.  Bash ma vlastny klasifikator neskodnych prikazov (v
# payloade permission_bash je pole 'classifierState'), takze na vyvolanie
# povolenia treba nieco, co je v pravidlach vyslovne 'ask'.  Tu je to
# `git commit`: je v ~/.claude/settings.json medzi ask a navyse na nom visi
# PreToolUse hook.  Preto ten jednorazovy git repozitar -- rovnako ako ho
# potrebuje bin/spike_console.exe.
PROMPT = (
    "Sprav v tomto repozitari commit vsetkeho, co je pripravene, so spravou "
    "'sonda'. Nic ine nerob."
)


def make_repo(workdir):
    """Jednorazovy git repozitar s jednym pripravenym suborom."""
    with open(workdir + "/subor.txt", "w", encoding="utf-8") as file:
        file.write("sonda\n")
    for command in (["git", "init", "-q"],
                    ["git", "config", "user.email", "sonda@example.com"],
                    ["git", "config", "user.name", "Sonda"],
                    ["git", "add", "subor.txt"]):
        subprocess.run(command, cwd=workdir, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

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


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 90.0
    workdir = tempfile.mkdtemp(prefix="probe_dialog_")
    make_repo(workdir)

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
            subtype = request.get("subtype")
            if subtype == "can_use_tool":
                # Povolit, nech tah dobehne a vidno cely priebeh.
                send({"type": "control_response",
                      "response": {"subtype": "success",
                                   "request_id": record.get("request_id"),
                                   "response": {
                                       "behavior": "allow",
                                       "updatedInput": request.get("input")
                                                       or {}}}})
            elif subtype == "request_user_dialog":
                # Toto je to, kvoli comu sonda existuje.  Odpoveda sa
                # 'cancelled' -- podla binarky je to skutocne vyrovnanie
                # ("pouzivatel dialog zavrel"), nie chyba, takze sa na nom da
                # vidiet, ako CLI settlement spracuje.
                send({"type": "control_response",
                      "response": {"subtype": "success",
                                   "request_id": record.get("request_id"),
                                   "response": {"behavior": "cancelled"}}})

    threading.Thread(target=read, daemon=True).start()
    send({"type": "control_request", "request_id": "init-0",
          "request": {"subtype": "initialize",
                      "hooks": {},
                      "supportedDialogKinds": DIALOG_KINDS}})
    send({"type": "user",
          "message": {"role": "user",
                      "content": [{"type": "text", "text": PROMPT}]}})

    done.wait(seconds)
    time.sleep(1.0)
    child.kill()

    print("pracovny adresar:", workdir)
    print("deklarovane druhy:", DIALOG_KINDS)
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
