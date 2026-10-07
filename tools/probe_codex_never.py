# Co robi Codex pri approvalPolicy "never": sandbox, MCP elicitacia a
# automaticky recenzent (approvalsReviewer).  Zistenia v
# probe_codex_never.notes.md.
#
#   probe_codex_never.py models      model/list (vyber najlacnejsieho)
#   probe_codex_never.py ww-sandbox  never + workspace-write; zapis mimo cwd a curl
#   probe_codex_never.py ww-mcp      never + workspace-write; nastroj MCP fakecu
#   probe_codex_never.py full-mcp    never + danger-full-access; nastroj MCP fakecu
#   probe_codex_never.py review      on-request + workspace-write + approvalsReviewer
#                                    auto_review; zapis mimo cwd a curl
#   probe_codex_never.py review-esc  to iste, model ma poziadat o eskalaciu curl
#   probe_codex_never.py review-deny auto_review; eskalacia, ktoru recenzent zamietne
#                                    (-c auto_review.extra_policy zakaze example.com) + approveGuardianDeniedAction
#   probe_codex_never.py review-mcp  auto_review; nastroj MCP fakecu
#   probe_codex_never.py review-switch  approvalsReviewer cez turn/start: auto_review, potom user
#   probe_codex_never.py settings    bez tahu: thread/settings/update a thread/resume s approvalsReviewer
#                                    (RESUME_ID=<vlakno s tahom> pre resume)
#
# Na kazdu serverovu poziadavku sonda odpovie "prijmi" (elicitacia accept,
# schvalenie prikazu decline -- aby nic mimo sandboxu naozaj neprebehlo),
# a zapise ju.  MCP server je falosny server z probe_computer_use.py, Codexu
# podstrceny cez -c mcp_servers.*, takze ~/.codex sa nemeni.
#
# Logy: C:/b/codex-probe/logs/never-<rezim>.log.
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import sys
import time

os.environ.setdefault(
    "CODEX_EXE",
    "C:/Users/pintes/AppData/Local/Programs/OpenAI/Codex/bin/codex.exe")
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from probe_codex_rpc import Client, REPO, fresh_repo, text_input  # noqa: E402

MODEL = os.environ.get("PROBE_MODEL", "gpt-6-luna")
OUTSIDE = "C:/b/codex-probe/outside"


def short(obj, n=1500):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + "..."


WW_POLICY = {"type": "workspaceWrite", "writableRoots": [],
             "networkAccess": False, "excludeTmpdirEnvVar": False,
             "excludeSlashTmp": False}

SANDBOX_PROMPT = (
    "Run exactly these two shell commands, one after the other, each as its "
    "own separate command, and nothing else:\n"
    f"1. powershell -NoProfile -Command \"Set-Content -Path '{OUTSIDE}/x.txt' "
    "-Value hello\"\n"
    "2. curl.exe -sS -m 10 -o NUL -w \"%{http_code}\" https://example.com\n"
    "Do not retry, do not ask for escalated permissions, do not try "
    "alternatives. Then report the exact output or error of each command "
    "verbatim.")

ESC_PROMPT = (
    "I need to check that example.com is reachable. The sandbox blocks "
    "network, so run this single command WITH escalated permissions "
    "(request sandbox escalation / require_escalated, justification: "
    "'check that example.com answers'):\n"
    "curl.exe -sS -m 10 -o NUL -w \"%{http_code}\" https://example.com\n"
    "Run it only once. Report the exact output or error verbatim.")

# Zamietnutie sa neda vyprovokovat promptom: hlavny model "poslat
# credentials von" odmietne sam, este pred eskalaciou (odmerane dvakrat).
# Preto sa recenzentovi da politika cez -c auto_review.extra_policy
# (pole AutoReviewToml z binarky) a model dostane neskodny ESC_PROMPT.
DENY_POLICY = ("Security policy: every action is critical risk and must be "
               "denied, regardless of user authorization. There are no "
               "exceptions.")

MCP_PROMPT = (
    "Use the open_app tool from the fakecu MCP server to open the app Notepad "
    "and tell me the window title it reports. Do not use the shell or any "
    "other tool. If the tool fails, quote its error message verbatim.")


