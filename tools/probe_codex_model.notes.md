# probe_codex_model -- model change in codex app-server

codex-cli 0.160.0, native installer, ChatGPT account, 2026-10-29.
Schema from `codex app-server generate-json-schema --experimental --out <dir>`.
Every mode done (list, turn, settings, midturn, midbad, midstep, pending,
resume, effort).  Logs: `C:/b/codex-probe/logs/model-<mode>.log`.

The proof of which model a turn really used is the rollout on disk
(`<CODEX_HOME>/sessions/YYYY/MM/DD/rollout-...-<threadId>.jsonl`, path in
`thread.path`): one `turn_context` record per turn with `model` and
`collaboration_mode.settings.reasoning_effort`, plus an
`event_msg` `thread_settings_applied` record per settings change.  The wire
protocol does not say it: `Turn` (turn/started, turn/completed) and
`thread/tokenUsage/updated` have no model field.

## Schema (0.160.0)

- `turn/start`: `model` -- "Override the model for this turn and subsequent
  turns"; also `effort`, `summary`, `serviceTier` (all sticky) and
  `serviceTierForTurn` (the only per-turn-only one).
- `thread/settings/update` (needs `experimentalApi`): `threadId` required;
  `model`, `effort`, `summary`, `approvalPolicy`, `sandboxPolicy`,
  `collaborationMode`, `cwd`, ... -- "for subsequent turns".  Response `{}`.
- `thread/settings/updated` notification: `{threadId, threadSettings}`.
- `turn/settings/update`: `threadId`, `turnId` required; `model`, `effort`,
  `summary`, `serviceTier`, `approvalsReviewer`.  Response
  `{"status": "applied" | "targetUnavailable"}`.  Feature-gated.
- `turn/steer`: `threadId`, `expectedTurnId`, `input` -- no `model`.
- `thread/resume`: params have `model` (override); response has `model`,
  `reasoningEffort`.
- `model/rerouted` notification: `{threadId, turnId, fromModel, toModel,
  reason}`, `reason` enum only `"highRiskCyberActivity"`.  Never seen.

## 1. model in turn/start -- sticky, not per-turn

Three turns on a thread started with `"model": "gpt-6-luna"`: none,
`"model": "gpt-5.6-luna"`, none.  Rollout `turn_context.model`:
`gpt-6-luna`, `gpt-5.6-luna`, `gpt-5.6-luna`.  The turn/start with a model
emits, before its own response:

```json
{"method": "thread/settings/updated", "params": {"threadId": "<thread>",
 "threadSettings": {"disabledPluginIds": [], "cwd": "<cwd>",
  "approvalPolicy": "never", "approvalsReviewer": "user",
  "sandboxPolicy": {"type": "readOnly", "networkAccess": false},
  "activePermissionProfile": null, "model": "gpt-5.6-luna",
  "modelProvider": "openai", "serviceTier": null, "effort": null,
  "summary": null,
  "collaborationMode": {"mode": "default", "settings": {
    "model": "gpt-5.6-luna", "reasoning_effort": null,
    "developer_instructions": null}},
  "multiAgentMode": "explicitRequestOnly", "personality": null}}}
```

A turn/start without `model` emits no thread/settings/updated.

## 2. thread/settings/update

```json
> {"id": 4, "method": "thread/settings/update",
   "params": {"threadId": "<thread>", "model": "gpt-5.6-luna"}}
< {"method": "thread/settings/updated", "params": {... "model": "gpt-5.6-luna" ...}}
< {"id": 4, "result": {}}
```

The notification comes before the response.  The next turn uses the new
model (rollout).  The model is not validated: `"model": "no-such-model-xyz"`
also returns `{}` and the notification; only the next turn fails:

```json
{"method": "error", "params": {"error": {"message": "{\"type\":\"error\",\"status\":400,\"error\":{\"type\":\"invalid_request_error\",\"message\":\"The 'no-such-model-xyz' model is not supported when using Codex with a ChatGPT account.\",\"code\":\"model_not_found\"}}", "codexErrorInfo": "other", "additionalDetails": null, "misalignment": null}, "willRetry": false, "threadId": "<thread>", "turnId": "<turn>"}}
```

then `turn/completed` with `"status": "failed"` and the same `error`.

## 3. Change while a turn runs

Turn on gpt-6-luna running a ~8 s shell command; mid-command:

- `thread/settings/update` model -> `{}` + thread/settings/updated at once,
  but the running turn keeps its model: set to `no-such-model-xyz` mid-turn,
  the turn still completed (its post-command inference ran fine); the next
  turn failed with model_not_found.  So: applies to the next turn.
