# account/rateLimits/read hned po initialize, bez tahu -- teda bez kreditu
# (claude-gui-lkk.44.19).
#
# Meria dve veci, ktore probe_codex_noturn.py nevidela: ci server berie
# "params": null (backend posiela params vzdy), a co odpovie odhlaseny ucet
# (prazdny CODEX_HOME, prostredie bez klucov ako v probe_noauth.py).
#
# Prieskumny nastroj, nie sucast produktu -- viz CLAUDE.md.

import json
import tempfile

from probe_codex_rpc import Client, fresh_repo
from probe_noauth import clean_env


def short(obj, n=900):
    s = json.dumps(obj, ensure_ascii=False)
    return s if len(s) <= n else s[:n] + "..."


def ask(name, env=None):
    c = Client(name, env=env)
    c.initialize()
    t = c.t()
    r = c.request("account/rateLimits/read", None)
    print(f"{name} bez params: {c.t() - t:.2f} s", short(r))
    # Tvar, ktory posiela CodexBackend::Request.
    c.send({"id": 99, "method": "account/rateLimits/read", "params": None})
    c.wait_for(lambda m: m.get("id") == 99, 30)
    print(f"{name} params null:", short(c.responses.get(99)))
    c.close()


def main():
    fresh_repo()
    ask("ratelimits_signedin")
    home = tempfile.mkdtemp(prefix="codex-home-")
    ask("ratelimits_signedout", clean_env(CODEX_HOME=home))


if __name__ == "__main__":
    main()
