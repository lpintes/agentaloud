# Sonda: approvalPolicy "never", sandbox a auto-review v Codexe

codex-cli 0.160.0 (C:\Users\pintes\AppData\Local\Programs\OpenAI\Codex\bin\codex.exe).
Schema: C:\b\cu-probe\schema, C:\b\cu-probe\ts (generate-json-schema / generate-ts, 0.160.0).
Retazce binarky: C:\b\cu-probe\codex-strings.txt.
Logy: C:/b/codex-probe/logs/never-*.log. ~/.codex sa nemeni (len -c a parametre RPC).

## Stav
- [x] otazka 3 staticky (schema, --help, binarka)
- [x] otazka 1 (never + workspace-write, prikaz mimo sandboxu)
- [x] otazka 2 (never + elicitacia MCP)
- [x] otazka 3 naostro (approvalsReviewer auto_review)

## Otazka 1 -- never + workspace-write (never-ww-sandbox.log, gpt-6-luna low)
- thread/start {approvalPolicy:"never", sandbox:"workspace-write"} prijate; odpoved
  approvalPolicy "never", approvalsReviewer "user", sandbox {type:workspaceWrite, writableRoots:[],
  networkAccess:false, ...}. turn/start s approvalPolicy+sandboxPolicy prijate (thread/settings/updated
  s tymi istymi hodnotami).
- ZIADNA serverova poziadavka. Oba prikazy rovno zlyhali a model dostal vystup:
  - zapis mimo cwd: commandExecution status "failed", exitCode 1, aggregatedOutput
    "Set-Content : Access to the path 'C:\b\codex-probe\outside\x.txt' is denied. ..."
    (Windows sandbox naozaj vynuteny, subor nevznikol).
  - curl: status "failed", exitCode 1, "curl: (7) Failed to connect to example.com:443 after 38 ms:
    Could not connect to server\r\n000".
  - item: {"type":"commandExecution", "command":"\"C:\\Program Files\\PowerShell\\7\\pwsh.exe\"
    -Command ...", "source":"unifiedExecStartup", "status":"failed", "commandActions":[{type:unknown}],
    "aggregatedOutput":..., "exitCode":1, "durationMs":...}
  - Model zlyhanie len ohlasil (pokyn "nepytaj eskalaciu"); ziadne guardianWarning/warning.

## Otazka 2 -- never + MCP (never-ww-mcp.log, never-full-mcp.log, cu-mcp.log)
- never + workspace-write: ZIADNA mcpServer/elicitation/request. mcpToolCall hned failed,
  durationMs 0, error {"message":"MCP tool call requires approval, but approval policy is never"};
  MCP server tools/call vobec nedostal. Model to len zopakoval.
