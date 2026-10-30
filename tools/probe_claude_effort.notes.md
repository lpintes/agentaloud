# probe_claude_effort -- reasoning effort of a running session (2026-10-30)

CLI 2.1.304. Script: `python -I tools/probe_claude_effort.py <dir> <steps...> [-- cli args]`.
Raw stream in `<dir>/raw.jsonl` (outside the repo).

There is no `set_effort` subtype. `/effort` and the SDK both go through:

```json
{"type":"control_request","request_id":"effort-3","request":
 {"subtype":"apply_flag_settings","settings":{"effortLevel":"low"}}}
```

Answer: `{"type":"control_response","response":{"subtype":"success","request_id":"effort-3"}}`,
no payload and no spontaneous record (no system/status, no echo, nothing on disk
but the request itself).

The proof of what a request carried is the session file on disk: record
`api-request`, `params.output_config.effort`. `success` proves nothing, see
below. A second view is `get_settings`: `response.applied.effort`.

## Handshake

`models[]` entries carry `supportsEffort` and `supportedEffortLevels`:

- most models: `low, medium, high, xhigh, max`
- claude-opus-4-6, claude-sonnet-4-6: `low, medium, high, max` (no xhigh)
- claude-haiku-4-5-20251001: both fields absent (no effort at all)

No `defaultEffort` field. `resolvedModel` is on the entry: alias `opus` ->
`claude-opus-5-5`, which is what `assistant.message.model` then reports.

## Behaviour

- `low`, `max` before a turn or between turns: the next request carries it.
  `max` applies although the settings schema lists only low..xhigh.
- `"effortLevel": null`: back to the default (`applied.effort` medium, request
  `medium`). It also clears a startup `--effort high` (run 5).
- `"effortLevel": "bogus"`: **`success`, silently ignored**; `applied.effort`
  stays what it was. So only levels from the catalog may be offered.
- Mid-turn (right after the assistant record with `tool_use`): `success`, and
  the request after the tool result already carries it. Applies in the turn.
- `--resume` does not remember it: `effort:low`, one turn, close, resume ->
  `applied.effort` medium. No trace of the effort is used on resume.
- Model change keeps the choice and the CLI clamps it per request:
  `xhigh` then set_model claude-sonnet-4-6 -> request `high`, flagSettings still
  `xhigh`; set_model claude-haiku-4-5-20251001 -> request without output_config.
  `max` then set_model haiku -> `max`.
- `get_settings` right after set_model, before a turn, may still name the old
  model in `applied.model` (run 4, get-4); do not lean on it for the model.
