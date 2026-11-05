# Co zapise "povolit natrvalo" v `codex app-server` a kam to siaha.
#
#   probe_codex_persist.py cmd    prikaz: prva ziadost s
#                                 acceptWithExecpolicyAmendment, potom ten isty
#                                 prikaz s inymi argumentmi v novom vlakne (ten
#                                 isty cwd), v inom cwd a v novom procese
#   probe_codex_persist.py cmd-prefix  to iste, ale odpoved nesie kratsi
#                                 amendment (prve dva prvky navrhu)
#   probe_codex_persist.py cmd-same    ten isty presny prikaz vo vsetkych krokoch
#   probe_codex_persist.py mcp    MCP: prva ziadost accept + _meta.persist=always,
#                                 potom ten isty nastroj v novom vlakne, v inom
#                                 cwd a v novom procese
#   probe_codex_persist.py mcp-cfg to iste, server v CODEX_HOME/config.toml
#   probe_codex_persist.py server (interne) falosny MCP server
#
# CODEX_HOME je jednorazovy (C:/b/codex-probe/home-persist, len kopia
# auth.json), skutocny ~/.codex sa nedotkne.  Po kazdom kroku sa vypise, ktore
# subory v CODEX_HOME pribudli alebo sa zmenili, a obsah textovych.
# Kazda dalsia ziadost po prvej sa zamietne (decline) -- ak pride, znamena to,
# ze trvale povolenie ju nepokrylo.
#
# Logy: C:/b/codex-probe/logs/persist-<rezim>-<krok>.log, persist-mcp.log.
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import shutil
import subprocess
import sys
import time

BASE = "C:/b/codex-probe"
MCP_LOG = BASE + "/logs/persist-mcp.log"
HOME = BASE + "/home-persist"
REAL_HOME = os.path.expanduser("~/.codex")
WORK_A = BASE + "/persist-a"
WORK_B = BASE + "/persist-b"
MODEL = "gpt-6-luna"


def server_main():
    os.makedirs(os.path.dirname(MCP_LOG), exist_ok=True)
    log = open(MCP_LOG, "a", encoding="utf-8")

    def send(obj):
        line = json.dumps(obj, ensure_ascii=False)
        log.write("> " + line + "\n")
        log.flush()
        sys.stdout.write(line + "\n")
        sys.stdout.flush()

    tool = {
        "name": "open_app",
        "description": "Open a desktop application and return its window "
                       "title.",
        "inputSchema": {"type": "object",
                        "properties": {"app": {"type": "string"}},
                        "required": ["app"]},
        "annotations": {"readOnlyHint": False, "destructiveHint": False}}
    while True:
        line = sys.stdin.readline()
        if not line:
            return
        log.write("< " + line)
        log.flush()
        msg = json.loads(line)
        method = msg.get("method")
        if method == "initialize":
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {
                "protocolVersion": msg["params"].get("protocolVersion",
                                                     "2025-06-18"),
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "fakepersist", "version": "0.0.1"}}})
        elif method == "tools/list":
            send({"jsonrpc": "2.0", "id": msg["id"],
                  "result": {"tools": [tool]}})
        elif method == "tools/call":
            app = msg["params"].get("arguments", {}).get("app", "?")
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {
                "content": [{"type": "text", "text":
                             f"{app} opened (simulated)"}],
                "isError": False}})
        elif method == "ping":
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {}})
        elif "id" in msg and method:
            send({"jsonrpc": "2.0", "id": msg["id"], "error": {
                "code": -32601, "message": "not found"}})


def dump(obj):
    return json.dumps(obj, ensure_ascii=False)


def snapshot(root):
    out = {}
    for dp, dn, fn in os.walk(root):
        for f in fn:
            p = os.path.join(dp, f)
            try:
                st = os.stat(p)
            except OSError:
                continue
            out[os.path.relpath(p, root)] = (st.st_mtime_ns, st.st_size)
    return out


def show_changes(before, label):
    after = snapshot(HOME)
    print(f"--- CODEX_HOME changes after {label} ---")
    for rel in sorted(after):
        if before.get(rel) == after[rel]:
            continue
        tag = "NEW" if rel not in before else "CHANGED"
        print(tag, rel, after[rel][1])
        low = rel.lower()
        if low.endswith((".toml", ".rules", ".json")) and \
                "session" not in low and after[rel][1] < 20000 and \
                not low.endswith("auth.json"):
            with open(os.path.join(HOME, rel), encoding="utf-8",
                      errors="replace") as fh:
                print("   | " + fh.read().replace("\n", "\n   | "))
    return after


