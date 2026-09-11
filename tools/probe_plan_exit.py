# Co CLI povie o rezime a o modeli, ked session opusti plan sama -- schvalenim
# ExitPlanMode, nie nasim set_permission_mode (claude-gui-lkk.40 a .41).
#
# Tri otazky:
#   1. Pride po schvaleni ExitPlanMode 'system/status' s novym permissionMode?
#      Ak ano, appka sa o zmene dozvie bez hadania; ak nie, bar zostane na plane.
#   2. Ktory model naozaj odpoveda pri --model opusplan pred a po?  system/init
#      nesie 'hlavny model' (pri opusplan Sonnet), assistant zaznamy ten skutocny.
#   3. DO AKEHO rezimu sa po ExitPlanMode vrati?  V binarke je to prePlanMode,
#      teda rezim, z ktoreho sa do plan vstupilo -- a ten zavisi od cesty:
#      priamo z auto, alebo cez Shift+Tab cyklus default -> acceptEdits -> plan.
#
# POSIELA PROMPTY, takze to stoji kredit -- dva kratke tahy, prvy na Opuse.
# Prieskumny nastroj, nie sucast produktu (viz CLAUDE.md).
#
# Pouzitie:
#   python tools/probe_plan_exit.py [sekundy] [startovaci-rezim] [rezim,rezim,...]
#
#   ... 300                                  start rovno v plan
#   ... 300 auto plan                        z auto priamo do plan
#   ... 300 auto default,acceptEdits,plan    z auto cestou Shift+Tabu
#
# Prepnutia sa poslu ako set_permission_mode po initialize, pred promptom.
#
# Na can_use_tool pre ExitPlanMode sa odpovie allow s nezmenenym vstupom; na
# vsetko ostatne deny, nech druhy tah nic nezapise.  Vypisuje sa len to, co sa
# tyka rezimu a modelu, v poradi prichodu.

import json
import subprocess
import sys
import tempfile
import threading
import time

ARGS = [
    "claude", "-p", "--verbose",
    "--input-format", "stream-json",
    "--output-format", "stream-json",
    "--permission-prompt-tool", "stdio",
    "--model", "opusplan",
]

FIRST = (
    "Navrhni kratky plan: vytvorit subor hello.txt s textom ahoj. Nic necitaj "
    "ani nehladaj, plan napis rovno a hned zavolaj ExitPlanMode."
)
SECOND = "Povedz len jedno slovo: hotovo. Ziadny nastroj nevolaj."


def send(child, value):
    child.stdin.write(json.dumps(value) + "\n")
    child.stdin.flush()


def prompt(text):
    return {"type": "user",
            "message": {"role": "user",
                        "content": [{"type": "text", "text": text}]}}


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 240.0
    start_mode = sys.argv[2] if len(sys.argv) > 2 else "plan"
    switches = [m for m in sys.argv[3].split(",") if m] if len(sys.argv) > 3 else []
    workdir = tempfile.mkdtemp(prefix="probe_plan_exit_")
    child = subprocess.Popen(
        ARGS + ["--permission-mode", start_mode], cwd=workdir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", shell=True)
    started = time.time()
    results = []

    def say(*parts):
        print("%6.1fs" % (time.time() - started), *parts, flush=True)

    def read():
        for line in child.stdout:
            try:
                record = json.loads(line)
            except ValueError:
                continue
            kind = record.get("type")
            subtype = record.get("subtype")
            if kind == "system" and subtype in ("init", "status"):
                if "permissionMode" in record or subtype == "init":
                    say("system/%s" % subtype,
                        "permissionMode=%s" % record.get("permissionMode"),
                        "model=%s" % record.get("model", "-"),
                        "status=%s" % record.get("status", "-"))
            elif kind == "assistant":
                message = record.get("message") or {}
                parts = [p.get("type") + (":" + p.get("name", "")
                                          if p.get("type") == "tool_use" else "")
                         for p in message.get("content") or []]
                say("assistant", "model=%s" % message.get("model"),
                    "parent=%s" % record.get("parent_tool_use_id"), parts)
            elif kind == "control_request":
                request = record.get("request") or {}
                name = request.get("tool_name")
                allow = name == "ExitPlanMode"
                say("can_use_tool", name, "-> allow" if allow else "-> deny")
                if allow:
                    body = {"behavior": "allow",
                            "updatedInput": request.get("input") or {}}
                else:
                    body = {"behavior": "deny", "message": "probe: nie"}
                send(child, {"type": "control_response",
                             "response": {"subtype": "success",
                                          "request_id": record.get("request_id"),
                                          "response": body}})
            elif kind == "control_response":
                outer = record.get("response") or {}
                inner = outer.get("response") or {}
                if "current_permission_mode" in inner:
                    say("initialize", inner["current_permission_mode"])
                elif str(outer.get("request_id", "")).startswith("mode-"):
                    say("set_permission_mode", outer.get("request_id"),
                        outer.get("subtype"), inner.get("mode"))
            elif kind == "result":
                usage = record.get("modelUsage") or {}
                say("result", record.get("subtype"),
                    "modelUsage=%s" % sorted(usage.keys()),
                    "cost=%s" % record.get("total_cost_usd"))
                results.append(record)

    threading.Thread(target=read, daemon=True).start()
    send(child, {"type": "control_request", "request_id": "init-1",
                 "request": {"subtype": "initialize", "hooks": {}}})
    for number, mode in enumerate(switches, 1):
        # Rozostup ako pri ludskom Shift+Tabe; na odpoved sa necaka, CLI
        # poziadavky spracuje v poradi.
        time.sleep(1.0)
        send(child, {"type": "control_request", "request_id": "mode-%d" % number,
                     "request": {"subtype": "set_permission_mode", "mode": mode}})
    if switches:
        time.sleep(2.0)
    send(child, prompt(FIRST))

    deadline = started + seconds
    while time.time() < deadline and len(results) < 1:
        time.sleep(0.5)
    if results:
        send(child, prompt(SECOND))
    while time.time() < deadline and len(results) < 2:
        time.sleep(0.5)
    child.stdin.close()
    try:
        child.wait(30)
    except subprocess.TimeoutExpired:
        child.kill()
    print("workdir:", workdir)


if __name__ == "__main__":
    main()
