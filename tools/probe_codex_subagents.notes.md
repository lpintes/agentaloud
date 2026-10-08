# probe_codex_subagents — subagenti a príkazy na pozadí v `codex app-server` (2026-10-08)

codex-cli 0.160.0 (natívny inštalátor). Schéma vrátane experimentálnych metód:
`codex app-server generate-ts --experimental --out C:/b/codex-probe/schema-exp`
(stabilná bez `--experimental` je v `C:\b\cu-probe\ts` a experimentálne metódy
v nej CHÝBAJÚ). Logy: `C:/b/codex-probe/logs/sub-<scenár>.log`.
Spustenie: `python -I tools/probe_codex_subagents.py <scenár>`.

## Statika (schéma 0.160.0 --experimental)

- `ThreadItem` `subAgentActivity` {id, `kind: started|interacted|interrupted|
  completed`, `agentThreadId`, `agentPath`} — dnes v `QuietItems`.
- `ThreadItem` `collabAgentToolCall` {id, `tool: spawnAgent|sendInput|
  resumeAgent|wait|closeAgent|sendMessage|followupTask|interruptAgent|
  listAgents`, `status: inProgress|completed|failed|interrupted`,
  `senderThreadId`, `receiverThreadIds[]`, `prompt`, `model`,
  `reasoningEffort`, `agentsStates{threadId: {status: pendingInit|running|
  interrupted|completed|errored|shutdown|notFound, message}}`}.
- `Thread` má `parentThreadId`, `source: SessionSource` (`{subAgent:
  {thread_spawn: {parent_thread_id, depth, agent_path, agent_nickname,
  agent_role}}}`), `agentNickname`, `agentRole`. `ThreadSourceKind` má
  `subAgentThreadSpawn` (filter pre `thread/list`).
- `Model.multiAgentVersion: disabled|v1|v2`; `ThreadStartParams`
  a `TurnStartParams` majú voliteľné `multiAgentMode: explicitRequestOnly|
  proactive|{custom}`.
- Príkazy: `commandExecution` má `processId` a `source: agent|userShell|
  unifiedExecStartup|unifiedExecInteraction`; notifikácia
  `item/commandExecution/terminalInteraction` {threadId, turnId, itemId,
  processId, stdin} (model píše do bežiaceho procesu cez `write_stdin`).
- EXPERIMENTÁLNE (len s `generate-ts --experimental`, klient musí poslať
  `capabilities.experimentalApi: true`):
  `thread/backgroundTerminals/list {threadId, cursor?, limit?}` →
  `{data: [{itemId, processId, command, cwd, osPid, cpuPercent, rssKb}],
  nextCursor}`; `thread/backgroundTerminals/terminate {threadId, processId}` →
  `{terminated: bool}`; `thread/backgroundTerminals/clean {threadId}` → `{}`.
  Notifikácia o zmene zoznamu terminálov NEEXISTUJE.
- `command/exec*` a `process/spawn|kill|writeStdin` sú príkazy, ktoré spúšťa
  KLIENT (bez vlákna a ťahu), nie agent — pre túto úlohu nepodstatné.
- Žiadna metóda „zoznam subagentov" ani „zastav subagenta" na strane klienta;
  `listAgents`/`interruptAgent`/`closeAgent` sú nástroje MODELU.

## info — zapnuté bez konfigurácie
`experimentalFeature/list`: `multi_agent` stage stable, enabled, defaultEnabled;
`multi_agent_v2` stable, ale enabled false; `unified_exec`, `unified_exec_tty`,
`sleep_tool` stable a zapnuté. `model/list`: gpt-6-luna a gpt-5.6-terra
`multiAgentVersion: "v2"`, gpt-5.6-luna `"v1"`. Subagenti teda fungujú bez
akéhokoľvek `-c`; model ich spustí, keď ho o to požiadaš (predvolený
`multiAgentMode` nevieme, parameter v thread/turn start je voliteľný).

## spawn — dvaja subagenti, hlavný na nich čaká (`wait`)
(sub-spawn.log, gpt-6-luna low, approvalPolicy never, workspace-write)
- Spustenie subagenta NEPRÍDE ako `collabAgentToolCall` tool `spawnAgent`, ale
  ako položka hlavného vlákna `subAgentActivity` {id = call_id volania,
  `kind:"started"`, `agentThreadId`, `agentPath:"/root/agent_1"`}
  (`item/started` + `item/completed` hneď za sebou, threadId = hlavné).
- Subagent je **samostatné vlákno**: nové `threadId`, vlastné
  `thread/status/changed` (idle → active), vlastný `turn/started` /
  `turn/completed` s vlastným turnId, `mcpServer/startupStatus/updated`,
  `thread/tokenUsage/updated`. `thread/started` pre subagenta NEPRÍDE (v logu
  1×, len hlavné). Spojenie s rodičom nesie len `subAgentActivity.agentThreadId`
  (v notifikáciách subagenta nie je parent id). `thread/loaded/list` potom
  vráti hlavné + oba subagentské threadId.
