# Nesie reasoning v `codex app-server` text?  Jeden tah s effort high
# a summary detailed na ulohe, pri ktorej model premyslat musi.  Pri effort
# low/medium na trivialnej ulohe prislo reasoningOutputTokens 0 a ziadny
# reasoning item, takze to odpoved nebola.
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import sys

from probe_codex_rpc import Client, REPO, fresh_repo, text_input


def main():
    effort = sys.argv[1] if len(sys.argv) > 1 else "high"
    fresh_repo()
    c = Client(f"reasoning-{effort}")
    c.initialize(experimental=True)
    r = c.request("thread/start", {"cwd": REPO, "model": "gpt-6-luna",
                                   "approvalPolicy": "untrusted",
                                   "sandbox": "read-only", "ephemeral": True})
    tid = r["result"]["thread"]["id"]
    start = len(c.notes)
    c.request("turn/start", {
        "threadId": tid, "effort": effort, "summary": "detailed",
        "input": text_input(
            "Bez nástrojov: nájdi najmenšie kladné celé číslo, ktoré dáva "
            "zvyšok 1 po delení 2, 3, 4, 5 aj 6 a je deliteľné 7. "
            "Odpovedz len číslom.")})
    done = c.wait_method("turn/completed", 180, start)
    print("turn/completed:", json.dumps(done, ensure_ascii=False)[:600])
    for _, m in c.notes[start:]:
        meth = m.get("method", "")
        if "reasoning" in meth or (meth.startswith("item/") and
                                   m["params"].get("item", {}).get("type")
                                   == "reasoning"):
            print(json.dumps(m, ensure_ascii=False)[:500])
    c.close()


if __name__ == "__main__":
    main()
