# Ako sa computer use v Codexe pyta na povolenie -- a co sa stane, ked klient
# serverovu poziadavku nepozna.
#
# Skutocny plugin computer-use@openai-bundled sa bez desktopovej appky Codex
# (instalator) pridat neda, takze sonda ho nahradza falosnym MCP serverom,
# ktory sa sprava rovnako ako pluginovy: pri volani nastroja si od klienta
# vypyta suhlas MCP elicitaciou a bez neho povie "not approved".  Nic
# neotvara a na nic neklika -- titulok okna si vymysli.  Server sa Codexu
# podstrci cez `-c mcp_servers.fakecu...`, teda bez zapisu do ~/.codex.
#
#   probe_computer_use.py refuse   na neznamu poziadavku -32601 (ako appka)
#   probe_computer_use.py accept   elicitaciu aj schvalenie nastroja prijme
#   probe_computer_use.py decline  elicitaciu odmietne action=decline
#   probe_computer_use.py refuse2  schvalenie nastroja prijme, elicitaciu pluginu -32601
#   probe_computer_use.py session  schvalenie s _meta.persist=session, dve volania
#   probe_computer_use.py mcp      (interne) beh falosneho MCP servera
#
# Logy: C:/b/codex-probe/logs/cu-<rezim>.log (app-server) a
#       C:/b/codex-probe/logs/cu-mcp.log (MCP server, pripisuje sa).
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import sys
import time

MCP_LOG = "C:/b/codex-probe/logs/cu-mcp.log"


# --- falosny MCP server -----------------------------------------------------

def mcp_main():
    os.makedirs(os.path.dirname(MCP_LOG), exist_ok=True)
    log = open(MCP_LOG, "a", encoding="utf-8")
    t0 = time.time()

    def w(tag, text):
        log.write(f"{time.time() - t0:8.3f} {tag} {text}\n")
        log.flush()

    def send(obj):
        line = json.dumps(obj, ensure_ascii=False)
        w(">", line)
        sys.stdout.write(line + "\n")
        sys.stdout.flush()

    def read():
        line = sys.stdin.readline()
        if not line:
            return None
        w("<", line.rstrip("\r\n"))
        return json.loads(line)

    w("#", "start pid %d" % os.getpid())
    client_caps = {}
    next_id = 1000
    while True:
        msg = read()
        if msg is None:
            w("#", "stdin EOF")
            return
        method = msg.get("method")
        if method == "initialize":
            client_caps = msg["params"].get("capabilities", {})
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {
                "protocolVersion": msg["params"].get("protocolVersion",
                                                     "2025-06-18"),
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "fakecu", "version": "0.0.1"}}})
        elif method == "tools/list":
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {"tools": [{
                "name": "open_app",
                "description": "Computer use: open a desktop application "
                               "and return its window title.",
                "inputSchema": {"type": "object",
                                "properties": {"app": {"type": "string"}},
                                "required": ["app"]},
                "annotations": {"readOnlyHint": False,
                                "destructiveHint": False}}]}})
        elif method == "tools/call":
            app = msg["params"].get("arguments", {}).get("app", "?")
            approved = False
            note = ""
            if "elicitation" not in client_caps:
                note = "client did not declare elicitation capability"
            else:
                next_id += 1
                eid = next_id
                send({"jsonrpc": "2.0", "id": eid,
                      "method": "elicitation/create", "params": {
                          "message": f"Allow Codex to use {app}?",
                          "requestedSchema": {"type": "object",
                                              "properties": {}}}})
                while True:
                    r = read()
                    if r is None:
                        return
                    if r.get("id") == eid and "method" not in r:
                        break
                    w("#", "ignored while waiting")
                if "result" in r:
                    approved = r["result"].get("action") == "accept"
                    note = "action=" + str(r["result"].get("action"))
                else:
                    note = "error " + json.dumps(r.get("error"))
            w("#", f"approved={approved} {note}")
            if approved:
                content = [{"type": "text", "text":
                            f"{app} opened. Window title: "
                            f"'Untitled - {app}' (simulated)"}]
            else:
                content = [{"type": "text", "text":
                            f"{app} is not approved for computer use "
                            f"({note})."}]
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {
                "content": content, "isError": not approved}})
        elif method == "ping":
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {}})
        elif "id" in msg and method:
            send({"jsonrpc": "2.0", "id": msg["id"], "error": {
                "code": -32601, "message": "not found"}})


# --- klient app-servera ------------------------------------------------------

KNOWN = ("item/commandExecution/requestApproval",
         "item/fileChange/requestApproval", "item/tool/requestUserInput")


def short(obj, n=1500):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + "..."


def probe_main(mode):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from probe_codex_rpc import Client, REPO, fresh_repo, text_input

    seen = []

    def on_request(c, msg):
        seen.append(msg)
        method = msg["method"]
        if method in KNOWN:
            # Ako appka by sa pytala cloveka; tu to nema nastat.
            c.respond(msg["id"], {"decision": "decline"})
            return
        meta = (msg.get("params") or {}).get("_meta") or {}
        tool_approval = meta.get("codex_approval_kind") == "mcp_tool_call"
        if mode in ("refuse2", "session") and tool_approval:
            c.respond(msg["id"], {"action": "accept", "content": {},
                                  "_meta": {"persist": "session"}
                                  if mode == "session" else None})
            return
        if mode in ("refuse", "refuse2"):
            c.send({"id": msg["id"], "error": {
                "code": -32601,
                "message": f"AgentAloud does not handle {method}"}})
        elif method == "mcpServer/elicitation/request":
            if mode in ("accept", "session"):
                c.respond(msg["id"], {"action": "accept", "content": {},
                                      "_meta": None})
            else:
                c.respond(msg["id"], {"action": "decline", "content": None,
                                      "_meta": None})
        else:
            c.send({"id": msg["id"], "error": {
                "code": -32601, "message": f"probe does not handle {method}"}})

    fresh_repo()
    py = sys.executable.replace("\\", "/")
    me = os.path.abspath(__file__).replace("\\", "/")
    args = ["-c", f'mcp_servers.fakecu.command="{py}"',
            "-c", f'mcp_servers.fakecu.args=["{me}", "mcp"]']
    c = Client("cu-" + mode, extra_args=args, on_request=on_request)
    c.initialize(experimental=True)
    r = c.request("thread/start", {
        "cwd": REPO, "model": "gpt-6-luna", "approvalPolicy": "on-request",
        "sandbox": "read-only"}, wait=60)
    tid = r["result"]["thread"]["id"]
    print("thread", tid)
    start = len(c.notes)
    c.request("turn/start", {
        "threadId": tid, "effort": "low",
        "input": text_input(
            ("Call the open_app tool from the fakecu MCP server twice, one "
             "call after the other: first with app Notepad, then with app "
             "Calculator. Tell me both window titles it reports. Do not "
             if mode == "session" else
             "Use the open_app tool from the fakecu MCP server to open the "
             "app Notepad and tell me the window title it reports. Do not ") +
            "use the shell or any other tool. If the tool fails, quote its "
            "error message verbatim.")})
    done = c.wait_method("turn/completed", 240, start)
    print("turn/completed:", short(done, 600))
    time.sleep(1.0)
    print("--- requests from server ---")
    for m in seen:
        print(short(m))
    print("--- agent messages / mcp items ---")
    for _, m in c.notes[start:]:
        if m.get("method") == "item/completed":
            item = m["params"]["item"]
            if item.get("type") in ("agentMessage", "mcpToolCall"):
                print(short(item, 1200))
    c.close()


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "refuse"
    if mode == "mcp":
        mcp_main()
    else:
        probe_main(mode)