- Text subagenta CHODÍ do streamu: `item/started agentMessage` (phase
  `commentary` aj `final_answer`), `item/agentMessage/delta`,
  `commandExecution` s `outputDelta` — všetko s threadId subagenta.
- Čakanie hlavného: `collabAgentToolCall` {tool `wait`, status inProgress →
  completed, `senderThreadId` = hlavné, `receiverThreadIds:[]`,
  `agentsStates:{}`}. Jeden `wait` skončil po prvom dobehnutom agentovi, model
  zavolal druhý `wait`.
- Koniec subagenta: jeho `turn/completed` + `thread/status/changed idle`
  a v hlavnom vlákne `subAgentActivity` {id
  `subagent-completed-<turnId subagenta>`, `kind:"completed"`, agentThreadId,
  agentPath}.
- Subagenti bežia SÚBEŽNE s hlavným ťahom aj navzájom (položky sa prekladajú);
  hlavný `turn/completed` až po oboch (20 s). Po ňom 45 s ticho.

## spawn-nowait — hlavný nečaká, ťah skončí hneď
- Hlavný `turn/completed` po 6.6 s; subagenti bežia ďalej a ich položky
  (commandExecution, agentMessage, ich turn/completed) prúdia MIMO ťahu
  hlavného vlákna, s threadId subagenta.
- `subAgentActivity kind:"completed"` príde na HLAVNOM vlákne po jeho
  `turn/completed` ako `item/started`+`item/completed`, a nesie `turnId`
  UŽ SKONČENÉHO ťahu (nie nový, bez `turn/started`). Adaptér, ktorý
  predpokladá položky len medzi turn/started a turn/completed, to musí zniesť.
- Samovoľný ťah hlavného vlákna po dobehnutí subagentov NEVZNIKOL
  (posledný agent skončil v 37.6 s, pozorované do 51.6 s; ani `turn/started`,
  ani `thread/status/changed` hlavného). Na rozdiel od Claude
  (`task-notification` → samovoľný ťah) Codex výsledok subagenta modelu sám
  nedoručí — model sa ho dozvie až cez `wait` alebo v ďalšom ťahu (neoverené,
  či ho v ďalšom ťahu dostane automaticky).

## spawn-int — `turn/interrupt` hlavného ťahu počas behu subagentov
- Odpoveď `{}`, hlavný `turn/completed` so `status:"interrupted"`.
  Položka `collabAgentToolCall wait` (inProgress) už NIKDY nedostala
  `item/completed` — visí.
- Subagenti `turn/interrupt` hlavného PREŽIJÚ: oba bežali ďalej, dobehli príkazy (ping 40) a odpovedali, ich `turn/completed`
  a `subAgentActivity completed` prišli 27 a 40 s po prerušení.
  Interrupt hlavného teda subagentov nezabije (opak Claude C1/C2).
- Subagent 1 pred koncom poslal správu rodičovi: v JEHO vlákne
  `subAgentActivity kind:"interacted"`, `agentThreadId` = HLAVNÉ vlákno,
  `agentPath:"/root"` (obsah správy v položke nie je). Ťah na hlavnom vlákne
  to nespustilo.
- `thread/read` subagenta: `parentThreadId` = hlavné, `threadSource:"subagent"`,
  `source: {subAgent: {thread_spawn: {parent_thread_id, depth:1,
  agent_path:"/root/agent1", agent_nickname:"Peirce", agent_role:null}}}`,
  `agentNickname:"Peirce"`. Meno pre hovoriaceho teda je (`agentNickname`), ale
  len cez `thread/read`, nie v notifikáciách. `agentPath` sa líši medzi behmi
  (`/root/agent_1` vs `/root/agent1`) — volí ho model.

## spawn-kill — `turn/interrupt` na vlákno SUBAGENTA (mimo ťahu hlavného)
- `turn/interrupt {threadId: subagent, turnId: jeho turn z turn/started}` →
  `{}`, okamžite `thread/status/changed idle` + `turn/completed
  status:"interrupted"` na vlákne subagenta. Druhý subagent beží ďalej.
  Zastaviť jedného subagenta teda klient vie — bežnou metódou na jeho vlákno.
- Na hlavnom vlákne pritom NEPRIŠLO nič: ani `subAgentActivity
  kind:"interrupted"`, ani `completed` (typ `interrupted` v schéme je, ale
  vyvoláva ho asi až nástroj modelu `interruptAgent`; neoverené).
- Príkaz, ktorý subagent práve spúšťal (`ping -n 40`, unified exec,
  processId "…"), interrupt PREŽIL: jeho `item/completed` prišiel o 37 s neskôr
  so `status:"completed"`, `exitCode:0`, `durationMs` ~40 s, s threadId
  a turnId prerušeného ťahu. Proces sa prerušením ťahu nezabíja.

