# Kam siaha "povolit na session" v `codex app-server`.
#
#   probe_codex_scope.py mcp       MCP: nastroj A (open_app) odsuhlaseny s
#                                  _meta.persist=session, potom A znova a nastroj
#                                  B (close_app) toho isteho servera
#   probe_codex_scope.py file      fileChange: acceptForSession na a.txt, potom
#                                  b.txt, sub/c.txt a subor mimo workspace
#                                  (untrusted + workspace-write)
#   probe_codex_scope.py file-ro   to iste s on-request + read-only
#   probe_codex_scope.py file-same a.txt dvakrat, potom b.txt
#   probe_codex_scope.py file-or   on-request + workspace-write
#   probe_codex_scope.py multi     jeden patch s a.txt, b.txt, sub/c.txt
#                                  (acceptForSession), potom kazdy zvlast, d.txt;
#                                  druhy tah: a.txt, sub/c.txt, e.txt
#   probe_codex_scope.py mcp-turns open_app session v 1. tahu, open_app v 2.
#   probe_codex_scope.py server   (interne) falosny MCP server
#
# Falosny server `fakescope` ma dva nastroje a SAM sa nepyta (ziadna elicitacia
# servera), takze kazda mcpServer/elicitation/request je schvalenie Codexom.
# Kazda dalsia ziadost po prvej sa odsuhlasi bez persist -- ak pride, znamena
# to, ze "session" ju nepokryla.
#
# Logy: C:/b/codex-probe/logs/scope-<rezim>.log, scope-mcp.log.
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import shutil
import subprocess
import sys
import time

BASE = "C:/b/codex-probe"
MCP_LOG = BASE + "/logs/scope-mcp.log"
WORK = BASE + "/scope-repo"
OUTSIDE = BASE + "/scope-outside"
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

    tool = lambda name, desc: {
        "name": name, "description": desc,
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
                "serverInfo": {"name": "fakescope", "version": "0.0.1"}}})
        elif method == "tools/list":
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {"tools": [
                tool("open_app", "Open a desktop application and return its "
                                 "window title."),
                tool("close_app", "Close a desktop application.")]}})
        elif method == "tools/call":
            p = msg["params"]
            app = p.get("arguments", {}).get("app", "?")
            text = (f"{app} opened, window 'Untitled - {app}' (simulated)"
                    if p["name"] == "open_app"
                    else f"{app} closed (simulated)")
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {
                "content": [{"type": "text", "text": text}],
                "isError": False}})
        elif method == "ping":
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {}})
        elif "id" in msg and method:
            send({"jsonrpc": "2.0", "id": msg["id"], "error": {
                "code": -32601, "message": "not found"}})


def dump(obj):
    return json.dumps(obj, ensure_ascii=False)


