# probe_anchor_claude — are record uuids stable between stream and disk?

Question: can `uuid` (and `message.id`) of stream records be used as a
persistent bookmark anchor that survives `--resume` (which replays the disk
file)?

Run: `python -I tools/probe_anchor_claude.py <empty-temp-dir>`
Raw stream in `<dir>/raw1.jsonl`, `<dir>/raw2.jsonl`, disk copies in
`disk1.jsonl`, `disk2.jsonl` (outside the repo).

Status: DONE. One run, CLI 2.1.322, haiku, 2026-11-12.

## Findings
- (a) Every stream `assistant` record (thinking, text, tool_use; one content
  block each) has a disk record with the same `uuid`, same `message.id` and
  deep-equal `message.content`. The `user` tool_result record likewise
  (same uuid, equal content). Stream records carry no `parentUuid`; disk
  does.
- `message.id` is NOT unique per record: thinking + text + tool_use of one
  API response share one `message.id` (three disk records, three uuids).
  Only `uuid` identifies one block.
- (b) With `--replay-user-messages` the echo of a normal prompt (prompt 1
  and the post-resume prompt) has a `uuid` equal to the disk `user`
  record's uuid. Echo of prompt 1 arrives only after the first API call
  (just before the first assistant record), as probe_midturn found.
- (c) Mid-turn prompt (absorbed, `remove reason: absorbed_mid_turn`): NO
  `user` record on disk. It exists only as `attachment` type
  `queued_command` whose own `uuid` is fresh (8a34c2ee…) and differs from
  the echo; the echo uuid appears as `attachment.source_uuid` and as
  `commandUuid` of the `queue-operation remove` record (`delivery_id`
  also present).
- (d) After `--resume <id>` + one prompt: phase-1 file is a byte-identical
  (parsed-equal) prefix of the new file; no uuid missing, changed or
  duplicated; new records only appended; the resumed stream does not
  re-emit old records and reuses no old uuid.

## Implication for bookmarks
uuid works as an anchor for assistant, tool_result and normal prompt
blocks. For an absorbed mid-turn prompt the restored block (built from the
`queued_command` attachment, invariant 18) must take its anchor from
`attachment.source_uuid`, not from the attachment record's `uuid`.

## Not verified
- Queued (not absorbed) mid-turn prompt that becomes its own turn — per
  probe_midturn it is a plain `user` record on disk; uuid match not checked.
- Interrupted turn, /clear, subagent (sidechain) records, compaction.
