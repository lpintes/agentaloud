# Sonda: computer use v Codexe a serverove poziadavky

Pracovny priecinok: C:\b\cu-probe (schema/, ts/ = vystup generate-json-schema / generate-ts, codex 0.160.0)

## Stav
- zaloha ~/.codex/config.toml: /tmp/codex-config.toml.bak (pred akoukolvek zmenou); ~/.codex
  zatial NEZMENENE (diff = zhoda, 6. 10. 2026)
- ServerRequest metody (ts/ServerRequest.ts): item/commandExecution/requestApproval,
  item/fileChange/requestApproval, item/tool/requestUserInput, mcpServer/elicitation/request,
  item/permissions/requestApproval, item/tool/call, account/chatgptAuthTokens/refresh,
  attestation/generate, applyPatchApproval, execCommandApproval
- computer use = plugin `computer-use@openai-bundled` (retazce v codex.exe), marketplace
  openai-bundled tu nie je (`codex plugin add computer-use@openai-bundled` -> not found).
  openai-bundled sa plni z `~/.cache/codex-runtimes/codex-primary-runtime/plugins`
  (+ `~/.codex/.tmp/bundled-marketplaces`), co dodava desktopova appka Codex
  (`codex app` = "opens the app installer if missing"). Tu nic z toho nie je ->
  plugin sa bez instalatora pridat NEDA. Zastavene podla zadania.
- `codex plugin list` (openai-curated-remote) computer use nema.
- feature `computer_use` = stable true.
- schvalovanie MCP nastrojov ide cez mcpServer/elicitation/request s _meta.codex_approval_kind
  = "mcp_tool_call" a volbou persist "session"/"always" (retazce v binarke, core/src/mcp_tool_call.rs;
  texty "Allow for this session", "Allow and don't ask me again", "Approve app tool call?")
- "approved app" je KONFIGURACIA: config.toml `[computer_use]` default_app_access = "allow"|"deny",
  `[computer_use.windows]` aumids = { "<AUMID>" = "allow"|"deny" }, exes = [{publisher_name,
  product_name, binary_name, access}] (ts/v2/ComputerUse*.ts). Requirements (spravca):
  allowPersistentApproval, allowLockedComputerUse.

## Nahradna sonda (hotovo, 6. 10. 2026)
tools/probe_computer_use.py: falosny MCP server `fakecu` (python, stdio, cez -c mcp_servers.*,
~/.codex sa nemeni), nastroj open_app, ktory si ako plugin vypyta suhlas MCP elicitaciou.
Logy C:/b/codex-probe/logs/cu-{refuse,accept,refuse2,session}.log a cu-mcp.log. gpt-6-luna, low.

Zistenia (vsetky odmerane):
- Codex klientovi MCP serverov deklaruje `elicitation: {form:{}, url:{}}`.
- POVOLENIE SU DVE VRSTVY a obe pridu ako `mcpServer/elicitation/request`:
  1. Codex sam: schvalenie volania MCP nastroja (_meta.codex_approval_kind="mcp_tool_call",
     persist ["session","always"], tool_params, tool_params_display), message
     "Allow the fakecu MCP server to run tool \"open_app\"?".
  2. Elicitacia samotneho servera/pluginu (_meta null), preposlana 1:1 (mode form).
- refuse: -32601 na vrstvu 1 -> mcpToolCall failed, error "user rejected MCP tool call",
  nastroj sa vobec nezavola.
- refuse2: vrstva 1 accept, vrstva 2 -32601 -> Codex posle MCP serveru {"action":"decline"},
  server vrati "not approved", model to zopakuje. = symptom z ineho stroja.
- accept: {"action":"accept","content":{},"_meta":null} na obe -> prejde.
- session: {"action":"accept","content":{},"_meta":{"persist":"session"}} na vrstvu 1 ->
  druhe volanie toho isteho nastroja uz vrstvu 1 nepyta (vrstva 2 pyta znova, to je server).
- po kazdej odpovedi pride notifikacia serverRequest/resolved {threadId, requestId}.
- config.toml po vsetkych behoch nezmeneny. persist "always" NEODSKUSANE (zapisalo by do
  ~/.codex; podla retazcov asi mcp_servers.<srv>.tools.<tool>.approval_mode = "approve").
- Neoverene: aky `mode` a _meta posiela skutocny computer-use plugin (mozno "openai/form").

## Overenie v appke (6. 10. 2026) -- PRESLO
bin/agentaloud.exe (s obsluhou mcpServer/elicitation/request), cez tools/lens.ps1, bez klaves.
Prikazovy riadok (Start-Process s jednym retazcom; pole argumentov PowerShell rozbil na medzere
v args=[..., "mcp"] a codex app-server potom TICHO skoncil -- okno bez chyby, ziadny proces):
  --backend codex --model gpt-6-luna C:\b\cu-app-test -- -c "mcp_servers.fakecu.command=\"<python>\""
  -c "mcp_servers.fakecu.args=[\"<.../probe_computer_use.py>\",\"mcp\"]"
Jedna session, tri tahy, kazdy vyvolal DVA dialogy IDD_PERMISSION v spravnom poradi:
1. Codex (vrstva 1): titulok "AgentAloud — povolenie: fakecu"; Popis "Computer use: open a desktop
   application and return its window title."; Dovod "Allow the fakecu MCP server to run tool
   \"open_app\"?"; Argumenty "app: Notepad" (fokus tu). Tlacidla &Povolit / &Zamietnut.
2. Server (vrstva 2): ten isty titulok; Popis "neuvedeny"; Dovod "Allow Codex to use Notepad?";
   Argumenty "bez argumentov" (fokus tu).
Vysledky (BM_CLICK cez PostMessage):
- povolit + povolit: MCP log approved=True; prepis "fakecu.open_app: Notepad" / "vystup (1 riadok)" /
  "codex: The window title is **Untitled - Notepad**."
- povolit + zamietnut: server dostal {"action":"decline"}; prepis "chyba: Calculator is not approved
  for computer use (action=decline). (1 riadok)", model to zopakoval.
- zamietnut vrstvu 1: server sa nevolal; prepis "user rejected MCP tool call" (BEZ predpony "chyba:")
  a "codex: user rejected MCP tool call".
Appka po celom case odpovedala, instancia zavreta podla PID, codex ani fakecu nezostali.