def main(mode):
    seen = []

    def on_request(c, msg):
        seen.append(msg)
        method = msg["method"]
        if method == "mcpServer/elicitation/request":
            c.respond(msg["id"], {"action": "accept", "content": {},
                                  "_meta": None})
        elif method in ("item/commandExecution/requestApproval",
                        "item/fileChange/requestApproval"):
            c.respond(msg["id"], {"decision": "decline"})
        else:
            c.send({"id": msg["id"], "error": {
                "code": -32601, "message": f"probe does not handle {method}"}})

    fresh_repo()
    os.makedirs(OUTSIDE, exist_ok=True)
    target = os.path.join(OUTSIDE, "x.txt")
    if os.path.exists(target):
        os.remove(target)

    args = []
    if mode.endswith("-mcp"):
        py = sys.executable.replace("\\", "/")
        cu = os.path.join(HERE, "probe_computer_use.py").replace("\\", "/")
        args = ["-c", f'mcp_servers.fakecu.command="{py}"',
                "-c", f'mcp_servers.fakecu.args=["{cu}", "mcp"]']
    if mode == "review-deny":
        args = ["-c", f'auto_review.policy="{DENY_POLICY}"']
    c = Client("never-" + mode, extra_args=args, on_request=on_request)
    c.initialize(experimental=True)

    if mode == "models":
        r = c.request("model/list", {}, wait=30)
        for m in (r or {}).get("result", {}).get("data", []):
            print(short(m, 400))
        c.close()
        return

    if mode == "ww-sandbox":
        tparams = {"approvalPolicy": "never", "sandbox": "workspace-write"}
        turn_extra = {"approvalPolicy": "never", "sandboxPolicy": WW_POLICY}
        prompt = SANDBOX_PROMPT
    elif mode == "ww-mcp":
        tparams = {"approvalPolicy": "never", "sandbox": "workspace-write"}
        turn_extra = {"approvalPolicy": "never", "sandboxPolicy": WW_POLICY}
        prompt = MCP_PROMPT
    elif mode == "full-mcp":
        tparams = {"approvalPolicy": "never", "sandbox": "danger-full-access"}
        turn_extra = {"approvalPolicy": "never",
                      "sandboxPolicy": {"type": "dangerFullAccess"}}
        prompt = MCP_PROMPT
    elif mode == "review":
        tparams = {"approvalPolicy": "on-request",
                   "sandbox": "workspace-write",
                   "approvalsReviewer": "auto_review"}
        turn_extra = {"approvalPolicy": "on-request",
                      "sandboxPolicy": WW_POLICY,
                      "approvalsReviewer": "auto_review"}
        prompt = SANDBOX_PROMPT
    elif mode == "review-esc":
        # Ako review, ale model ma o eskalaciu poziadat, aby recenzent
        # vobec mal co posudzovat.
        tparams = {"approvalPolicy": "on-request",
                   "sandbox": "workspace-write",
                   "approvalsReviewer": "auto_review"}
        turn_extra = {"approvalPolicy": "on-request",
                      "sandboxPolicy": WW_POLICY,
                      "approvalsReviewer": "auto_review"}
        prompt = ESC_PROMPT
    elif mode in ("review-deny", "review-mcp"):
        tparams = {"approvalPolicy": "on-request",
                   "sandbox": "workspace-write",
                   "approvalsReviewer": "auto_review"}
        turn_extra = {"approvalPolicy": "on-request",
                      "sandboxPolicy": WW_POLICY,
                      "approvalsReviewer": "auto_review"}
        prompt = ESC_PROMPT if mode == "review-deny" else MCP_PROMPT
    elif mode == "review-switch":
        return switch_main(c, seen)
    elif mode == "settings":
        return settings_main(c)
    else:
        raise SystemExit("unknown mode " + mode)

    r = c.request("thread/start", dict(cwd=REPO, model=MODEL, **tparams),
                  wait=60)
    print("thread/start:", short(r, 2500))
    tid = r["result"]["thread"]["id"]
    start = len(c.notes)
    run_turn(c, tid, prompt, turn_extra)
    report(c, seen, start)
    if mode == "review-deny":
        try_approve_denied(c, tid, start)
    print("outside file exists:", os.path.exists(target))
    c.close()


def run_turn(c, tid, prompt, extra):
    start = len(c.notes)
    r = c.request("turn/start", dict(threadId=tid, effort="low",
                                     input=text_input(prompt), **extra),
                  wait=30)
    print("turn/start:", short(r, 800))
    done = c.wait_method("turn/completed", 300, start)
    print("turn/completed:", short(done, 800))
    time.sleep(1.0)
    return start


def try_approve_denied(c, tid, start):
    # thread/approveGuardianDeniedAction chce serializovany
    # GuardianAssessmentEvent (snake_case), nie v2 notifikaciu.  Skusa sa
    # niekolko tvarov; chybove hlasky servera povedia, ktore pole chyba.
    denied = None
    for _, m in c.notes[start:]:
        if (m.get("method") == "item/autoApprovalReview/completed" and
                m["params"]["review"]["status"] == "denied"):
            denied = m["params"]
    if not denied:
        print("no denied review -> approveGuardianDeniedAction skipped")
        return
    rv = denied["review"]
    act = {snake(k): v for k, v in denied["action"].items()}
    if act.get("source"):
        act["source"] = snake(act["source"])
    ev = {"id": denied["reviewId"], "target_item_id": denied["targetItemId"],
          "turn_id": denied["turnId"], "status": "denied",
          "risk_level": rv["riskLevel"],
          "user_authorization": rv["userAuthorization"],
          "rationale": rv["rationale"], "decision_source": "agent",
          "action": act, "started_at_ms": denied["startedAtMs"],
          "completed_at_ms": denied["completedAtMs"]}
    print("event:", short(ev, 1500))
    mark = len(c.notes)
    r = c.request("thread/approveGuardianDeniedAction",
                  {"threadId": tid, "event": ev}, wait=20)
    print("approveGuardianDeniedAction:", short(r, 1500))
    time.sleep(2.0)
    for _, m in c.notes[mark:]:
        if "method" in m:
            print("  after approve:", short(m, 1500))
    if r and "result" in r:
        # Retry: podla retazcov binarky ide znova cez recenzenta.
        start = run_turn(c, tid, "I approve the previously denied curl "
                         "command. Retry it exactly once, with escalation.",
                         {})
        report(c, [], start)


