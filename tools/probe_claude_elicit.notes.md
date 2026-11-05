# Sonda: MCP elicitacia v Claude CLI (headless stream-json)

Claude Code 2.1.314, `claude -p --verbose --input-format stream-json --output-format stream-json
--permission-prompt-tool stdio --replay-user-messages --model haiku`, initialize len `{"hooks":{}}`.
Odmerane 5. 11. 2026. Sonda: tools/probe_claude_elicit.py (falosny server `fakeel`
z probe_elicit_form.py cez `--mcp-config <json> --strict-mcp-config`, ~/.claude nezmenene).
Rezimy run, error, bad, silent, interrupt. Logy: C:/b/claude-elicit-probe/logs/<rezim>.*
(stream, out, debug) a C:/b/elicit-probe/logs/el-mcp.log (strana servera, pripisuje sa).

## Zistenia (odmerane)
- CLI serveru deklaruje `capabilities.elicitation: {form:{}, url:{}}` (aj `roots`, extension
  `io.modelcontextprotocol/tasks`), protocolVersion "2025-11-25"; pred initialize skusi
  `server/discover` (fakeel vrati -32601, CLI pokracuje). Bez akehokolvek pola v initialize
  control_request elicitacie chodia na stdio. Vypina ich len `--permission-prompts none`
  (`hostAnswersElicitations=false` -> cancel bez otazky; z binarky, neodskusane).
- Poradie: tool_use -> `can_use_tool` pre mcp__fakeel__collect (aj pri readOnlyHint=true; nesie
  `mcp_server:{name,source}`, `server_policy_ask_only:true`, navrh addRules localSettings) ->
  az po allow tools/call -> elicitacie. Model najprv vola ToolSearch (MCP nastroj je deferred).
- Formular: `{"type":"control_request","request_id":"<uuid>","request":{"subtype":"elicitation",
  "mcp_server_name":"fakeel","message":"CASE valid: ...","mode":"form","requested_schema":{...},
  "property_order":["name","email","age","ratio","subscribe","color","size","legacy","tags"]}}`.
  requested_schema obsahovo nezmeneny (vsetky kluce ako Codex) A poradie kluca v properties
  zachovane; `property_order` je navyse explicitne (popis v binarke: pre hostitela, ktory poradie
  kluca neudrzi; indexove mena "0","1" by isli prve).
- Url: `{"subtype":"elicitation","mcp_server_name":"fakeel","message":"...","mode":"url",
  "url":"https://...","elicitation_id":"el-url_accept"}` -- bez requested_schema/property_order.
- Volitelne `title`, `display_name`, `description` z `_meta["anthropic/permissionDisplay"]`
  servera (len z binarky, neodskusane).
- Odpoved: obvykly obal `{"type":"control_response","response":{"subtype":"success",
  "request_id":"<id>","response":{"action":"accept","content":{...}}}}`; decline/cancel
  `{"action":"decline"}` / `{"action":"cancel"}` bez content. Schema v binarke:
  `{action: accept|decline|cancel, content?: record}`.
- Server dostane presne `{"action":"accept","content":{...}}` / `{"action":"decline"}` /
  `{"action":"cancel"}`; url accept bez content -> `{"action":"accept"}` (bez content, nie {}).
- CLI NEVALIDUJE obsah voci schema: chybajuce required, "five" za integer, 500 nad maximum,
  kratke meno, 3 polozky pri maxItems 2, mimo enum/oneOf, nezname pole, cisla/boolean ako
  retazce -- vsetko 1:1 serveru.
- `subtype:"error"` odpoved -> server dostane `{"action":"cancel"}`. Zla odpoved (action mimo
  enum, content retazec, response {}, bez response) -> tiez cancel, bez chyby a bez hlasky
  na stdout.
- CLI vrati na stdout kopiu kazdej nasej `control_response` so `subtype:"success"` (aj
  can_use_tool, aj zlych), `error` nevracia. Ci je to kvoli --replay-user-messages, neoverene.
- url: `notifications/elicitation/complete` od servera pride klientovi ako
  `{"type":"system","subtype":"elicitation_complete","mcp_server_name":"fakeel",
  "elicitation_id":"el-url_accept","uuid":...,"session_id":...}` (aj ked klient odpovedal
  cancel/chybou).
- Okolo elicitacie v streame nic ine: ziadny tool_result ani system zaznam; tool_result
  az na konci volania nastroja.
- Bez odpovede CLI neodpocitava (390 s), kazdych 30 s `tool_progress` s `heartbeat:true`.
- Interrupt pocas otvorenej elicitacie: `control_cancel_request {"request_id":"<id elicitacie>"}`,
  server dostane cancel aj `notifications/cancelled` pre tools/call, tool_result is_error, result
  `error_during_execution`. Server, ktory pokracuje, posle dalsiu elicitaciu a CLI ju klientovi
  prepose este PO `result`. To iste `control_cancel_request` pride, ked sa zavrie spojenie s MCP
  serverom (tool_result "Connection closed").
- Zatvoreny stdin: otvorena elicitacia -> cancel, dalsie uz klientovi nechodia (cancel hned).

## Neodskusane
- `--permission-prompts none`, `_meta["anthropic/permissionDisplay"]`, elicitacia zo subagenta
  (pole agent_id?), elicitation hooky (Elicitation/ElicitationResult v settings).
