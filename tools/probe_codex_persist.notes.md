# Sonda: trvale povolenie v Codexe (execpolicy amendment, MCP persist always)

codex 0.166.0 (natívny). Sonda tools/probe_codex_persist.py. Logy C:/b/codex-probe/logs/persist-*.log,
MCP server C:/b/codex-probe/logs/persist-mcp.log. CODEX_HOME = C:/b/codex-probe/home-persist
(kopia len auth.json, pred kazdym behom sa zmaze a vytvori znova). Pracovne priecinky
C:/b/codex-probe/persist-a, persist-b (git repa, smu sa mazat). Skutocny ~/.codex sa nemeni:
snimka mtime v C:/b/codex-probe/realhome-before.txt (pozor, appka/codex pouzivatela tam
pise logs_2.sqlite* a pod. sam -- porovnavat config.toml, rules/, nie sqlite).

Schema (codex app-server generate-json-schema, C:/b/codex-probe/schema166):
- params prikazu: proposedExecpolicyAmendment: string[] | null (plus command, cwd, reason,
  commandActions, proposedNetworkPolicyAmendments, availableDecisions? -- overit naostro)
- odpoved: {"decision": {"acceptWithExecpolicyAmendment": {"execpolicy_amendment": [..]}}}

## Plan
Rezim cmd: proces 1: vlakno A (persist-a) prikaz -> accept s amendment; vlakno B (persist-a)
ten isty prikaz s inymi argumentmi; vlakno C (persist-b) to iste. Proces 2: vlakno D (persist-b).
Po kazdom kroku snimka CODEX_HOME (novy/zmeneny subor + obsah).
Rezim mcp: to iste s fake MCP serverom a _meta.persist=always.
Politika untrusted + danger-full-access (bez sandboxu Windows, ten by v prazdnom CODEX_HOME
potreboval setup).

## Stav
- [x] cmd (persist-cmd.out, logy persist-cmd-1/2.log) -- untrusted + danger-full-access
  Ziadost A (git checkout -b alpha):
  {"kind":"command","threadId":..,"turnId":..,"itemId":"exec-..","startedAtMs":..,
   "environmentId":"local","command":"\"C:\\\\Program Files\\\\PowerShell\\\\7\\\\pwsh.exe\" -NoProfile -Command 'git checkout -b alpha'",
   "cwd":"C:\\b\\codex-probe\\persist-a","commandActions":[{"type":"unknown","command":"git checkout -b alpha"}],
   "proposedExecpolicyAmendment":["git","checkout","-b","alpha"],
   "availableDecisions":["accept",{"acceptWithExecpolicyAmendment":{"execpolicy_amendment":["git","checkout","-b","alpha"]}},"cancel"]}
  Pozor: availableDecisions NEMA acceptForSession ani decline (len accept, amendment, cancel)!
  (decline sme aj tak poslali a Codex ho prijal: item status "declined".)
  Odpoved {"decision":{"acceptWithExecpolicyAmendment":{"execpolicy_amendment":[...]}}} prijata,
  prikaz bezal. Zapisane: NOVY subor CODEX_HOME/rules/default.rules, obsah presne
  `prefix_rule(pattern=["git", "checkout", "-b", "alpha"], decision="allow")` + "\n".
  Ziadna cesta projektu v pravidle. Navrh = CELY prikaz vratane argumentov, takze:
  B (nove vlakno, ten isty cwd, -b beta) PYTA, C (iny cwd, gamma) PYTA, D (novy proces, delta) PYTA.
  Vedlajsi nalez: config.toml dostal [projects.'c:\b\codex-probe\persist-a'] trust_level="trusted"
  pri prvom thread/start v kazdom cwd (aj persist-b v kroku C) -- nezavisle od amendmentu
  (asi kvoli danger-full-access/untrusted v thread/start? overit).
  Skutocny ~/.codex: config.toml mtime nezmeneny, rules/ nevznikol.
- [x] cmd-prefix (persist-cmd-prefix.out): odpoved s kratsim amendmentom ["git","checkout"]
  (klient si ho sam skratil, navrh bol cely prikaz) Codex PRIJAL bez chyby, zapisal
  `prefix_rule(pattern=["git", "checkout"], decision="allow")`; potom B (beta, ten isty cwd),
  C (gamma, iny cwd), D (delta, novy proces) BEZ ziadosti, vsetky completed.
  => pravidlo je globalne (CODEX_HOME), prefixove, klient smie poslat vlastny prefix.
- [x] cmd-same (persist-cmd-same.out): cely navrh; ten isty presny prikaz v B, C, D bez ziadosti.
- [x] mcp (persist-mcp.out, server cez -c, on-request + read-only): ziadost
  {"threadId":..,"turnId":..,"serverName":"fakepersist","mode":"form","_meta":{"codex_approval_kind":"mcp_tool_call",
   "persist":["session","always"],"tool_description":..,"tool_params":{"app":"Notepad"},"tool_params_display":[..]},
   "message":"Allow the fakepersist MCP server to run tool \"open_app\"?","requestedSchema":{"type":"object","properties":{}}}
  Odpoved {"action":"accept","content":{},"_meta":{"persist":"always"}} -> nastroj bezal, ale stderr:
  `ERROR codex_core::mcp_tool_call: failed to persist MCP tool approval error=MCP server `fakepersist`
  is not configured in config.toml or an enabled plugin`. Nic sa nezapisalo, B/C/D sa pytali znova.
  Klientovi sa chyba NEOHLASI (len stderr). Ani "session" efekt sa nepouzil (B v novom vlakne pytal).
- [x] mcp-cfg (persist-mcp-cfg.out, server v CODEX_HOME/config.toml): to ista odpoved zapise do
  config.toml (pripoji na koniec):
      [mcp_servers.fakepersist.tools.open_app]
      approval_mode = "approve"
  Globalne, ziadna cesta projektu. B (nove vlakno, Paint), C (iny cwd, Calculator), D (novy proces,
  WordPad) BEZ ziadosti, vsetky completed. Dosah = server+nastroj, nie cely server.
- Skutocny ~/.codex: snimka mtime/velkosti do hlbky 2 pred a po vsetkych behoch identicka.

## Zaver
1. Prikaz: amendment = pole tokenov, navrh = cely prikaz s argumentmi. Odpoved je objekt
   {"decision":{"acceptWithExecpolicyAmendment":{"execpolicy_amendment":[...]}}}; klient smie poslat
   kratsi prefix a Codex ho zapise tak, ako prisiel. Zapis: CODEX_HOME/rules/default.rules,
   riadok `prefix_rule(pattern=[...], decision="allow")`. Globalne, bez projektu; plati v novom
   vlakne, inom cwd aj novom procese. availableDecisions v tejto ziadosti: accept, amendment, cancel
   (bez acceptForSession a decline, decline aj tak funguje).
2. MCP always: zapisuje do CODEX_HOME/config.toml `[mcp_servers.<server>.tools.<tool>]
   approval_mode = "approve"`, globalne. Funguje LEN ak server je v config.toml (alebo plugine);
   pre server z -c tichy neuspech (len stderr ERROR) a nic sa nepamata.
Vedlajsie: pri untrusted + danger-full-access Codex pri thread/start zapise do config.toml
[projects.'<cwd malymi>'] trust_level="trusted" (v mcp behoch s on-request/read-only nie). Pricina nezmerana.
Nezmerane: viac pravidiel za sebou (pripaja sa do default.rules?); ci sa pravidla nacitaju aj
za behu ineho procesu (ziveho) bez restartu; MCP always pre plugin server.
