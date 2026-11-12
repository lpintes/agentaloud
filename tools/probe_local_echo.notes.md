# probe_local_echo.py -- slash commands in the stream and on disk

Measured 2026-11-12, Claude CLI 2.1.322, `--model haiku`,
`--replay-user-messages`.  Steps: a plain prompt, `/clear`, `/context`,
`/probe-gamma` (a project skill the probe writes into `<dir>/.claude/skills`).

## Stream

- Plain prompt: echo `user` `isReplay`, uuid == the disk `user` record.
- `/clear`: no `user` record.  `conversation_reset` with `trigger: "clear"`,
  `new_conversation_id` and `user_message_uuid`.  `user_message_uuid` IS the
  uuid of the disk record `<command-name>/clear</command-name>`.
  `new_conversation_id` is NOT the id the CLI files the new conversation
  under (that one comes with the next `system/init`).
- Right after the reset, `system/background_tasks_changed` still carries the
  OLD `session_id`; only `hook_started` / `system/init` carry the new one.
  Taking the first `session_id` after the reset therefore keeps the old id
  (fixed by `proto::TrackSessionId`).
- `/context`: nothing with a uuid of the command; a `result` with no cost.
- `/probe-gamma` (skill, run by the model): echo `user` `isReplay` with text
  `<command-message>probe-gamma</command-message> <command-name>/probe-gamma...`,
  uuid == disk record.  `HumanPromptText` returns empty for it (starts with
  `<`), so no `PromptAnchored` comes of it today.

## Disk

- `/clear` sits at the START of the NEW conversation's file: `user` isMeta
  `<local-command-caveat>`, `user` `<command-name>/clear</command-name>
  <command-message>clear</command-message>...` with its own uuid, then
  `system` `local_command`.
- `/context` the same three records in the current file.
- The skill: `user` with the echo's uuid, then an isMeta `user` with the
  skill's text ("Base directory for this skill: ...").
- `TranslateHistory` drops every slash command record through
  `HumanPromptText`, so a resumed transcript shows none of them.

Arguments (corpus 2026-11-12): `<command-args>` after the name, empty when
none; a skill writes message, name, args on separate lines.

Not measured: whether a local command's disk record is written before its
`result`.
