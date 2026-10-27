# Ako Codex preposiela klientovi app-servera MCP elicitaciu s polami formulara
# a elicitaciu v rezime "url" -- a co z odpovede klienta dostane MCP server.
#
# Falosny MCP server `fakeel` (python, stdio, cez -c mcp_servers.*, ~/.codex
# sa nemeni) ma jeden nastroj `collect`.  Jedno jeho volanie posle za sebou
# vsetky pripady ako `elicitation/create`; sprava kazdeho zacina "CASE <meno>:"
# a klient podla mena vyberie odpoved.  Jeden tah, jedno volanie nastroja.
#
#   probe_elicit_form.py run   cely beh, nastroj readOnlyHint=true
#   probe_elicit_form.py rw    cely beh, readOnlyHint=false (pyta aj Codex)
#   probe_elicit_form.py mcp   (interne) beh falosneho MCP servera
#   probe_elicit_form.py mcp app   server pre skusku v appke: jeden formular
#                              (SCHEMA) a jeden odkaz, odpoved v el-mcp.log.
#     agentaloud --backend codex <priecinok> --
#       -c mcp_servers.fakeel.command="<python>"
#       -c mcp_servers.fakeel.args=["<tento subor>", "mcp", "app"]
#
# Logy: C:/b/elicit-probe/logs/el-<rezim>.log (app-server) a
#       C:/b/elicit-probe/logs/el-mcp.log (MCP server, pripisuje sa).
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import sys
import time

LOGDIR = "C:/b/elicit-probe/logs"
MCP_LOG = LOGDIR + "/el-mcp.log"

SCHEMA = {
    "type": "object",
    "properties": {
        "name": {"type": "string", "title": "Name", "description": "Your name",
                 "minLength": 2, "maxLength": 20, "default": "Jan"},
        "email": {"type": "string", "title": "E-mail", "format": "email"},
        "age": {"type": "integer", "title": "Age", "minimum": 0,
                "maximum": 120},
        "ratio": {"type": "number", "title": "Ratio", "minimum": 0.5,
                  "maximum": 9.5, "default": 1.5},
        "subscribe": {"type": "boolean", "title": "Subscribe",
                      "default": True},
        "color": {"type": "string", "title": "Color",
                  "enum": ["red", "green", "blue"], "default": "green"},
        "size": {"type": "string", "title": "Size", "oneOf": [
            {"const": "s", "title": "Small"},
            {"const": "m", "title": "Medium"},
            {"const": "l", "title": "Large"}]},
        "legacy": {"type": "string", "title": "Legacy",
                   "enum": ["a", "b"], "enumNames": ["Option A", "Option B"]},
        "tags": {"type": "array", "title": "Tags", "minItems": 1,
                 "maxItems": 2, "items": {"type": "string",
                                          "enum": ["x", "y", "z"]},
                 "default": ["x"]},
    },
    "required": ["name", "age", "color"],
}

VALID = {"name": "Peter", "email": "p@example.com", "age": 5, "ratio": 2.25,
         "subscribe": False, "color": "blue", "size": "m", "legacy": "b",
         "tags": ["x", "z"]}

# meno pripadu -> odpoved klienta (vysledok na mcpServer/elicitation/request)
FORM_CASES = [
    ("valid", {"action": "accept", "content": VALID, "_meta": None}),
    ("missing_required", {"action": "accept",
                          "content": {"name": "Peter", "color": "red"},
                          "_meta": None}),
    ("wrong_type", {"action": "accept",
                    "content": dict(VALID, age="five"), "_meta": None}),
    ("out_of_range", {"action": "accept",
                      "content": dict(VALID, age=500, tags=["x", "y", "z"],
                                      name="P"), "_meta": None}),
    ("not_in_enum", {"action": "accept",
                     "content": dict(VALID, color="purple", size="xl",
                                     tags=["q"]), "_meta": None}),
    ("numbers_as_strings", {"action": "accept",
                            "content": dict(VALID, age="5", ratio="2.25",
                                            subscribe="true"),
                            "_meta": None}),
    ("extra_field", {"action": "accept",
                     "content": dict(VALID, unknown="zzz"), "_meta": None}),
    ("cancel", {"action": "cancel", "content": None, "_meta": None}),
    ("decline", {"action": "decline", "content": None, "_meta": None}),
]
URL_CASES = [
    ("url_accept", {"action": "accept", "content": None, "_meta": None}),
    ("url_decline", {"action": "decline", "content": None, "_meta": None}),
    ("url_cancel", {"action": "cancel", "content": None, "_meta": None}),
]
ANSWERS = dict(FORM_CASES + URL_CASES)


# --- falosny MCP server -----------------------------------------------------

