# probe_subagents — viac subagentov naraz (2026-10-08)

CLI 2.1.293, `--model haiku`, argumenty ako appka, `can_use_tool` → allow,
dočasný priečinok mimo repozitára (`%TEMP%\probe_sub_<scenár>`), surové
záznamy tam (`raw-<scenár>.jsonl`). Spustenie:
`python -I tools/probe_subagents.py <dir> fg|bg|bg-int|bg-self [s] [model]`.
Sonda odstraňuje z prostredia premenné `CLAUDE*` rodičovského Claude Code
(okrem `CLAUDE_CODE_GIT_BASH_PATH`), aby CLI bežalo ako z appky.

Stav: HOTOVO, pozri koniec súboru.

## Agent bez run_in_background = NA POZADÍ
CLI 2.1.293: `shouldRunAsync` = `wantsBackground !== false` (pre hlavného
agenta; binárka, funkcia s `forceAsync`/`callerIsHeadlessSubagent`). Prvý
pokus A bez parametra dal tool_result „Async agent launched successfully"
(`tool_use_result.status=async_launched`, `isAsync=true`). Popredie len
s výslovným `run_in_background: false`.

## A — traja v popredí (`run_in_background: false`), haiku poslúchol
Tri volania Agent prídu ako TRI samostatné `assistant` záznamy (jeden
tool_use každý, ~0.5 s od seba), každý hneď nasleduje:
`system/task_started` (task_type `local_agent`, `is_backgrounded:false`,
`tool_use_id` = id volania Agent, `description`, `subagent_type`,
`spawn_depth:1`, `prompt`) → `user` <ptu=volanie> [text = prompt subagenta].
`background_tasks_changed` v popredí NEPRÍDE vôbec.

