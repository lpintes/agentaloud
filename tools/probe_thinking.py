"""Ktory model a ktora verzia CLI posiela TEXT premyslania, a ktora len podpis?

Otazka vznikla z claude-gui-lkk.35: pregenerovana basic.jsonl nemala ani jeden
blok Thinking, hoci diskovy subor toho isteho behu ich ma sedem.  Model teda
premyslal a rozdiel je v tom, CO CLI POSLE -- nie v tom, ci sa myslelo.  Blok
`thinking` chodi vzdy; od istej chvile ma prazdny text a ostava z neho len
`signature`, a z prazdneho model/transcript.cpp blok zamerne nespravi.

Prva verzia tohto skriptu skusala, ci na tom nezalezi SPOSOB PODANIA promptu
(argument verzus zaznam na stdin, so `--permission-prompt-tool stdio` aj bez).
Nezalezi -- vsetky tri tvary daju to iste -- takze tie vetvy su prec a zostal
sweep cez modely, lebo prave ten rozdiel korpus ukazal:

  na CLI 2.1.258 mal haiku text (32 z 32) a opus na TEJ ISTEJ verzii nulu
  (0 z 478).  Nerozhoduje teda len verzia, rozhoduje dvojica.  Sonnet na
  2.1.258 nikdy nebezal, takze o nom korpus nehovori nic -- 128 jeho blokov je
  z verzii, kde bol prazdny aj haiku.

VYSLEDOK SWEEPU (7. 9. 2026, CLI 2.1.263): haiku NIE, sonnet NIE, opus NIE.
Vsetky tri poslu blok `thinking` a v nom prazdny text.  Text premyslania sa
teda dnes neda dostat ziadnym modelom -- a `--effort` (low/high/max) na to
vplyv nema, skusane.  Dosledky pre appku su v claude-gui-lkk.37.

Skript preto meria DLZKU textu, nie pocet blokov: pocet sam o sebe klame.

Beh:  python tools/probe_thinking.py       (tri kratke tahy, mini kredit)
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


def run_stdin(cwd, extra, model):
    """Prompt ako zaznam na stdin -- tak, ako to robi make_fixtures.py."""
    proc = subprocess.Popen(
        ["claude"] + COMMON + ["--model", model,
         "--input-format", "stream-json"] + extra,
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

    # Modely, nie tvary podania.  Tvar sa ukazal ako nepodstatny (viz hlavicka)
    # a rozhoduje dvojica verzia CLI + model: na 2.1.258 mal haiku text
    # a opus na tej istej verzii nie.  Sonnet na 2.1.258 nikdy nebezal, takze
    # o nom korpus nehovori nic a musi sa odmerat.
    cases = [("model " + model, (lambda m: lambda: run_stdin(cwd, [], m))(model))
             for model in ("haiku", "sonnet", "opus")]
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