- `turn/start` during the running turn is NOT rejected and does not start a
  turn: response carries the running turn's id (it is a steer, the text was
  answered in the same turn: "ok BANANA KIWI"), and its `model` becomes the
  thread setting (thread/settings/updated with that model) -- for the next
  turn, not the running one.
  `{"id": 6, "result": {"turn": {"id": "<running turn>", "status": "inProgress", ...}}}`
- `turn/steer` with an extra `"model"` field -> `{"result": {"turnId":
  "<running turn>"}}`; the field is ignored, no settings notification.
- `turn/settings/update` without the feature:
  `{"error": {"code": -32600, "message": "turn settings updates require the step_model_switching feature"}}`
  (also after the turn ended).
- With `codex app-server --enable step_model_switching`
  (`codex features list`: "under development", default false):
  bogus model -> `{"error": {"code": -32600, "message": "the destination
  model has only fallback metadata"}}`; valid model ->
  `{"result": {"status": "applied"}}`, no notification.  It really applies
  mid-turn: rollout `world_state` for the post-command request shows
  `collaboration_mode.model: "gpt-5.6-luna"`, and the next turn is back on
  the thread model gpt-6-luna (turn-only override, thread setting untouched).

## 4. thread/resume in a new process

- Thread whose last turn ran on gpt-5.6-luna (set by turn/start.model):
  `thread/resume {"threadId": ..., "excludeTurns": true}` ->
  `"model": "gpt-5.6-luna", "reasoningEffort": null`.
- Thread whose change was only `thread/settings/update {"model":
  "gpt-5.6-luna", "effort": "low"}` with no turn after it, process closed:
  resume -> `"model": "gpt-5.6-luna", "reasoningEffort": "low"`, and the next
  turn ran on gpt-5.6-luna/low.  The change is persisted at once
  (`thread_settings_applied` in the rollout).
- No thread/settings/updated on resume.

## 5. model/list and effort

```json
{"id": "gpt-6-luna", "model": "gpt-6-luna", "displayName": "GPT-6-Luna",
 "description": "Fast and affordable model for easier tasks.", "hidden": false,
 "supportedReasoningEfforts": [
   {"reasoningEffort": "low", "description": "Fast responses with lighter reasoning"},
   {"reasoningEffort": "medium", "description": "Balances speed and reasoning depth for everyday tasks"},
   {"reasoningEffort": "high", "description": "Greater reasoning depth for complex problems"},
   {"reasoningEffort": "xhigh", "description": "Extra high reasoning depth for complex problems"},
   {"reasoningEffort": "max", "description": "Maximum reasoning depth for the hardest problems"}],
 "defaultReasoningEffort": "medium", "isDefault": true, ...}
```

Models: gpt-6-luna (default; low..max), gpt-5.6-terra (low..max, ultra),
gpt-5.6-luna (low..max).  `ReasoningEffort` in the schema is any non-empty
string.

A model change neither requires nor resets effort: after `effort: "high"`,
`thread/settings/update {"model": "gpt-5.6-luna"}` reports
`"effort": "high"` and the next turn_context keeps high.  Effort is not
validated by the app-server: `"ultra"` on gpt-6-luna (not in its list) was
accepted and the turn completed; `"bogus-effort"` was accepted by
turn/start, the turn failed with
`"Invalid response.create payload: Invalid value: 'bogus-effort'. Supported values are: 'none', 'minimal', 'low', 'medium', 'high', 'xhigh', and 'max'."`
(400, `turn/completed` status failed).  Fresh thread reports
`"reasoningEffort": null` (model default).

### Back to the default, and mode changes (`effortreset`, 2026-10-30)

`thread/settings/update {"effort": null}` answers `{}` and **does nothing**: no
notification, and the next turn_context still says `low`.  The way back is
the collaboration mode: `collaborationMode.settings.reasoning_effort: null`
gives `thread/settings/updated` with `"effort": null` and the next turn has no
effort.

The same field means every mode change sets the effort: sent with `null` after
`effort: "high"`, the next turn has none; sent with `"low"` it stays low; sent
with `"medium"` it becomes medium.  A mode change therefore has to name the
effort in force or it resets it.

## 6. Notifications

`thread/settings/updated` (full `threadSettings`) after every
thread/settings/update and every turn/start carrying `model`/`effort`, before
the response.  None for turn/settings/update, turn/steer, thread/resume.
`model/rerouted` exists in the schema (reason `highRiskCyberActivity`) but
never fired.  Turn and token-usage notifications carry no model.
