# Sonda: MCP elicitacia s polami formulara a rezim "url" v Codexe

codex-cli 0.160.0 (natívny), `codex app-server`, gpt-6-luna / low. Odmerane 27. 10. 2026.
Sonda: tools/probe_elicit_form.py (falosny MCP server `fakeel` cez -c mcp_servers.*, ~/.codex
nezmeneny, sha1 config.toml pred aj po rovnaky). Jeden tah, jedno volanie nastroja `collect`;
server v nom posle za sebou 9 formularov a 3 url elicitacie, klient odpovie podla "CASE <meno>:"
v sprave.
Logy: C:/b/elicit-probe/logs/el-run.log (readOnlyHint=true), el-rw.log (readOnlyHint=false),
el-mcp.log (strana MCP servera, oba behy, oddelene "=== rw").

## Zistenia (vsetky odmerane)
- Codex serveru deklaruje `elicitation: {form:{}, url:{}}`, protocolVersion "2025-06-18".
- A. Formular pride ako `mcpServer/elicitation/request` s params
  `{threadId, turnId, serverName, mode:"form", _meta:null, message, requestedSchema}`.
  requestedSchema je OBSAHOVO nezmeneny: zostava minLength/maxLength, format, default,
  minimum/maximum, enum, enumNames, oneOf [{const,title}], array items.enum, minItems/maxItems,
  required, title, description. Nic nepremenovane ani zahodene.
  ALE poradie kluca v `properties` sa STRATI: pride abecedne (age, color, email, legacy, name,
  ratio, size, subscribe, tags), server ich poslal v poradi name, email, age, ... Poradie
  poli formulara sa teda z requestedSchema vyrobit neda (Codex to drzi ako mapu).
  Vrstva schvalenia Codexu (_meta.codex_approval_kind="mcp_tool_call", requestedSchema
  {type:object, properties:{}}, message "Allow the fakeel MCP server to run tool \"collect\"?")
  pride PRED volanim nastroja len ked nastroj NEMA readOnlyHint=true. S readOnlyHint=true
  sa Codex nepyta vobec, prve co klient vidi je elicitacia servera.
- B. accept s platnym obsahom: server dostane presne to iste, typy zachovane
  (`{"action":"accept","content":{"name":"Peter","email":"p@example.com","age":5,"ratio":2.25,
  "subscribe":false,"color":"blue","size":"m","legacy":"b","tags":["x","z"]}}`). Pole `_meta`
  z odpovede klienta (null) Codex serveru neposle.
- C. Codex NEVALIDUJE: chybajuce required pole, "five" namiesto integer, age 500, name kratsie
  ako minLength, 3 polozky pri maxItems 2, hodnota mimo enum/oneOf, aj nezname pole navyse
  -> vsetko dorazi serveru 1:1, bez chyby klientovi aj serveru. Validacia je na klientovi
  (alebo serveri).
- D. Cisla a boolean ako retazce ("5", "2.25", "true") prejdu nezmenene ako retazce, bez chyby.
- E. Rezim url dorazi klientovi:
  `{threadId, turnId, serverName, mode:"url", _meta:null, message, url, elicitationId}`
  (elicitationId nezmenene zo servera). Odpovede: accept s content null -> server dostane
  `{"action":"accept","content":{}}` (null sa zmeni na {}); decline -> `{"action":"decline"}`;
  cancel -> `{"action":"cancel"}`. `notifications/elicitation/complete` od servera Codex
  klientovi NEPREPOSLE -- po nej nepride ziadna notifikacia (len to iste co vzdy).
- F. cancel (formular aj url): server dostane `{"action":"cancel"}`, bez content; decline
  `{"action":"decline"}`. content:null od klienta Codex vypusti.
- Po kazdej odpovedi pride `serverRequest/resolved {threadId, requestId}`; okolo kazdej
  elicitacie `thread/status/changed` s activeFlags ["waitingOnApproval"] a potom [].
- Elicitacie sa v prepise neobjavia: ziadny item, len mcpToolCall po skonceni nastroja.
- Id serverovej poziadavky voci klientovi cisluje app-server od 0 (nie id z MCP servera).

## Neodskusane
- mode "openai/form" / "openaiForm" (v typoch su, requestedSchema ako volny JsonValue).
- URLElicitationRequiredError (-32042) ako odpoved na tools/call.
- odpoved klienta, ktora nesedi s typom McpServerElicitationRequestResponse (napr. content ako
  retazec).