def probe_main(mode):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from probe_codex_rpc import Client, text_input

    seen = []

    def on_request(c, msg):
        seen.append(msg)
        method = msg["method"]
        first = len([m for m in seen if m["method"] == method]) == 1
        if method == "mcpServer/elicitation/request":
            c.respond(msg["id"], {"action": "accept", "content": {},
                                  "_meta": {"persist": "session"}
                                  if first else None})
        elif method == "item/fileChange/requestApproval":
            c.respond(msg["id"], {"decision": "acceptForSession"
                                  if first else "accept"})
        elif method == "item/commandExecution/requestApproval":
            c.respond(msg["id"], {"decision": "decline"})
        else:
            c.send({"id": msg["id"], "error": {
                "code": -32601, "message": f"probe does not handle {method}"}})

    for d in (WORK, OUTSIDE):
        if os.path.isdir(d):
            shutil.rmtree(d)
    os.makedirs(WORK)
    os.makedirs(OUTSIDE)
    subprocess.run(["git", "init", "-q", WORK], check=True)

    args = []
    if mode.startswith("mcp"):
        py =sys.executable.replace("\\", "/")
        me = os.path.abspath(__file__).replace("\\", "/")
        args = ["-c", f'mcp_servers.fakescope.command="{py}"',
                "-c", f'mcp_servers.fakescope.args=["-I", "{me}", "server"]']
    c = Client("scope-" + mode, extra_args=args, on_request=on_request,
               cwd=WORK)
    c.initialize(experimental=True)
    if mode == "mcp":
        policy, sandbox = "on-request", "read-only"
        prompt = ("Using only the fakescope MCP server tools, make exactly "
                  "these three calls one after the other, each as a separate "
                  "tool call: 1) open_app with app Notepad, 2) open_app with "
                  "app Calculator, 3) close_app with app Notepad. Then report "
                  "each result. Do not use the shell or any other tool.")
    elif mode == "mcp-turns":
        policy, sandbox = "on-request", "read-only"
        prompt = ["Call the fakescope MCP tool open_app with app Notepad and "
                  "report the result. Do not use the shell or any other tool.",
                  "Call the fakescope MCP tool open_app with app Paint and "
                  "report the result. Do not use the shell or any other tool."]
    elif mode == "multi":
        policy, sandbox = "untrusted", "workspace-write"
        prompt = [("Use the apply_patch tool (never the shell). FIRST make "
                   "ONE SINGLE apply_patch call whose patch contains three "
                   "'*** Add File' sections at once: a.txt, b.txt and "
                   "sub/c.txt, each with one line: hello. It must be one "
                   "tool call, not three. AFTER that, make these SEPARATE "
                   "apply_patch calls, one file per call, in order: update "
                   "a.txt (add line: two), update b.txt (add line: two), "
                   "update sub/c.txt (add line: two), create d.txt in the "
                   "current directory with one line: hello. If a call is "
                   "rejected, continue with the next one. Do not run any "
                   "shell command."),
                  ("Use the apply_patch tool (never the shell), each step a "
                   "SEPARATE apply_patch call, in order: update a.txt (add "
                   "line: three), update sub/c.txt (add line: three), create "
                   "e.txt with one line: hello. If a call is rejected, "
                   "continue with the next one. Do not run any shell "
                   "command.")]
    elif mode == "file-same":
        policy, sandbox = "untrusted", "workspace-write"
        prompt = ("Use the apply_patch tool (never the shell), each step a "
                  "SEPARATE apply_patch call, in this order: 1) create a.txt "
                  "with one line: hello, 2) update a.txt by adding a second "
                  "line: world, 3) create b.txt with one line: hello. If a "
                  "call is rejected, continue with the next one. Do not run "
                  "any shell command.")
    else:
        policy, sandbox = {"file": ("untrusted", "workspace-write"),
                           "file-ro": ("on-request", "read-only"),
                           "file-or": ("on-request", "workspace-write")}[mode]
        out = OUTSIDE.replace("/", "\\") + "\\d.txt"
        prompt = ("Use the apply_patch tool (never the shell) to create four "
                  "files, each with a SEPARATE apply_patch call, in this "
                  "order: 1) a.txt in the current directory, 2) b.txt in the "
                  "current directory, 3) sub/c.txt (create the subdirectory), "
                  f"4) {out} (absolute path, outside the workspace). Each file "
                  "contains one line: hello. If a call is rejected, continue "
                  "with the next one. Then list which files were created.")
    r = c.request("thread/start", {
        "cwd": WORK, "model": MODEL, "approvalPolicy": policy,
        "sandbox": sandbox}, wait=60)
    tid = r["result"]["thread"]["id"]
    print("thread", tid)
    start = len(c.notes)
    prompts = prompt if isinstance(prompt, list) else [prompt]
    for n, p in enumerate(prompts, 1):
        before = len(seen)
        t0 = len(c.notes)
        c.request("turn/start", {"threadId": tid, "effort": "low",
                                 "input": text_input(p)})
        done = c.wait_method("turn/completed", 300, t0)
        print(f"turn {n} completed:", dump(done)[:300])
        print(f"   requests in turn {n}:", len(seen) - before)
        time.sleep(1.0)
    print("--- item/started fileChange (verbatim changes) ---")
    for _, m in c.notes[start:]:
        if m.get("method") == "item/started" and \
                m["params"]["item"].get("type") == "fileChange":
            it = m["params"]["item"]
            print(it["id"], dump(it["changes"]))

    items = {}
    for _, m in c.notes[start:]:
        if m.get("method") in ("item/started", "item/completed"):
            it = m["params"]["item"]
            items[it["id"]] = it
    print("--- requests from server (full params) ---")
    for m in seen:
        print(m["method"], "id", m["id"])
        print("  ", dump(m["params"]))
        it = items.get(m["params"].get("itemId"))
        if it:
            print("   item:", dump({k: it.get(k) for k in
                                    ("type", "status", "changes")})[:800])
    print("--- tool / file items ---")
    for _, m in c.notes[start:]:
        if m.get("method") == "item/completed":
            it = m["params"]["item"]
            if it.get("type") in ("mcpToolCall", "fileChange",
                                  "commandExecution", "agentMessage"):
                print(dump(it)[:900])
    print("--- files ---")
    for root in (WORK, OUTSIDE):
        for dp, dn, fn in os.walk(root):
            if ".git" in dp:
                continue
            for f in fn:
                print(os.path.join(dp, f))
    c.close()


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "mcp"
    if mode == "server":
        server_main()
    else:
        probe_main(mode)