def probe_main(mode):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from probe_codex_rpc import Client, text_input

    real_cfg = os.path.join(REAL_HOME, "config.toml")
    real_before = (os.stat(real_cfg).st_mtime_ns,
                   os.path.isdir(os.path.join(REAL_HOME, "rules")))

    if os.path.isdir(HOME):
        shutil.rmtree(HOME)
    os.makedirs(HOME)
    shutil.copy(os.path.join(REAL_HOME, "auth.json"), HOME)
    for d in (WORK_A, WORK_B):
        if os.path.isdir(d):
            shutil.rmtree(d)
        os.makedirs(d)
        subprocess.run(["git", "init", "-q", d], check=True)
    env = dict(os.environ)
    env["CODEX_HOME"] = HOME.replace("/", "\\")

    seen = []

    def on_request(c, msg):
        seen.append(msg)
        method = msg["method"]
        first = len([m for m in seen if m["method"] == method]) == 1
        if method == "mcpServer/elicitation/request":
            c.respond(msg["id"], {"action": "accept" if first else "decline",
                                  "content": {} if first else None,
                                  "_meta": {"persist": "always"}
                                  if first else None})
        elif method == "item/commandExecution/requestApproval":
            amend = msg["params"].get("proposedExecpolicyAmendment")
            if mode == "cmd-prefix" and amend:
                amend = amend[:2]
            if first and amend:
                c.respond(msg["id"], {"decision": {
                    "acceptWithExecpolicyAmendment": {
                        "execpolicy_amendment": amend}}})
            else:
                c.respond(msg["id"], {"decision": "decline"})
        elif method == "item/fileChange/requestApproval":
            c.respond(msg["id"], {"decision": "decline"})
        else:
            c.send({"id": msg["id"], "error": {
                "code": -32601, "message": f"probe does not handle {method}"}})

    args = []
    if mode.startswith("mcp"):
        py = sys.executable.replace("\\", "/")
        me = os.path.abspath(__file__).replace("\\", "/")
        if mode == "mcp":
            # Server len cez -c: Codex potom "always" nezapise ("failed to
            # persist MCP tool approval ... is not configured in config.toml").
            args = ["-c", f'mcp_servers.fakepersist.command="{py}"',
                    "-c", f'mcp_servers.fakepersist.args=["-I", "{me}", '
                          '"server"]']
        else:
            with open(HOME + "/config.toml", "w", encoding="utf-8") as fh:
                fh.write(f'[mcp_servers.fakepersist]\ncommand = "{py}"\n'
                         f'args = ["-I", "{me}", "server"]\n')
        policy, sandbox = "on-request", "read-only"

        def prompt(app):
            return (f"Call the fakepersist MCP tool open_app with app {app} "
                    "and report the result. Do not use the shell or any "
                    "other tool.")
        steps = [("A", 1, WORK_A, "Notepad"), ("B", 1, WORK_A, "Paint"),
                 ("C", 1, WORK_B, "Calculator"), ("D", 2, WORK_B, "WordPad")]
    else:
        policy, sandbox = "untrusted", "danger-full-access"

        def prompt(name):
            return (f"Run exactly this shell command once: git checkout -b "
                    f"{name}   Then report its output. Do not run any other "
                    "command and do not retry if it is rejected.")
        if mode == "cmd-same":
            # Ta ista vetva v novom vlakne (prikaz zlyha, ale schvalenie je
            # pred spustenim), v inom cwd a v novom procese.
            steps = [("A", 1, WORK_A, "alpha"), ("B", 1, WORK_A, "alpha"),
                     ("C", 1, WORK_B, "alpha"), ("D", 2, WORK_B, "alpha")]
        else:
            steps = [("A", 1, WORK_A, "alpha"), ("B", 1, WORK_A, "beta"),
                     ("C", 1, WORK_B, "gamma"), ("D", 2, WORK_B, "delta")]

    snap = snapshot(HOME)
    c = None
    proc = 0
    for label, p, cwd, arg in steps:
        if p != proc:
            if c:
                c.close()
                snap = show_changes(snap, f"close process {proc}")
            proc = p
            c = Client(f"persist-{mode}-{p}", extra_args=args,
                       on_request=on_request, cwd=cwd, env=env)
            c.initialize(experimental=True)
        r = c.request("thread/start", {
            "cwd": cwd, "model": MODEL, "approvalPolicy": policy,
            "sandbox": sandbox}, wait=60)
        if not r or "result" not in r:
            print("thread/start failed", dump(r))
            break
        tid = r["result"]["thread"]["id"]
        before = len(seen)
        t0 = len(c.notes)
        c.request("turn/start", {"threadId": tid, "effort": "low",
                                 "input": text_input(prompt(arg))})
        done = c.wait_method("turn/completed", 300, t0)
        time.sleep(1.0)
        print(f"=== step {label} (process {p}, cwd {cwd}, arg {arg}) "
              f"thread {tid}")
        print("turn completed:", dump(done)[:300])
        print("requests in step:", len(seen) - before)
        for m in seen[before:]:
            print("  REQUEST", m["method"], "id", m["id"])
            print("    ", dump(m["params"]))
        for _, m in c.notes[t0:]:
            if m.get("method") == "item/completed":
                it = m["params"]["item"]
                if it.get("type") in ("mcpToolCall", "commandExecution"):
                    print("  ITEM", dump(it)[:700])
        snap = show_changes(snap, f"step {label}")
    if c:
        c.close()
        show_changes(snap, f"close process {proc}")

    real_after = (os.stat(real_cfg).st_mtime_ns,
                  os.path.isdir(os.path.join(REAL_HOME, "rules")))
    print("real ~/.codex config.toml mtime / rules dir unchanged:",
          real_before == real_after, real_after)


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "cmd"
    if mode == "server":
        server_main()
    else:
        probe_main(mode)
