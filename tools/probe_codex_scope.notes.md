# Sonda: dosah "na session" v Codexe (MCP nastroj, fileChange)

codex 0.160.0 (natívny). Sonda tools/probe_codex_scope.py. Logy C:/b/codex-probe/logs/scope-*.log,
MCP server C:/b/codex-probe/logs/scope-mcp.log. Pracovne priecinky C:/b/codex-probe/scope-repo,
C:/b/codex-probe/scope-outside (vlastne, smu sa mazat). ~/.codex sa nemeni (len -c).

## Stav
- [x] otazka 1 (MCP: session na A -> A znova, B toho isteho servera) -- scope-mcp.log
  Vysledok: open_app(Notepad) ziadost #0 -> accept+persist session; open_app(Calculator) BEZ
  ziadosti; close_app(Notepad) ziadost #1 (znova pyta). Session = len ten isty nastroj, nie server.
  Meno nastroja je LEN v `message` ("...run tool \"close_app\"?"); _meta ma tool_description,
  tool_params, tool_params_display, persist, codex_approval_kind -- ziadne pole s menom nastroja.
  Server je v `serverName`.
- [x] otazka 2 (fileChange: acceptForSession na a.txt -> b.txt, podpriecinok, mimo workspace)
  - file (untrusted + workspace-write, scope-file.log): a.txt #0 acceptForSession, potom b.txt #1,
    sub/c.txt #2, mimo workspace d.txt #3 -- KAZDY sa pyta znova.
  - file-ro (on-request + read-only, scope-file-ro.log): to iste, 4 ziadosti.
  - file-same (untrusted + workspace-write, scope-file-same.log): a.txt vytvor #0 acceptForSession,
    a.txt uprav (update) BEZ ziadosti, b.txt #1 pyta. => session = ten isty subor (mnozina ciest patchu).
  - file-or (on-request + workspace-write, scope-file-or.log): a/b/sub/c bez ziadosti (sandbox
    povoli), len d.txt mimo workspace pyta.
  - reason aj grantRoot VZDY null vo vsetkych 4 behoch (aj pri zapise mimo workspace).

## Zaver
1. MCP: "persist session" na nastroj A (open_app) pokryje dalsie volania A aj s inymi argumentmi
   (Calculator nepytal), ale nastroj B (close_app) toho isteho servera sa pyta znova -- dosah je
   server+nastroj, nie server. Meno nastroja je len v `message`; v _meta je tool_description,
   tool_params, tool_params_display, nie meno.
   params: {"threadId":"01a12739-...","turnId":"01a12739-...","serverName":"fakescope",
   "mode":"form","_meta":{"codex_approval_kind":"mcp_tool_call","persist":["session","always"],
   "tool_description":"Close a desktop application.","tool_params":{"app":"Notepad"},
   "tool_params_display":[{"name":"app","value":"Notepad","display_name":"app"}]},
   "message":"Allow the fakescope MCP server to run tool \"close_app\"?",
   "requestedSchema":{"type":"object","properties":{}}}
2. fileChange: acceptForSession na a.txt pokryje dalsiu upravu TOHO ISTEHO suboru (a.txt), ale
   b.txt v tom istom priecinku, sub/c.txt aj subor mimo workspace sa pytaju znova (untrusted aj
   on-request/read-only). grantRoot aj reason prisli vzdy null.
   params: {"threadId":"01a1273a-...","turnId":"01a1273a-...","itemId":"exec-b6f66f03-...",
   "startedAtMs":1791659487253,"reason":null,"grantRoot":null}
   Cesty su len v iteme fileChange (item/started, `changes[].path`), nie v ziadosti.
Nezmerane: kedy grantRoot nie je null (ani zapis mimo workspace pri workspace-write ho nevyvolal;
asi len pri eskalacii sandboxu z prikazu/apply_patch s vyslovnou ziadostou o root); persist
"always" (zapisalo by do ~/.codex); ci session MCP prezije novy tah / thread/resume (meral sa
jeden tah); patch s viacerymi subormi naraz (ci session pokryje podmnozinu).
(Dve posledne veci domerane nizsie.)

## Druhe kolo (10. 10. 2026): viacsuborovy patch a session cez tahy
- multi (untrusted + workspace-write, scope-multi.log), tah 1: JEDEN apply_patch s a.txt, b.txt,
  sub/c.txt -> JEDNA ziadost #0 (acceptForSession); potom zvlast update a.txt, b.txt, sub/c.txt BEZ
  ziadosti; d.txt (novy) ziadost #1 (accept). Tah 2: update a.txt a sub/c.txt BEZ ziadosti,
  e.txt (novy) ziadost #2.
- mcp-turns (on-request + read-only, scope-mcp-turns.log): tah 1 open_app(Notepad) ziadost
  (accept + persist session); tah 2 open_app(Paint) BEZ ziadosti.

## Zaver druheho kola
a) Jeden patch s tromi subormi = jedna ziadost item/fileChange/requestApproval; v nej ziadna
   cesta, subory su len v iteme.
   params: {"threadId":"01a1274d-6242-72c0-b40f-a0c331ea79bb","turnId":"01a1274d-64fa-7613-a2c3-bbecb4f39f73",
   "itemId":"exec-ea602761-dca1-4378-a149-c3e662884d07","startedAtMs":1791660748811,"reason":null,"grantRoot":null}
   item/started changes: [{"path":"C:\\b\\codex-probe\\scope-repo\\a.txt","kind":{"type":"add"},"diff":"hello\n"},
   {"path":"C:\\b\\codex-probe\\scope-repo\\b.txt","kind":{"type":"add"},"diff":"hello\n"},
   {"path":"C:\\b\\codex-probe\\scope-repo\\sub\\c.txt","kind":{"type":"add"},"diff":"hello\n"}]
b) acceptForSession na ten patch pokryje VSETKY jeho subory: dalsia uprava a.txt, b.txt aj sub/c.txt
   sa nepyta; novy d.txt sa pyta.
c) Session povolenie prezije do dalsieho tahu v tom istom vlakne -- pre subory (a.txt, sub/c.txt
   v 2. tahu bez ziadosti, novy e.txt sa pyta) aj pre MCP nastroj (open_app v 2. tahu bez ziadosti).
Nezmerane: prezitie cez thread/resume alebo novy proces app-server (meral sa len dalsi tah v tom
istom procese); persist "always"; grantRoot != null sa vyvolat nepodarilo.

## Vychodiska (z inych sond)
- edit.log (untrusted + workspace-write): params fileChange approval
  {threadId, turnId, itemId:"exec-...", startedAtMs, reason:null, grantRoot:null}.
- schema: grantRoot "[UNSTABLE] ... allow writes under this root for the remainder of the
  session (unclear if this is honored today)". Rozhodnutia accept|acceptForSession|decline|cancel.
