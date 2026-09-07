"""Nesie stream bloky `thinking`, ked prompt pride cez --input-format stream-json?

Otazka vznikla z claude-gui-lkk.35: pregenerovana basic.jsonl nemala ani jeden
blok Thinking, hoci diskovy subor toho isteho behu ich ma sedem.  Model teda
premyslal a rozdiel je v tom, co CLI posle na stdout -- nie v tom, co spravilo.

Jediny rozdiel oproti sonde, ktora thinking videla, je sposob podania promptu:
argument na prikazovom riadku verzus zaznam typu user na stdin.  Skript teda
pusti oba tvary s inak rovnakymi volbami a spocita bloky.

ODPOVED (7. 9. 2026): na sposobe podania NEZALEZI -- vsetky tri tvary daju
rovnaky vysledok.  Zalezi na verzii CLI.  Bloky `thinking` chodia vzdy, ale od
2.1.260 s PRAZDNYM TEXTOM a samotnym podpisom, takze Transcript z nich blok
nespravi.  Skript preto uz nepocita len bloky, ale aj dlzku ich textu -- pocet
sam o sebe klame.

Krizova tabulka nad korpusom (197 suborov, vyse 6000 casti `thinking`):
text ma 32 a vsetkych 32 je z CLI 2.1.258 + haiku; 2.1.260 a 2.1.263 maju pri
haiku nulu a opus so sonnetom nemali text ani raz, na ziadnej z 20 verzii.
Podrobnosti v claude-gui-lkk.35.

Beh:  python tools/probe_thinking.py       (tri tahy haiku, mini kredit)
"""
import json
import os
import subprocess
import sys
import tempfile
import threading

PROMPT = "Odpovedz jednou vetou: na co je subor README.md?"

COMMON = [
    "-p", "--verbose",
    "--output-format", "stream-json",
    "--model", "haiku",
]


def count_blocks(lines):
    """Pocty podla druhu, a k tomu dlzky textu blokov `thinking`.

    Dlzky su tu preto, ze pocet sam o sebe klame: blok chodi aj vtedy, ked
    v nom nic nie je, a prave to je ten rozdiel, kvoli ktoremu sonda vznikla.
    """
    counts = {}
    thinking_lengths = []
    for line in lines:
        line = line.strip()
        if not line:
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        message = record.get("message")
        if not isinstance(message, dict):
            continue
        for part in message.get("content") or []:
            if isinstance(part, dict):
                kind = part.get("type")
                counts[kind] = counts.get(kind, 0) + 1
                if kind == "thinking":
                    thinking_lengths.append(len(part.get("thinking") or ""))
    return counts, thinking_lengths


def run_argument(cwd):
    """Prompt ako argument -- tak, ako to robila sonda, ktora thinking videla."""
    result = subprocess.run(
        ["claude"] + COMMON + [PROMPT],
        cwd=cwd, capture_output=True, text=True, encoding="utf-8")
    return result.stdout.splitlines(), result.returncode, result.stderr


def run_stdin(cwd, extra):
    """Prompt ako zaznam na stdin -- tak, ako to robi make_fixtures.py."""
    proc = subprocess.Popen(
        ["claude"] + COMMON + ["--input-format", "stream-json"] + extra,
        cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
    stdin, stdout = proc.stdin, proc.stdout
    assert stdin is not None and stdout is not None

    lines = []
    done = threading.Event()

    def read():
        for line in stdout:
            lines.append(line)
            try:
                if json.loads(line.strip()).get("type") == "result":
                    done.set()
            except (json.JSONDecodeError, AttributeError):
                pass

    threading.Thread(target=read, daemon=True).start()
    stdin.write(json.dumps({"type": "user", "message": {
        "role": "user",
        "content": [{"type": "text", "text": PROMPT}]}}) + "\n")
    stdin.flush()
    done.wait(180)
    # Az teraz: stdin je control kanal (invariant 1).
    stdin.close()
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
    return lines, proc.returncode, ""


def main():
    cwd = tempfile.mkdtemp(prefix="probe-thinking-")
    with open(os.path.join(cwd, "README.md"), "w", encoding="utf-8") as handle:
        handle.write("# Pokus\n")

    cases = [
        ("prompt ako argument", lambda: run_argument(cwd)),
        ("prompt cez stdin", lambda: run_stdin(cwd, [])),
        ("prompt cez stdin + permission-prompt-tool",
         lambda: run_stdin(cwd, ["--permission-prompt-tool", "stdio"])),
    ]
    for name, run in cases:
        lines, code, err = run()
        counts, lengths = count_blocks(lines)
        # "S TEXTOM" je ta otazka, nie "prisiel blok".
        print("%-42s kod=%s bloky=%s dlzky=%s  S TEXTOM: %s"
              % (name, code, counts, lengths,
                 "ANO" if any(lengths) else "NIE"))
        if err.strip():
            print("   stderr:", err.strip()[:200])
    return 0


if __name__ == "__main__":
    sys.exit(main())
