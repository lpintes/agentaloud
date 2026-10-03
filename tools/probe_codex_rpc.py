# Spolocny klient pre sondy Codexu: `codex app-server` cez stdio.
#
# Nie je to sonda sama o sebe -- importuju ho probe_codex_*.py.  Spusta
# natívny codex.exe priamo (nie node shim), lebo presne tak ho bude spustat
# appka cez CreateProcessW.  Kazdy riadok, ktory pride alebo odide, sa zapise
# do logu s casom, aby sa tvar zaznamov dal citat po skonceni.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import shutil
import subprocess
import threading
import time

# Natívna binarka z npm balicka.  `codex` v PATH je shim (sh/cmd -> node ->
# codex.js -> spawn tohto .exe); shim prida len CODEX_MANAGED_BY_NPM=1
# a CODEX_MANAGED_PACKAGE_ROOT, co ovplyvni iba hlasky o aktualizacii.
EXE = os.environ.get("CODEX_EXE") or (
    "C:/Users/pintes/scoop/apps/nodejs/current/bin/node_modules/@openai/"
    "codex/node_modules/@openai/codex-win32-x64/vendor/"
    "x86_64-pc-windows-msvc/bin/codex.exe")

REPO = "C:/b/codex-probe/repo"
LOGDIR = "C:/b/codex-probe/logs"


class Client:
    def __init__(self, name, extra_args=(), on_request=None, cwd=REPO):
        os.makedirs(LOGDIR, exist_ok=True)
        self.log = open(f"{LOGDIR}/{name}.log", "w", encoding="utf-8")
        self.started = time.time()
        self.lock = threading.Lock()
        self.next_id = 1
        self.responses = {}
        self.notes = []          # (t, msg) vsetko, co prislo
        self.on_request = on_request
        args = [EXE, "app-server", *extra_args]
        self.write_log("#", "args " + json.dumps(args))
        self.child = subprocess.Popen(
            args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, cwd=cwd)
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._read_err, daemon=True).start()

    def t(self):
        return time.time() - self.started

    def write_log(self, tag, text):
        with self.lock:
            self.log.write(f"{self.t():8.3f} {tag} {text}\n")
            self.log.flush()

    def _read(self):
        for raw in self.child.stdout:
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            self.write_log("<", line)
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            self.notes.append((self.t(), msg))
            if "id" in msg and "method" in msg:
                if self.on_request:
                    self.on_request(self, msg)
            elif "id" in msg:
                self.responses[msg["id"]] = msg
        self.write_log("#", "stdout EOF")

    def _read_err(self):
        for raw in self.child.stderr:
            self.write_log("!", raw.decode("utf-8", errors="replace")
                           .rstrip("\r\n"))

    def send(self, obj):
        line = json.dumps(obj, ensure_ascii=False)
        self.write_log(">", line)
        self.child.stdin.write(line.encode("utf-8") + b"\n")
        self.child.stdin.flush()

    def request(self, method, params=None, wait=30.0):
        rid = self.next_id
        self.next_id += 1
        msg = {"id": rid, "method": method}
        if params is not None:
            msg["params"] = params
        self.send(msg)
        if wait is None:
            return rid
        deadline = time.time() + wait
        while time.time() < deadline:
            if rid in self.responses:
                return self.responses[rid]
            time.sleep(0.05)
        return None

    def notify(self, method, params=None):
        msg = {"method": method}
        if params is not None:
            msg["params"] = params
        self.send(msg)

    def respond(self, rid, result):
        self.send({"id": rid, "result": result})

    def initialize(self, experimental=False):
        r = self.request("initialize", {
            "clientInfo": {"name": "claudelens_probe", "title": None,
                           "version": "0.0.1"},
            "capabilities": {"experimentalApi": experimental,
                             "requestAttestation": False}})
        self.notify("initialized")
        return r

    def wait_for(self, pred, timeout=120.0, start=0):
        deadline = time.time() + timeout
        i = start
        while time.time() < deadline:
            while i < len(self.notes):
                if pred(self.notes[i][1]):
                    return self.notes[i][1]
                i += 1
            time.sleep(0.05)
        return None

    def wait_method(self, method, timeout=120.0, start=0):
        return self.wait_for(lambda m: m.get("method") == method,
                             timeout, start)

    def close(self, grace=5.0):
        try:
            self.child.stdin.close()
        except OSError:
            pass
        try:
            code = self.child.wait(grace)
            self.write_log("#", f"exit {code} after stdin close")
        except subprocess.TimeoutExpired:
            self.write_log("#", "still alive after stdin close -> kill")
            self.child.kill()
        self.log.close()


def text_input(text):
    return [{"type": "text", "text": text, "text_elements": []}]


def fresh_repo():
    if os.path.isdir(REPO):
        return
    os.makedirs(REPO)
    subprocess.run(["git", "init", "-q", REPO], check=True)