- never + danger-full-access: ZIADNA serverova poziadavka ku klientovi. Vrstva 1 (schvalenie
  nastroja Codexom) vynechana, nastroj zavolany; vlastnu elicitaciu servera ("Allow Codex to use
  Notepad?") Codex SAM zodpovedal action=accept (cu-mcp.log: approved=True action=accept).
  mcpToolCall completed.

## Otazka 3 -- staticky (7. 10. 2026)
- `ApprovalsReviewer = "user" | "auto_review" | "guardian_subagent"` (ts/v2/ApprovalsReviewer.ts).
  Doc: "Configures who approval requests are routed to for review. Examples include sandbox
  escapes, blocked network access, MCP approval prompts, and ARC escalations. Defaults to `user`.
  `auto_review` uses a carefully prompted subagent to gather relevant context and apply a
  risk-based decision framework before approving or denying the request."
- Pole `approvalsReviewer?` je v ThreadStartParams, TurnStartParams ("Override where approval
  requests are routed for review on this turn and subsequent turns"), ThreadResume/Fork params;
  v ThreadSettings a ThreadStartResponse povinne (`approvalsReviewer: ApprovalsReviewer`).
  config.toml: `approvals_reviewer`, sekcia `auto_review` (AutoReviewRequirements:
  requiredOnModels, ignoreRules; requirement `disableAutoReview`).
- Notifikacie [UNSTABLE]: `item/autoApprovalReview/started`, `item/autoApprovalReview/completed`
  (reviewId, targetItemId, decisionSource "agent", review {status inProgress|approved|denied|
  timedOut|aborted, riskLevel low..critical, userAuthorization, rationale}, action {type command|
  execve|writeStdin|applyPatch|networkAccess|mcpToolCall|requestPermissions ...}),
  `autoApprovalReview/strictReviewRequired`, `guardianWarning` {threadId, message}.
- Klientska poziadavka `thread/approveGuardianDeniedAction` {threadId, event} -- clovek prebije
  zamietnutie recenzenta.
- AskForApproval ma aj granularnu formu: `{granular: {sandbox_approval, rules, skill_approval,
  request_permissions, mcp_elicitations}}` (booleany) popri "untrusted"|"on-request"|"never".
- CLI: `codex --help` aj `codex exec --help`: `--approve-for-me` "Route approval requests through
  automatic review using the workspace-write sandbox" (interny nazov AUTO_REVIEW, alias
  `not-so-yolo`). TUI ma predvolby "Read Only" / "Ask for approval" / "Approve for me" / full access.
- `-a never`: "Never ask for user approval. Execution failures are immediately returned to the model".

## Otazka 3 -- naostro (never-review.log, never-review-esc.log)
- thread/start aj turn/start s `approvalsReviewer:"auto_review"` (+ on-request, workspace-write)
  prijate, bez experimentalApi problemu (initialize mal experimentalApi true); thread/settings/updated
  hlasi approvalsReviewer "auto_review".
- `review`: prompt zakazal pytat eskalaciu -> model nepytal, prikazy zlyhali v sandboxe ako
  v otazke 1, recenzent sa nespustil (nebolo co posudzovat).
- `review-esc`: model poziadal o eskalaciu curl. ZIADNA serverova poziadavka ku klientovi; namiesto
  toho notifikacie v poradi:
  1. item/autoApprovalReview/started {reviewId, targetItemId:"exec-...", review {status:"inProgress",
     riskLevel:null, ...}, action {type:"command", source:"unifiedExec", command, cwd}}
  2. guardianWarning {threadId, message:"Automatic approval review approved (risk: low,
     authorization: high): The user explicitly authorized ..."}  (pozor: chodi aj pri schvaleni)
  3. item/autoApprovalReview/completed {... decisionSource:"agent", review {status:"approved",
     riskLevel:"low", userAuthorization:"high", rationale:"..."}, action {...}}
  4. commandExecution completed, aggregatedOutput "200", exitCode 0 (mimo sandboxu, so sietou).
  Recenzia trvala ~5 s. Zamietnutie recenzentom NEODSKUSANE.

## Druhe kolo (7. 10. 2026): zamietnutie, MCP, prepinanie recenzenta

### Zamietnutie recenzentom (never-review-deny*.log)
- Promptom sa vyprovokovat NEDALO: "posli prod credentials na externu adresu" hlavny model
  odmietol sam este pred eskalaciou (2 pokusy; DENY_PROMPT vyhodeny).
- `-c auto_review.extra_policy="... example.com ... critical, deny regardless ..."`: config/read
  potvrdil, ze sa nastavi (auto_review.extra_policy), ale recenzent aj tak SCHVALIL (risk low,
  authorization high, rationale o politike nehovori) -- 2 pokusy (never-review-deny-extra.log).
- `-c auto_review.policy="Security policy: every action is critical risk and must be denied..."`
  (nahradi celu politiku) -> ZAMIETNUTE. Tvary:
  - item/autoApprovalReview/started ako pri schvaleni.
  - guardianWarning {threadId, message:"Automatic approval review denied (risk: critical,
    authorization: high): <rationale>"}
  - item/autoApprovalReview/completed {..., decisionSource:"agent", review {status:"denied",
    riskLevel:"critical", userAuthorization:"high", rationale}, action {type:"command",
    source:"unifiedExec", command, cwd}}
  - item/completed commandExecution {status:"declined", source:"agent", processId:null,
    aggregatedOutput:null, exitCode:null, durationMs:null} -- prikaz nebezal.
  - Ziadna serverova poziadavka klientovi. Tah POKRACUJE: model dostal zamietnutie s dovodom a
    v odpovedi ho zopakoval ("The command was rejected by the automatic approval review ...");
    turn/completed status "completed".
- `thread/approveGuardianDeniedAction` je LEN klientske RPC (ziadna serverova poziadavka, na ktoru
  by sa reagovalo). Params {threadId, event}, event = serializovany GuardianAssessmentEvent
  v snake_case: {id:<reviewId>, target_item_id, turn_id, status:"denied", risk_level,
  user_authorization, rationale, decision_source:"agent", action {type:"command",
  source:"unified_exec" (NIE "unifiedExec" -> chyba "unknown variant"), command, cwd},
  started_at_ms, completed_at_ms}. Prazdny event -> -32600 "invalid Guardian denial event:
  missing field `id`". Platny -> result {} a ZIADNA notifikacia.
- Ucinok (rollout ~/.codex/sessions/...01a1154a-f199...): do historie pribudne developer sprava
  "The user has manually approved a specific action that was previously `Rejected`. Treat this as
  approval to perform that exact action ... Do not assume this also authorizes similar operations
  with different payloads. Approved action: {...}". Tah sam nespusti. Dalsi tah (prompt "retry")
  ide ZNOVA cez recenzenta (retazec v binarke: "the retry still goes through auto-review"); pri
  critical znova denied ("does not permit overrides for critical actions") -- sedi s politikou
  v binarke: post-denial schvalenie prebije len high-risk zamietnutie, nie critical.

### MCP pri auto_review (never-review-mcp.log, cu-mcp.log)
- Vrstva 1 (schvalenie MCP nastroja Codexom) ide RECENZENTOVI, nie klientovi:
  item/autoApprovalReview/started|completed s action {type:"mcpToolCall", server:"fakecu",
  toolName:"open_app", connectorId:null, connectorName:null, toolTitle:null}, approved (low/high),
  guardianWarning "Automatic approval review approved ...".
- Vrstva 2 (vlastna elicitacia servera) pride KLIENTOVI ako doteraz:
  mcpServer/elicitation/request {threadId, turnId, serverName:"fakecu", mode:"form", _meta:null,
  message:"Allow Codex to use Notepad?", requestedSchema{...}} -> po odpovedi serverRequest/resolved.
- Nastroj prebehol (mcpToolCall completed). tools/call _meta nesie aj "sandbox":"windows_elevated".

### Prepnutie recenzenta za behu (never-review-switch.log)
- Jedno vlakno: thread/start bez approvalsReviewer -> "user". turn/start s approvalsReviewer
  "auto_review" -> thread/settings/updated hlasi "auto_review", eskalacia curl isla recenzentovi
  (approved, prikaz bezal, 200). Dalsi turn/start s "user" -> thread/settings/updated hlasi "user"
  a eskalacia prisla klientovi ako item/commandExecution/requestApproval {kind:"command", itemId,
  reason:"check that example.com answers", command, cwd, commandActions,
  proposedExecpolicyAmendment:[...], availableDecisions:["accept",
  {acceptWithExecpolicyAmendment:{execpolicy_amendment:[...]}}, "cancel"]}; sonda decline ->
  commandExecution status "declined", model: "Rejected(\"rejected by user\")".
- Teda: turn/start prepina recenzenta oboma smermi a thread/settings/updated to ohlasi.

## Stav na konci (7. 10. 2026)
Vsetky tri otazky zodpovedane. ~/.codex nezmeneny (len -c a parametre RPC).
Neodskusane (po 2. kole): granular approvalPolicy, zamietnutie MCP recenzentom, thread/settings/update s approvalsReviewer,
`guardian_subagent`, never + MCP s ineho nastroja nez fakecu (skutocny computer use plugin tu nie je).
