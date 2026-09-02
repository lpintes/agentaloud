"""Spike ku claude-gui-lkk.1: zistit, cim sa zapne smerovanie can_use_tool
na control kanal holeho stdio -- bez oficialneho SDK.

Zistene pred spikom (viz claude-gui-lkk poznamky):
  * 'initialize' control_request z holeho stdin funguje a vrati control_response
  * bez obsluhy povoleni sa z 'ask' pravidla stane 'deny', na stream pride
    {"type":"system","subtype":"permission_denied",...} a nastroj sa nevykona
  * --permission-prompt-tool je z --help vypadnuty, ale CLI ho stale prijima

Hypoteza tohto spiku: SDK zapina canUseTool tym, ze CLI poda
--permission-prompt-tool stdio.  Skript preto spusti tu istu session viackrat,
zakazdym s inym kandidatom na prepinac, a pozrie sa, ci pride can_use_tool.

Uspech = na stdout pride control_request so subtype 'can_use_tool', skript nan
odpovie povolenim a commit sa realne vykona.  Zlyhanie = permission_denied.

Beh:  python tools/spike_control.py [--keep]
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

# Prompt volime tak, aby narazil na 'ask' pravidlo pre git commit.  Nic ine
# v repozitari nie je, takze ziadny iny nastroj sa nema preco spustit.
PROMPT = "Run exactly this bash command and nothing else: git commit --allow-empty -m spike"

# Kandidati na to, cim sa smerovanie zapina.  Poradie je od najpravdepodobnejsieho.
CANDIDATES = [
    ("stdio", ["--permission-prompt-tool", "stdio"]),
    ("sdk", ["--permission-prompt-tool", "sdk"]),
    ("mcp__control__approve", ["--permission-prompt-tool", "mcp__control__approve"]),
    ("ziadny", []),
]

BASE_ARGS = [
    "-p", "--verbose",
    "--input-format", "stream-json",
    "--output-format", "stream-json",
    "--model", "haiku",
    "--tools", "Bash",
]


def make_repo():
    """Jednorazovy git repozitar.  Commit sa v nom smie naozaj vykonat."""
    path = tempfile.mkdtemp(prefix="spike-control-")
    for args in (["init", "-q", "."],
                 ["config", "user.email", "spike@example.invalid"],
                 ["config", "user.name", "spike"]):
        subprocess.run(["git"] + args, cwd=path, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return path


def count_commits(path):
    done = subprocess.run(["git", "rev-list", "--count", "HEAD"], cwd=path,
                          capture_output=True, text=True)
    return int(done.stdout.strip()) if done.returncode == 0 else 0


class Run:
    """Jeden beh CLI.  Cita stdout vo vlakne, lebo na stdin musime pisat
    priebezne -- odpoved na can_use_tool pride az pocas tahu."""

    def __init__(self, label, extra, cwd, log):
        self.label = label
        self.log = log
        self.saw_can_use_tool = False
        self.saw_denied = False
        self.control_requests = []
        # Stdin je zaroven control kanal.  Zavriet ho skor, nez tah skonci,
        # znamena "Tool permission request failed: AbortError: Stream closed" --
        # CLI sa pytat chce, ale nema uz kam.  Cakame teda na 'result'.
        self.turn_done = threading.Event()
        self.proc = subprocess.Popen(
            ["claude"] + BASE_ARGS + extra,
            cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, encoding="utf-8", bufsize=1)
        self.lock = threading.Lock()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def send(self, obj):
        line = json.dumps(obj, ensure_ascii=False)
        self.log.write(f">>> {line}\n")
        with self.lock:
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()

    def _read(self):
        for line in self.proc.stdout:
            line = line.strip()
            if not line:
                continue
            self.log.write(f"<<< {line[:2000]}\n")
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            self._handle(msg)

    def _handle(self, msg):
        kind = msg.get("type")
        if kind == "control_request":
            request = msg.get("request", {})
            subtype = request.get("subtype")
            self.control_requests.append(subtype)
            if subtype == "can_use_tool":
                self.saw_can_use_tool = True
                self._approve(msg)
        elif kind == "system" and msg.get("subtype") == "permission_denied":
            self.saw_denied = True
        elif kind == "result":
            self.turn_done.set()

    def _approve(self, msg):
        """Tvar odpovede podla toho, co robi SDK: control_response so subtype
        'success' a v nom vysledok canUseTool callbacku."""
        request = msg.get("request", {})
        self.send({
            "type": "control_response",
            "response": {
                "subtype": "success",
                "request_id": msg.get("request_id"),
                "response": {
                    "behavior": "allow",
                    "updatedInput": request.get("input", {}),
                },
            },
        })

    def finish(self, timeout):
        self.turn_done.wait(timeout=timeout)
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        deadline = time.time() + 15
        while time.time() < deadline and self.proc.poll() is None:
            time.sleep(0.2)
        if self.proc.poll() is None:
            self.proc.kill()
        self.reader.join(timeout=2)


def attempt(label, extra, log):
    repo = make_repo()
    log.write(f"\n===== kandidat: {label}  ({' '.join(extra) or 'bez prepinaca'}) =====\n")
    run = Run(label, extra, repo, log)
    run.send({"type": "control_request", "request_id": "init-1",
              "request": {"subtype": "initialize", "hooks": {}}})
    time.sleep(3)
    run.send({"type": "user", "message": {"role": "user",
              "content": [{"type": "text", "text": PROMPT}]}})
    run.finish(timeout=90)
    commits = count_commits(repo)
    shutil.rmtree(repo, ignore_errors=True)
    return {
        "kandidat": label,
        "can_use_tool": run.saw_can_use_tool,
        "permission_denied": run.saw_denied,
        "commitov": commits,
        "control_requesty": run.control_requests,
    }


def main():
    log_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "spike_control.log")
    results = []
    with open(log_path, "w", encoding="utf-8") as log:
        for label, extra in CANDIDATES:
            try:
                result = attempt(label, extra, log)
            except Exception as error:          # noqa: BLE001 -- spike
                result = {"kandidat": label, "chyba": repr(error)}
            results.append(result)
            print(json.dumps(result, ensure_ascii=False))
            if result.get("can_use_tool"):
                print(f"\nUSPECH: smerovanie zapina '{label}'")
                break
    print(f"\nSurovy zaznam: {log_path}")


if __name__ == "__main__":
    main()
