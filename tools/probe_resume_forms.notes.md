# probe_resume_forms -- how the CLI spells an option's value (2026-11-05)

CLI 2.1.314. No script; one command per form, from an empty directory, with an
id that names no conversation, so a recognised resume ends before any model
call:

```
claude -p <form> --output-format json "hi"
```

Z = `00000000-0000-0000-0000-000000000001`.

- `--resume Z`, `--resume=Z`, `-rZ`: "No conversation found with session ID: Z",
  exit 1. All three are a resume of Z.
- `-r=Z`: "--resume requires a valid session ID or session title ... Provided
  value "=Z" is not a UUID". Still a resume -- of the value `=Z`, with the
  equals sign kept.
- `--session-id=Z --resume=Z`: "--session-id can only be used with --continue
  or --resume if --fork-session is also specified", exit 1. Both `=` forms are
  read as the options themselves.
- `--session-id=Z` alone: runs a turn under id Z (costs a real turn).

So every option the app reads out of `extraArgs` has to be recognised in all
of these spellings (`proto::OptionValues`); before claude-gui-lkk.96 the app
knew only the separate word, added its own `--session-id` to `--resume=Z`, and
the CLI exited 1 with nothing visible.

Checked end to end: `agentaloud.exe <dir> -- --resume=Z` on an existing
conversation shows its history and the CLI keeps running.