Potom sa záznamy troch subagentov striedajú (prekladajú, nie po blokoch):
`system/task_progress` (task_id agenta, `tool_use_id` = volanie Agent,
`description` „Running …", `usage{total_tokens,tool_uses,duration_ms}`,
`last_tool_name`) → `assistant` <ptu> [tool_use Bash] → `control_request
can_use_tool` (bez rozlíšenia, ktorý agent; v request je asi agent_id —
neoverené) → … → `system/task_started` (task_type `local_bash`,
`owned_by_subagent:true`, `parent_task_id` = task agenta,
`is_backgrounded:false`) prichádza až ~3 s po spustení, tesne pred
`system/task_notification` (status completed, `output_file:""`) →
`user` <ptu> [tool_result].
`tool_progress` <ptu> (tool_name Agent, `heartbeat:true`,
`elapsed_time_seconds:30`) raz za 30 s behu.
Koniec agenta: `system/task_updated` (patch status completed, end_time) →
`system/task_notification` (output_file = .output prepis, `summary` =
posledný text, `usage`) → `user` BEZ ptu [tool_result volania Agent:
„[Subagent hand-back] … The report follows: …"]. Výsledky Agent chodia
v poradí dokončenia (3, 1, 2), nie volania.
Text/thinking subagenta v popredí v streame NIE JE — `assistant` s ptu nesú
len tool_use (v B áno, pozri nižšie). Žiadny `system/init` navyše.
Hlavný ťah: jeden `result` až po všetkých troch (40 s).

## B — traja na pozadí (`run_in_background: true`)
Spustenie: ako A, ale po každom `assistant` [tool_use Agent] ide
`system/background_tasks_changed` (tasks rastie 1→2→3; prvok: task_id,
run_id, task_type `local_agent`, subagent_type, description) →
`system/task_started` (`is_backgrounded:true`) → `user` BEZ ptu
[tool_result „Async agent launched successfully …" + `tool_use_result`
{status async_launched, agentId, outputFile}]. Prompt subagenta ako `user`
s ptu tu NEPRÍDE. Hlavný `result` po 7 s.
Po `result` (mimo ťahu) prúdia záznamy subagentov rovnako ako v A
(task_progress, assistant <ptu> [thinking/text/tool_use], can_use_tool,
local_bash task_started/notification, user <ptu> tool_result), striedavo.
Tu chodí aj text a thinking subagentov („agent N: step k").
Koniec každého: assistant <ptu> [posledný text] → task_updated (completed)
→ task_notification (summary, usage, output_file) →
background_tasks_changed (n o 1 menej, nakoniec `[]`).
`user` s `<task-notification>` ani tool_result „hand-back" na stdout neprišiel.
Samovoľné ťahy: TRI `system/init` + `result`, ale len dva s obsahom:
36.1 s init (po agentovi 1; agent 2 dobehol 0.1 s pred init) → text
„Agent 1 dokončil" → result; 38.9 s init → result hneď (num_turns 0,
duration_api_ms 0, result "", žiadny assistant) — prázdny ťah, notifikácia
agenta 2 sa zrejme zlúčila do prvého; 39.2 s init → text o agentovi 3 →
result. Čiže jeden ťah na notifikáciu, zlúčenie môže dať prázdny ťah.
`system/init` s ptu nikdy; navyše init len na začiatku samovoľných ťahov.

## C1 — interrupt mimo ťahu (3 s po result, agenti bežia)
Hneď (do 0.1 s), pre každého agenta: task_updated (patch status `killed`)
→ task_notification (status `stopped`) → background_tasks_changed (n−1 …
`[]`); potom control_response success. Žiadny `result`, žiadny samovoľný
ťah. O 5 s neskôr dobehnú `user` <ptu> [tool_result „The user doesn't want
to proceed…"] + `user` <ptu> [text „[Request interrupted by user for tool
use]"] za každého agenta. Interrupt mimo ťahu teda ZABIJE všetkých
subagentov na pozadí.

## C2 — interrupt hneď po init samovoľného ťahu (agent 3 dobehol, 1 a 2 bežia)
Rovnako: agenti 1 a 2 killed/stopped, background_tasks_changed → `[]`,
control_response success, user <ptu> „[Request interrupted…]", user
„[Request interrupted by user]" a `result/error_during_execution`
is_error=true pre samovoľný ťah. Ďalší ťah už nevznikol.
(`system/agents_killed` „All background agents stopped" v binárke je, na
stdout v C1/C2 neprišiel.)

## Control requesty pre úlohy (binárka 2.1.293, schéma SDK)
- `stop_task {task_id}` — „Stops a running task."
- `background_tasks {tool_use_id?}` — bez id = Ctrl+B pre všetky úlohy
  v popredí (Bash aj subagenti); odpoveď `{backgrounded?: bool}`.
- `get_task_output {task_id}` — posledných 8 KiB výstupu shell/Monitor
  úlohy, bez ťahu modelu; odpoveď `{output,total_bytes,truncated}`.
- `send_task_message` — reťazec v zozname podporovaných, schému som nečítal.
- Zoznam úloh ako control_request nie je; stav nesie
  `system/background_tasks_changed.tasks`.
Overenie naostro: scenáre `bg-stop`, `fg-bgall` (nižšie).

## bg-stop — stop_task na agenta 2 (3 s po result) — OVERENÉ
task_updated (killed) → task_notification (stopped) →
background_tasks_changed (n=2) → control_response success `{}`; 5 s potom
user <ptu> tool_result „doesn't want to proceed" + „[Request interrupted by
user for tool use]". Ostatní dvaja bežia ďalej. Zastavenie VYVOLÁ samovoľný
ťah (init → „Agent 2 bol zastavený" → result), potom ďalšie dva po
dobehnutí agentov 3 a 1.

## fg-bgall — background_tasks bez tool_use_id počas fg ťahu — OVERENÉ
Pre každého agenta: background_tasks_changed (n rastie) → task_updated
(patch `{is_backgrounded:true}`) → control_response success `{}` → tri
`user` [tool_result „Async agent launched successfully…"] (ako pri bg) →
hlavný ťah pokračuje a skončí `result` (13.5 s). Odtiaľ ako B; od presunu
chodia aj text bloky subagentov (v popredí nie). Samovoľné ťahy: 3× init,
jeden opäť prázdny (num_turns 0).

## fg-bash — background_tasks na Bash v popredí (9. 10. 2026) — OVERENÉ
Bez subagentov, `ping -n 60` v popredí. `system/task_started` (local_bash,
`is_backgrounded:false`) príde až **~8 s po** `tool_use` (obe merania 8 s).
Žiadosť PRED ním: `control_response success {}` a nič — príkaz dobehne
v popredí, žiadny `background_tasks_changed`. Odpoveď presun teda
nerozlišuje. Žiadosť 2 s PO ňom: v tej istej milisekunde
`background_tasks_changed` (n=1) → `task_updated` `{is_backgrounded:true}`
→ `control_response success {}`; potom `user` [tool_result „Command was
manually backgrounded by user with ID: …"] a ťah pokračuje k `result`.
Čiže o presune hovorí len `task_updated` pred odpoveďou, nie odpoveď.

## bg-bash — interrupt a stop_task na príkazy na pozadí (9. 10. 2026) — OVERENÉ
Dva `ping` s `run_in_background`. Interrupt 3 s po `result` (mimo ťahu):
`control_response success` a **nič** — oba príkazy bežia ďalej, žiadny
task_updated ani background_tasks_changed. Interrupt teda zastaví len
subagentov (C1), nie príkazy. `stop_task` na príkaz 1: task_updated
(killed) → task_notification (stopped) → background_tasks_changed (n=1)
→ success `{}`, a **žiadny samovoľný ťah** (29 s ticho) — na rozdiel od
subagenta (bg-stop). Príkaz 2 zabilo až zatvorenie stdin.

Stav: HOTOVO (A, B, C1, C2, bg-stop, fg-bgall, fg-bash, bg-bash).