## spawn s `-c features.multi_agent_v2=true` (sub-spawn-v2.log)
Tvar rovnaký ako v1 (`subAgentActivity` started/completed, `wait`,
`thread/read` s nickname „Mill"/„Leibniz"). `collabAgentToolCall` s tool
`spawnAgent` sa ani tu neukázal; spawn sa hlási len ako `subAgentActivity`.

## bgterm — príkaz na pozadí (unified exec) a `backgroundTerminals`
Prvý pokus: model namiesto toho spustil `Start-Process ping.exe` (odpojený
proces, ktorý Codex nevidí; list prázdny). S výslovným pokynom
„exec_command … yield_time_ms 1500" to ide:
- `item/started commandExecution` {`processId:"64253"` (id relácie unified
  exec, nie PID OS), `source:"unifiedExecStartup"`, status inProgress}.
  Volanie modelu sa vráti po yield, ale `item/completed` NEPRÍDE — položka
  zostane inProgress aj po `turn/completed`.
- `item/commandExecution/outputDelta` chodí ďalej aj PO `turn/completed`
  (každú sekundu, s threadId a turnId skončeného ťahu).
- `thread/backgroundTerminals/list` po ťahu: `{data:[{itemId:"exec-…",
  processId:"64253", command:"ping -n 40 127.0.0.1", cwd, osPid:null,
  cpuPercent:null, rssKb:null}]}`. `itemId` = id položky commandExecution,
  takže sa dá spárovať s prepisom.
- `thread/backgroundTerminals/terminate {threadId, processId}` →
  `{terminated:true}`, hneď `item/completed commandExecution` `status:"failed"`,
  `exitCode:-1`, aggregatedOutput doterajší; list potom prázdny.
- Samovoľný ťah po dobehnutí/zabití procesu NEVZNIKOL.

## bgterm-int — `turn/interrupt` pri bežiacom procese na pozadí + clean
(sub-bgterm-int-1.log, sub-bgterm-int.log)
- `list` počas ťahu ukáže OBA procesy — aj ten, na ktorý model práve čaká
  v popredí. Zoznam = všetky živé procesy unified exec vlákna, nielen
  „na pozadí".
- `turn/interrupt` → `turn/completed status:"interrupted"`, ale OBA procesy
  žijú ďalej (list po ťahu má oba, outputDelta pokračuje). Ping 40 dobehol
  sám v 50 s: `item/completed status:"completed"`, `exitCode:0`, turnId
  prerušeného ťahu, bez nového ťahu.
- `thread/backgroundTerminals/clean {threadId}` → `{}`, oba hneď
  `item/completed status:"failed"`; list prázdny. Toto je „zastav všetky
  príkazy" (Codex TUI: „stop all background terminals").

## Záver pre port (porovnanie s Claude, probe_subagents.notes.md)
- Subagent v Codexe = vlákno (threadId) s vlastnými ťahmi; v Claude = úloha
  `task_id` + `parent_tool_use_id` v hlavnom streame. Spoločné: id, popis
  (Codex: `agentPath` + nickname cez `thread/read`; prompt NIE je v položke
  spawnu), druh „agent", štart a koniec v hlavnom vlákne, text subagenta
  v streame, beh súbežný s hlavným a aj mimo ťahu.
- Rozdiely: Codex nemá „popredie/pozadie" subagenta (vždy beží súbežne,
  hlavný si ho prípadne `wait`-uje), nemá Ctrl+B, nemá samovoľný ťah po
  dobehnutí, `turn/interrupt` hlavného subagentov NEZABÍJA. Zastaviť jedného
  = `turn/interrupt` na jeho vlákno (bez hlásenia v hlavnom vlákne).
  Zoznam subagentov si adaptér musí viesť sám zo `subAgentActivity`
  started/completed + `turn/*` a `thread/status/changed` ich vlákien.
- Príkaz na pozadí: Codex má zoznam (`backgroundTerminals/list`, ťahom, bez
  notifikácie), zastavenie jedného (`terminate`) aj všetkých (`clean`); Claude
  `local_bash` + `stop_task` + `background_tasks_changed` (push). Ekvivalent
  `background_tasks_changed` v Codexe nie je — ani pre agentov, ani pre
  terminály.

## Priebeh

- statika zo schémy hotová; `info`, `spawn`, `spawn-nowait`, `spawn-int`,
  `spawn-kill`, spawn v2, `bgterm`, `bgterm-int` (+ clean) hotové.
- Neoverené: `subAgentActivity kind:"interrupted"` (asi až nástroj modelu
  `interruptAgent`); `collabAgentToolCall` s inými nástrojmi než `wait`;
  či výsledok subagenta dobehnutého mimo ťahu dostane model v ďalšom ťahu;
  čo s procesmi a subagentmi urobí zavretie app-servera (stdin EOF);
  `item/commandExecution/terminalInteraction` naostro; povolenia
  (approvals) zo subagentského vlákna — tu approvalPolicy never.
- Stav: HOTOVO.
