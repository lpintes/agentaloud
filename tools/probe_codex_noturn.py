# Co sa da z `codex app-server` zistit bez jedineho tahu -- teda bez kreditu.
#
# initialize, zoznam modelov, thread/start (kedy je id threadu zname a ci
# vznikne subor na disku este pred prvym tahom), skills, collaboration mody,
# rate limity, a ci sa da rezim povoleni zmenit za behu (thread/settings/update,
# experimentalne API).  Nakoniec zavrie stdin a odmeria, ci proces skonci sam.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import sys
import time

from probe_codex_rpc import Client, REPO, fresh_repo


def short(obj, n=600):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + "..."


def main():
    fresh_repo()
    c = Client("noturn")
    r = c.initialize(experimental=True)
    print("initialize:", short(r))
    for method, params in [
        ("model/list", {}),
        ("collaborationMode/list", {}),
        ("skills/list", {"cwds": [REPO]}),
        ("account/read", {}),
        ("account/rateLimits/read", None),
        ("permissionProfile/list", {}),
    ]:
        print(f"{method}:", short(c.request(method, params), 1500))

    r = c.request("thread/start", {"cwd": REPO, "approvalPolicy": "untrusted",
                                   "sandbox": "read-only"})
    print("thread/start:", short(r, 3000))
    tid = r["result"]["thread"]["id"]
    path = r["result"]["thread"].get("path")
    time.sleep(1.0)
    import os
    print("path exists after start:", path, path and os.path.exists(path))

    r = c.request("thread/settings/update",
                  {"threadId": tid, "approvalPolicy": "on-request",
                   "sandboxPolicy": {"type": "workspaceWrite",
                                     "writableRoots": [], "networkAccess": False,
                                     "excludeTmpdirEnvVar": False,
                                     "excludeSlashTmp": False}})
    print("thread/settings/update:", short(r, 2000))
    time.sleep(1.0)
    r = c.request("thread/read", {"threadId": tid, "includeTurns": False})
    print("thread/read:", short(r, 1500))
    r = c.request("thread/loaded/list", {})
    print("thread/loaded/list:", short(r))
    time.sleep(1.0)
    print("--- notifications ---")
    for t, m in c.notes:
        if "method" in m:
            print(f"{t:7.3f}", short(m, 400))
    c.close()


if __name__ == "__main__":
    main()
