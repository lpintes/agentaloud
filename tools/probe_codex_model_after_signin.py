# Sonda: vlakno, ktore vzniklo pred prihlasenim, ma model pre API kluc
# (gpt-6.1-sol) a konto ChatGPT ho odmietne chybou 400.  Zistuje:
#   1. ktory model da model/list kontu ChatGPT ako predvoleny,
#   2. ze vlakno s gpt-6.1-sol naozaj zlyha a ako vyzera turn/completed,
#   3. ci ho zachrani pole model v turn/start (a ci plati aj pre dalsi tah).
#
# Potrebuje prihlaseny CODEX_HOME (argument), mina kredit na dva kratke tahy.
#   python tools/probe_codex_model_after_signin.py C:/b/noauth-ui/codex-home
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import os
import sys

from probe_codex_rpc import Client, text_input

HOME = sys.argv[1] if len(sys.argv) > 1 else "C:/b/noauth-ui/codex-home"
CWD = "C:/b/noauth-ui/proj"


def turn(c, thread, text, model=None):
    params = {"threadId": thread, "input": text_input(text)}
    if model:
        params["model"] = model
    start = len(c.notes)
    r = c.request("turn/start", params)
    print("turn/start", "model=" + str(model), "->",
          json.dumps(r)[:300])
    done = c.wait_method("turn/completed", 90, start)
    t = (done or {}).get("params", {}).get("turn", {})
    print("  status", t.get("status"), "error",
          json.dumps(t.get("error"))[:300])


def main():
    env = dict(os.environ, CODEX_HOME=HOME)
    c = Client("model-after-signin", cwd=CWD, env=env)
    c.initialize()
    acc = c.request("account/read", {})
    print("account/read", json.dumps(acc)[:300])
    models = c.request("model/list", {})
    for m in (models or {}).get("result", {}).get("data", []):
        print("  model", m.get("model") or m.get("id"),
              "default" if m.get("isDefault") else "")

    plain = c.request("thread/start", {"cwd": CWD})
    print("thread/start bez modelu -> model",
          plain.get("result", {}).get("model"))

    bad = c.request("thread/start", {"cwd": CWD, "model": "gpt-6.1-sol"})
    thread = bad["result"]["thread"]["id"]
    print("thread/start gpt-6.1-sol -> model", bad["result"].get("model"))
    turn(c, thread, "Say only: one")
    good = plain.get("result", {}).get("model")
    turn(c, thread, "Say only: two", model=good)
    turn(c, thread, "Say only: three")
    c.close()


if __name__ == "__main__":
    main()
