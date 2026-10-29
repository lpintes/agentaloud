# probe_set_model — control_request set_model (2026-10-29)

CLI 2.1.304. Script: `python -I tools/probe_set_model.py <dir> <steps...> [-- cli args]`.
Raw stream in `<dir>/raw.jsonl` (outside the repo).

Status: DONE (runs 1-4 below).

## Run 1 (no prompts, no credit) -- DONE
Steps `set:haiku init set:bogus-model-xyz set:null set:omit init`, no --model.
- success: `{"type":"control_response","response":{"subtype":"success","request_id":"model-2"}}` (no payload) for haiku, null, omit.
- unknown: `{"type":"control_response","response":{"subtype":"error","request_id":"model-4","error":"Model 'bogus-model-xyz' not found","error_code":"catalog_unknown"}}`
- initialize response has no current-model field (keys: account, agents, capabilities, claude_code_version, commands, current_permission_mode, models, session_state, ...), so a second initialize cannot confirm the change.
- No spontaneous record after set_model.

## Run 2 (no --model; default resolves to claude-opus-5-5) -- DONE
Steps `set:haiku prompt set:null prompt set:haiku set:omit prompt set:haiku tool:sonnet prompt`.
- set:haiku before the first turn -> next `system/init` `"model":"claude-haiku-5-5"`, `assistant.message.model` `claude-haiku-5-5`.
- `"model":null` and omitted key both = reset to default: CLI runs `/model default`, next init/assistant `claude-opus-5-5`.
- Mid-turn (sent right after the assistant record with `tool_use`, before the tool_result): accepted (`success`), and the SAME turn's next API call answered with `claude-sonnet-5-5` (that turn's system/init said haiku). Next turn's init says sonnet.
- Spontaneous record: no system/status. But every set_model AFTER the first turn is echoed on stdout as a `user` record with `isReplay:true` even without `--replay-user-messages` (literal, ids scrubbed):
  `{"type":"user","message":{"role":"user","content":"<local-command-stdout>Set model to `haiku (claude-haiku-5-5)`</local-command-stdout>"},"session_id":"<sid>","parent_tool_use_id":null,"uuid":"<uuid>","timestamp":"2026-10-29T12:10:06.528Z","isReplay":true}`
  For reset: content `<local-command-stdout>Set model to `claude-opus-5-5`</local-command-stdout>`. Sonnet: `Set model to `sonnet (claude-sonnet-5-5)``.
  set_model before the first turn: no echo (also run 1).
- Disk: set_model = local slash command `/model`. Three `user` records (promptId, entrypoint "sdk-cli"):
  1. content `<local-command-caveat>The command below was run directly in Claude Code, not sent to you as a request, and its output goes straight to the user. ...`
  2. content `<command-name>/model</command-name>\n            <command-message>model</command-message>\n            <command-args>haiku</command-args>` (null/omit -> `<command-args>default</command-args>`)
  3. content `<local-command-stdout>Set model to `haiku (claude-haiku-5-5)`</local-command-stdout>` (same uuid as the stream echo)
  Plus the next turn gets an `attachment` `{"type":"model","identity":{"modelId":"claude-sonnet-5-5","marketingName":"Sonnet 5.5",...},"text":"You are powered by the model named ..."}`.
  Pre-first-turn set_model: no /model records on disk. Mid-turn set_model: records written after the turn's final assistant.
  NOTE for app: history restore must not treat these `user` records as prompts (HumanPromptText).

## Run 3 (resume run 2 session, no --model) -- DONE
system/init and assistant `claude-sonnet-5-5` (the last set_model), not the default opus.

## Run 4 (start `--model haiku`, prompt, set:sonnet, exit WITHOUT another turn; then resume, no --model) -- DONE
Echo for set:sonnet arrived on stdout, but NO /model record on disk, and the resumed session answered with `claude-haiku-5-5`.
=> resume follows the model of the last turn in the history (last assistant / model attachment), not the set_model itself; a set_model with no following turn is lost on resume.
