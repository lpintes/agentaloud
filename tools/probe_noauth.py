# Co povie neprihlasene Claude a Codex (claude-gui-lkk.71.1).
#
# Nikto sa neodhlasuje: obe CLI dostanu prazdny konfiguracny priecinok
# (CLAUDE_CONFIG_DIR, CODEX_HOME) a prostredie bez klucov API a bez
# premennych tejto session Claude Code, ktore by dieta inak zdedilo.  Prihlasenie
# sa nedokonci, iba zacne a zrusi, takze sa nespotrebuje kredit a ostry ucet
# zostane netknuty -- na konci sa to overi podla casu zmeny jeho suborov.
#
# Vsetko, co pride alebo odide, ide s casom do C:/b/noauth-probe/logs.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

import probe_codex_rpc
from probe_codex_rpc import Client, text_input

ROOT = "C:/b/noauth-probe"
LOGDIR = ROOT + "/logs"
probe_codex_rpc.LOGDIR = LOGDIR

HOME = os.path.expanduser("~")
CLAUDE = shutil.which("claude") or "claude"

REAL_SECRETS = [os.path.join(HOME, ".claude", ".credentials.json"),
                os.path.join(HOME, ".claude.json"),
                os.path.join(HOME, ".codex", "auth.json")]

# Prefixy premennych, ktore nesu prihlasenie alebo vazbu na rodicovsku
# session Claude Code.  Dieta, ktore by ich zdedilo, by mohlo byt prihlasene
# inou cestou nez suborom a sonda by merala nieco ine, nez si mysli.
DROP = ("ANTHROPIC_", "CLAUDE_", "CLAUDECODE", "OPENAI_", "CODEX_")


def clean_env(**extra):
    env = {k: v for k, v in os.environ.items()
           if not k.upper().startswith(DROP)}
    env.update(extra)
    return env


def mtimes():
    return {p: os.path.getmtime(p) if os.path.exists(p) else None
            for p in REAL_SECRETS}


class Log:
    def __init__(self, name):
        os.makedirs(LOGDIR, exist_ok=True)
        self.f = open(f"{LOGDIR}/{name}.log", "w", encoding="utf-8")
        self.started = time.time()
        self.lock = threading.Lock()

    def write(self, tag, text):
        with self.lock:
            self.f.write(f"{time.time() - self.started:8.3f} {tag} {text}\n")
            self.f.flush()