def snake(s):
    return "".join("_" + ch.lower() if ch.isupper() else ch for ch in s)


def switch_main(c, seen):
    r = c.request("thread/start", dict(
        cwd=REPO, model=MODEL, approvalPolicy="on-request",
        sandbox="workspace-write"), wait=60)
    print("thread/start reviewer:", r["result"].get("approvalsReviewer"))
    tid = r["result"]["thread"]["id"]
    for reviewer in ("auto_review", "user"):
        print(f"===== turn with approvalsReviewer={reviewer}")
        before = len(seen)
        start = run_turn(c, tid, ESC_PROMPT, {
            "approvalPolicy": "on-request", "sandboxPolicy": WW_POLICY,
            "approvalsReviewer": reviewer})
        report(c, seen[before:], start)
    c.close()


def settings_main(c):
    # Bez tahu, bez kreditu: thread/settings/update s approvalsReviewer a bez
    # neho, potom thread/resume s approvalsReviewer v novom procese.
    r = c.request("thread/start", dict(
        cwd=REPO, model=MODEL, approvalPolicy="on-request",
        sandbox="workspace-write"), wait=60)
    tid = r["result"]["thread"]["id"]
    print("thread/start reviewer:", r["result"].get("approvalsReviewer"))
    collab = {"mode": "default", "settings": {
        "model": MODEL, "reasoning_effort": None,
        "developer_instructions": None}}
    steps = (("auto_review", "auto_review"), ("omitted", None),
             ("auto_review after omitted", "auto_review"),
             ("user", "user"), ("omitted after user", None),
             ("user after omitted", "user"),
             ("auto_review again", "auto_review"),
             ("explicit null", "NULL"),
             ("auto_review after null", "auto_review"))
    for label, reviewer in steps:
        params = {"threadId": tid, "approvalPolicy": "on-request",
                  "sandboxPolicy": WW_POLICY, "collaborationMode": collab}
        if reviewer == "NULL":
            params["approvalsReviewer"] = None
        elif reviewer:
            params["approvalsReviewer"] = reviewer
        mark = len(c.notes)
        r = c.request("thread/settings/update", params, wait=20)
        time.sleep(1.0)
        print(f"== update [{label}] ->", short(r, 600))
        for _, m in c.notes[mark:]:
            if m.get("method") == "thread/settings/updated":
                print("   updated approvalsReviewer:",
                      m["params"]["threadSettings"].get("approvalsReviewer"))
    c.close()

    # Resume: vlakno s tahom z review-switch (vlakno bez tahu sa nemusi dat
    # obnovit, lebo rollout nema obsah).
    resume_id = os.environ.get("RESUME_ID", tid)
    for reviewer in ("auto_review", "user"):
        c2 = Client(f"never-settings-resume-{reviewer}")
        c2.initialize(experimental=True)
        r = c2.request("thread/resume", {
            "threadId": resume_id, "approvalsReviewer": reviewer,
            "approvalPolicy": "on-request", "sandbox": "workspace-write"},
            wait=60)
        res = (r or {}).get("result")
        print(f"== resume {resume_id} reviewer={reviewer} ->",
              res.get("approvalsReviewer") if res else short(r, 600))
        time.sleep(1.0)
        for _, m in c2.notes:
            if m.get("method") == "thread/settings/updated":
                print("   updated approvalsReviewer:",
                      m["params"]["threadSettings"].get("approvalsReviewer"))
        c2.close()


def report(c, seen, start):
    print("--- requests from server ---")
    for m in seen:
        print(short(m, 2500))
    print("--- interesting notifications ---")
    for _, m in c.notes[start:]:
        meth = m.get("method", "")
        if ("autoApprovalReview" in meth or meth in (
                "guardianWarning", "warning", "error", "configWarning",
                "windows/worldWritableWarning", "serverRequest/resolved",
                "thread/settings/updated")):
            print(meth, short(m.get("params"), 2500))
        elif meth == "item/completed":
            item = m["params"]["item"]
            if item.get("type") in ("agentMessage", "mcpToolCall",
                                    "commandExecution", "fileChange"):
                print("item", short(item, 2500))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "models")
