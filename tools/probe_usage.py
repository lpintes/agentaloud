# Da sa zistit stav limitov planu bez poslania promptu?
#
# rate_limit_event chodi len pocas tahu, takze stavovy riadok ukazuje limit
# z posledneho tahu (claude-gui-lkk.5.8).  Binarka ma control_request
# 'get_usage' s volbou 'skip_behaviors' ("for callers that need only the plan
# rate limits, such as a usage meter").  Sonda posle 'initialize', potom
# 'get_usage' a vypise odpoved.  Ziadny prompt, takze to nestoji kredit.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import subprocess
import sys
import threading
import time

ARGS = [
    "claude", "-p", "--verbose",
    "--input-format", "stream-json",
    "--output-format", "stream-json",
    "--permission-prompt-tool", "stdio",
]

INITIALIZE = {
    "type": "control_request",
    "request_id": "init-0",
    "request": {"subtype": "initialize", "hooks": {}},
}


def usage(skip):
    request = {"subtype": "get_usage"}
    if skip is not None:
        request["skip_behaviors"] = skip
    return {"type": "control_request",
            "request_id": "usage-%s" % skip,
            "request": request}


def main():
    # probe_usage.py [sekundy] [dalsie prepinace pre claude...]
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    child = subprocess.Popen(
        ARGS + sys.argv[2:], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", shell=True)

    lines = []
    started = time.time()

    def read():
        for line in child.stdout:
            lines.append((time.time() - started, line))

    threading.Thread(target=read, daemon=True).start()
    for request in (INITIALIZE, usage(True), usage(None)):
        child.stdin.write(json.dumps(request) + "\n")
        child.stdin.flush()
        time.sleep(0.5)
    time.sleep(seconds)
    child.kill()

    for when, line in lines:
        try:
            record = json.loads(line)
        except ValueError:
            print("NEJSON:", line[:200].rstrip())
            continue
        response = record.get("response", {})
        rid = response.get("request_id", "")
        if record.get("type") != "control_response" or rid == "init-0":
            print("=== %5.1fs %s %s" % (when, record.get("type"),
                                       record.get("subtype", "")))
            continue
        print("=== %5.1fs %s" % (when, rid))
        print(json.dumps(record, indent=1, ensure_ascii=False)[:6000])
    if not lines:
        print("(nic neprislo)")


if __name__ == "__main__":
    main()