def run_once(log, args, env, stdin_text=None, timeout=30):
    log.write("#", "args " + json.dumps(args))
    try:
        r = subprocess.run(args, env=env, input=stdin_text or "",
                           capture_output=True, text=True, encoding="utf-8",
                           errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired as e:
        log.write("#", f"timeout after {timeout}s")
        for tag, data in (("<", e.stdout), ("!", e.stderr)):
            for line in (data or b"").decode("utf-8", "replace").splitlines():
                log.write(tag, line)
        return None
    for line in r.stdout.splitlines():
        log.write("<", line)
    for line in r.stderr.splitlines():
        log.write("!", line)
    log.write("#", f"exit {r.returncode}")
    return r.returncode


def claude_stream(log, env, cwd, prompt, seconds=90):
    exe = CLAUDE
    args = [exe, "-p", "--verbose", "--input-format", "stream-json",
            "--output-format", "stream-json",
            "--permission-prompt-tool", "stdio", "--model", "haiku"]
    log.write("#", "args " + json.dumps(args))
    child = subprocess.Popen(args, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             env=env, cwd=cwd)
    got_result = threading.Event()

    def read(stream, tag):
        for raw in stream:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            log.write(tag, line)
            if tag == "<" and '"type":"result"' in line.replace(" ", ""):
                got_result.set()
        log.write("#", f"{tag} EOF")

    threading.Thread(target=read, args=(child.stdout, "<"),
                     daemon=True).start()
    threading.Thread(target=read, args=(child.stderr, "!"),
                     daemon=True).start()

    def send(obj):
        line = json.dumps(obj)
        log.write(">", line)
        try:
            child.stdin.write(line.encode("utf-8") + b"\n")
            child.stdin.flush()
        except OSError as e:
            log.write("#", f"stdin write failed: {e}")

    send({"type": "control_request", "request_id": "init-0",
          "request": {"subtype": "initialize", "hooks": {}}})
    time.sleep(5)
    if child.poll() is None:
        send({"type": "user",
              "message": {"role": "user", "content": prompt}})
        deadline = time.time() + seconds
        while time.time() < deadline and child.poll() is None \
                and not got_result.is_set():
            time.sleep(0.1)
    # Invariant 1: stdin sa zatvara az po result -- tu je to jedno, ale nech
    # sa vidi, ci proces skonci sam.
    try:
        child.stdin.close()
    except OSError:
        pass
    try:
        code = child.wait(15)
        log.write("#", f"exit {code}")
    except subprocess.TimeoutExpired:
        log.write("#", "still alive after stdin close -> kill")
        child.kill()


def probe_claude(cfg, repo):
    env = clean_env(CLAUDE_CONFIG_DIR=cfg)
    log = Log("claude")
    log.write("#", "== claude auth status --json")
    run_once(log, [CLAUDE, "auth", "status", "--json"], env)
    log.write("#", "== claude auth status --text")
    run_once(log, [CLAUDE, "auth", "status", "--text"], env)
    log.write("#", "== stream-json session")
    claude_stream(log, env, repo, "Say hi in one word.")
    log.write("#", "== config dir after: " + json.dumps(sorted(os.listdir(cfg))))


def probe_codex(home, repo):
    env = clean_env(CODEX_HOME=home)
    log = Log("codex-cli")
    log.write("#", "== codex login status")
    run_once(log, [probe_codex_rpc.EXE, "login", "status"], env)

    c = Client("codex-rpc", cwd=repo, env=env)
    c.initialize()
    c.request("account/read", {"refreshToken": False})
    r = c.request("thread/start", {"cwd": repo, "approvalPolicy": "untrusted",
                                    "sandbox": "read-only"})
    tid = (r or {}).get("result", {}).get("thread", {}).get("id")
    if tid:
        start = len(c.notes)
        c.request("turn/start", {"threadId": tid,
                                 "input": text_input("Say hi in one word.")})
        c.wait_for(lambda m: m.get("method") == "turn/completed",
                   180, start)
        time.sleep(3)
    for kind in ("chatgpt", "chatgptDeviceCode"):
        r = c.request("account/login/start", {"type": kind}, wait=60)
        login = (r or {}).get("result", {}).get("loginId")
        if login:
            time.sleep(2)
            c.request("account/login/cancel", {"loginId": login})
            c.wait_method("account/login/completed", 10)
    c.close()
    log.write("#", "== CODEX_HOME after: " + json.dumps(sorted(os.listdir(home))))


def main():
    which = sys.argv[1:] or ["claude", "codex"]
    before = mtimes()
    base = tempfile.mkdtemp(prefix="noauth-", dir=ROOT if os.path.isdir(ROOT)
                            else None)
    repo = os.path.join(base, "repo")
    os.makedirs(repo)
    subprocess.run(["git", "init", "-q", repo], check=True)
    print("base", base)
    if "claude" in which:
        cfg = os.path.join(base, "claude")
        os.makedirs(cfg)
        probe_claude(cfg, repo)
    if "codex" in which:
        home = os.path.join(base, "codex")
        os.makedirs(home)
        probe_codex(home, repo)
    after = mtimes()
    for path in REAL_SECRETS:
        state = "UNCHANGED" if before[path] == after[path] else "CHANGED"
        print(f"{state}  {path}")
    print("logs in", LOGDIR)


if __name__ == "__main__":
    main()
