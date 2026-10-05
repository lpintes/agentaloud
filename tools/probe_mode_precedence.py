# Ktory rezim povoleni session naozaj dostane, ked sa bije '--permission-mode'
# z prikazoveho riadka s 'permissions.defaultMode' projektu
# (.claude/settings.json alebo .claude/settings.local.json).
#
# Appka posiela rezim vzdy ako volbu a panel si pociatocny rezim berie od
# backendu; keby projektovy defaultMode volbu prebil, stavovy riadok by od
# startu hovoril nepravdu.  Preto sa meraju oba svedkovia: 'current_permission_mode'
# z odpovede na initialize a 'permissionMode' zo system/init (ten chodi az so
# zaciatkom tahu, takze sa posle jeden kratky prompt -- stoji trochu kreditu).
# Stdin sa zatvara az po zazname 'result' (invariant 1).
#
# Haiku sa NEPOUZIVA zamerne: s '--model haiku' CLI '--permission-mode auto'
# ticho zhodi na 'default' (brana auto), aj v prazdnom projekte -- vyzeralo by
# to, ze auto prebil projekt.  Odmerane 5. 10. 2026 na 2.1.288.
#
# probe_mode_precedence.py <korenovy-priecinok>   -- vyrobi a zmaze podpriecinky
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import shutil
import subprocess
import sys
import threading
import time

BASE = [
    "claude", "-p", "--verbose",
    "--input-format", "stream-json",
    "--output-format", "stream-json",
    "--permission-prompt-tool", "stdio",
]

PROJECT_MODE = "plan"
CASES = [None, "acceptEdits", "auto", "default"]


def rq(request_id, request):
    return json.dumps(
        {"type": "control_request", "request_id": request_id,
         "request": request}) + "\n"


def run(cwd, mode, timeout=120.0):
    args = list(BASE)
    if mode:
        args += ["--permission-mode", mode]
    child = subprocess.Popen(
        args, cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", shell=True)

    found = {"init_resp": None, "sys_init": None, "result": False}
    done = threading.Event()

    def read():
        for line in child.stdout:
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            if rec.get("type") == "control_response":
                resp = rec.get("response", {})
                if resp.get("request_id") == "init-1":
                    inner = resp.get("response") or {}
                    found["init_resp"] = inner.get("current_permission_mode",
                                                   "<chyba>")
            elif rec.get("type") == "system" and rec.get("subtype") == "init":
                found["sys_init"] = rec.get("permissionMode")
            elif rec.get("type") == "result":
                found["result"] = True
                done.set()
        done.set()

    threading.Thread(target=read, daemon=True).start()
    child.stdin.write(rq("init-1", {"subtype": "initialize", "hooks": {}}))
    child.stdin.write(json.dumps({
        "type": "user",
        "message": {"role": "user", "content": "povedz len ok"}}) + "\n")
    child.stdin.flush()
    done.wait(timeout)
    try:
        child.stdin.close()
        child.wait(10)
    except Exception:
        child.kill()
    return found


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else r"C:\b\probe-mode-prec"
    for fname in ("settings.json", "settings.local.json"):
        proj = os.path.join(root, fname.replace(".", "_"))
        os.makedirs(os.path.join(proj, ".claude"), exist_ok=True)
        with open(os.path.join(proj, ".claude", fname), "w") as f:
            json.dump({"permissions": {"defaultMode": PROJECT_MODE}}, f)
        for mode in CASES:
            r = run(proj, mode)
            print("%-20s %-12s init=%-12s system/init=%-12s result=%s" % (
                fname, mode or "(ziadny)", r["init_resp"], r["sys_init"],
                r["result"]), flush=True)
        shutil.rmtree(proj, ignore_errors=True)


if __name__ == "__main__":
    main()