def mcp_main(read_only, app=False):
    os.makedirs(LOGDIR, exist_ok=True)
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

    state = {"next": 1000}

    def elicit(params):
        state["next"] += 1
        eid = state["next"]
        send({"jsonrpc": "2.0", "id": eid, "method": "elicitation/create",
              "params": params})
        while True:
            r = read()
            if r is None:
                return None
            if r.get("id") == eid and "method" not in r:
                return r
            w("#", "ignored while waiting")

    w("#", "start pid %d" % os.getpid())
    while True:
        msg = read()
        if msg is None:
            w("#", "stdin EOF")
            return
        method = msg.get("method")
        if method == "initialize":
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {
                "protocolVersion": msg["params"].get("protocolVersion",
                                                     "2025-11-25"),
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "fakeel", "version": "0.0.1"}}})
        elif method == "tools/list":
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {"tools": [{
                "name": "collect",
                "description": "Collects user details via a form.",
                "inputSchema": {"type": "object", "properties": {}},
                "annotations": {"readOnlyHint": read_only}}]}})
        elif method == "tools/call":
            results = []
            form_cases, url_cases = FORM_CASES, URL_CASES
            if app:
                form_cases, url_cases = [("app", None)], [("url_app", None)]
            for name, _ in form_cases:
                r = elicit({"mode": "form",
                            "message": f"CASE {name}: please fill the form",
                            "requestedSchema": SCHEMA})
                if r is None:
                    return
                results.append(f"{name}: {json.dumps(r)}")
                w("#", f"RESULT {name}: {json.dumps(r)}")
            for i, (name, _) in enumerate(url_cases):
                elid = f"el-{name}"
                r = elicit({"mode": "url",
                            "message": f"CASE {name}: open the page",
                            "url": f"https://example.com/auth?case={name}",
                            "elicitationId": elid})
                if r is None:
                    return
                results.append(f"{name}: {json.dumps(r)}")
                w("#", f"RESULT {name}: {json.dumps(r)}")
                if name == "url_accept":
                    send({"jsonrpc": "2.0",
                          "method": "notifications/elicitation/complete",
                          "params": {"elicitationId": elid}})
                    time.sleep(1.0)
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {
                "content": [{"type": "text", "text": "done, %d cases"
                             % len(results)}], "isError": False}})
        elif method == "ping":
            send({"jsonrpc": "2.0", "id": msg["id"], "result": {}})
        elif "id" in msg and method:
            send({"jsonrpc": "2.0", "id": msg["id"], "error": {
                "code": -32601, "message": "not found"}})


# --- klient app-servera ------------------------------------------------------

def short(obj, n=3000):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + "..."


def probe_main(mode):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import probe_codex_rpc
    probe_codex_rpc.LOGDIR = LOGDIR
    from probe_codex_rpc import Client, REPO, fresh_repo, text_input

    seen = []

    def on_request(c, msg):
        seen.append(msg)
        method = msg["method"]
        params = msg.get("params") or {}
        if method != "mcpServer/elicitation/request":
            c.send({"id": msg["id"], "error": {
                "code": -32601, "message": f"probe does not handle {method}"}})
            return
        meta = params.get("_meta") or {}
        if meta.get("codex_approval_kind") == "mcp_tool_call":
            c.respond(msg["id"], {"action": "accept", "content": {},
                                  "_meta": None})
            return
        text = params.get("message", "")
        case = text[5:].split(":")[0] if text.startswith("CASE ") else None
        answer = ANSWERS.get(case)
        if answer is None:
            c.respond(msg["id"], {"action": "decline", "content": None,
                                  "_meta": None})
        else:
            c.respond(msg["id"], answer)

    fresh_repo()
    py = sys.executable.replace("\\", "/")
    me = os.path.abspath(__file__).replace("\\", "/")
    args = ["-c", f'mcp_servers.fakeel.command="{py}"',
            "-c", f'mcp_servers.fakeel.args=["{me}", "mcp", "{mode}"]']
    c = Client("el-" + mode, extra_args=args, on_request=on_request)
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
            "Call the collect tool from the fakeel MCP server exactly once, "
            "with no arguments, and quote its result verbatim. Do not use "
            "the shell or any other tool.")})
    done = c.wait_method("turn/completed", 300, start)
    print("turn/completed:", short(done, 600))
    time.sleep(1.0)
    print("--- requests from server ---")
    for m in seen:
        print(short(m))
    print("--- notifications (not item/agentMessage deltas) ---")
    for _, m in c.notes[start:]:
        meth = m.get("method", "")
        if "id" not in m and not meth.endswith("/delta") and \
                "Delta" not in meth:
            print(short(m, 600))
    c.close()


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "run"
    if mode == "mcp":
        sub = sys.argv[2] if len(sys.argv) > 2 else "run"
        mcp_main(sub != "rw", app=sub == "app")
    else:
        probe_main(mode)
