# probe_codex_clear — /clear ako v TUI: druhé `thread/start` v tom istom procese (2026-11-10)

codex-cli 0.167.0 (natívny inštalátor), gpt-6-luna, effort low, approvalPolicy
never. Logy: `C:/b/codex-probe/logs/clear-<scenár>[-unsub].log`.
Spustenie: `python tools/probe_codex_clear.py <scenár> [--unsub]` (bez `-I`:
izolovaný režim vyhodí priečinok skriptu zo `sys.path` a `probe_codex_rpc`
sa nenájde).

## clear — ťah v A, potom `thread/start` B so `sessionStartSource: "clear"`
- Druhé `thread/start` v tom istom procese prejde; odpoveď má ten istý tvar
  ako prvé (nové `thread.id` = `sessionId`, `model`, `approvalPolicy`…).
  Prichádza `thread/started` pre B a `mcpServer/startupStatus/updated`.
  `sessionStartSource` nie je experimentálne pole.
- B z A nepozná nič: na „What word did I ask you to remember?" odpovedal
  `NONE` (A dostalo „Remember the word PELICAN").
- `thread/name/set {threadId: B, name}` → `{}` a notifikácia
  `thread/name/updated`; `thread/read` B potom nesie `name`.
- A zostáva načítané (`thread/loaded/list` vráti A aj B), `status idle`.
- Medzi `thread/start` a prvým ťahom B príde `thread/settings/updated` pre B.

## clear-sub — v A dvaja subagenti bez čakania, hneď po ťahu /clear
- Bez `thread/unsubscribe`: vlákna subagentov streamujú ďalej (príkazy, text,
  `turn/completed`), a na vlákne **A** príde `subAgentActivity kind:completed`
  pre každého — teda položka starého vlákna po tom, čo appka prešla na B.
- S `thread/unsubscribe A` (→ `{status: "unsubscribed"}`): vlákna subagentov
  streamujú ďalej rovnako (sú to samostatné odbery), ale `subAgentActivity
  completed` na A **nepríde**.

## clear-bg — v A príkaz na pozadí (unified exec, `ping -n 30`), po ťahu /clear
- Bez `thread/unsubscribe`: `outputDelta` aj `item/completed` príkazu chodia
  ďalej s `threadId` A (`item/completed` ~20 s po /clear).
- S `thread/unsubscribe A`: od odpovede na unsubscribe z A nepríde **nič**,
  ani `item/completed`. A je pritom stále v `thread/loaded/list`; že proces
  dobehol, sa klient nedozvie.

## Dôsledok pre appku
- Úlohy starého vlákna (príkazy aj subagenti) /clear prežijú. Bez unsubscribe
  o nich chodia správy s `threadId` A, ktoré `CodexBackend::OnLine` dnes podľa
  `threadId_` nepovažuje za vlastné; `StopTask` príkazu volá `threadId_`,
  teda po /clear B, nie A.
- S unsubscribe sa o príkaze A appka už nič nedozvie (visel by ako bežiaci),
  subagenti A by ďalej hlásili.
