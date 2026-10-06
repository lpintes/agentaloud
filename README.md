# AgentAloud

A native Windows window for talking to coding agents — Claude Code and
Codex — instead of a terminal. Written first for its author, and with blind
users of the NVDA screen reader in mind throughout.

**The user interface is English and Slovak.** It follows the language of
Windows — Slovak on a Slovak system, English everywhere else — and
`language=en` or `language=sk` in the settings file overrides that.

## Why not the terminal

A terminal is one flat buffer of characters with no structure. A
conversation with a coding agent has plenty: prompts, answers, tool calls
and their output, questions, permission requests. With a screen reader the
difference is everything — in a terminal you hear the screen being redrawn;
here you move by answer, by tool call, by output, and nothing moves your
cursor while you read.

AgentAloud does not emulate a terminal. It runs the agent's own headless
mode, which sends structured JSON, and draws the same conversation
differently:

- the transcript is a read-only text field you can walk with the usual keys,
  with single-letter jumps between prompts, answers, tool calls and output,
  and blocks that fold to one line;
- a running turn is spoken in the order things happen — one sentence per
  tool, not token by token — and stays quiet while you are reading elsewhere;
- permission requests and the agent's multiple-choice questions are real
  dialogs, showing exactly the command that will run;
- Shift+Tab switches the permission mode, as in the terminal; F1 lists every
  key, F2 shows the session's details.

## Requirements

- Windows 10 (version 1803 or later) or Windows 11, 64-bit.
- **Claude Code** — the `claude` command on the PATH, signed in.
- **Codex** (optional, `--backend codex`) — installed with the native
  installer, not from npm:
  `powershell -c "irm https://chatgpt.com/codex/install.ps1 | iex"`.
  npm only puts script shims on the PATH, and those cannot be started
  directly.
- **NVDA** (optional) for speech. Without NVDA running, the application works
  silently; without `nvdaControllerClient.dll` beside the EXE it says so at
  start-up.

## Installing

Download `AgentAloud.zip` from the
[latest release](https://github.com/lpintes/agentaloud/releases/latest) and
unpack it into a folder you can write to — not Program Files. AgentAloud
updates itself from the releases page and replaces its own files when it
does, so it needs that folder to be writable.

## Running

```
agentaloud [options] [folder] [-- options for the CLI]
```

The folder is the project the agent works in. Without it, AgentAloud asks
for one. `agentaloud --help` lists every option; the most used are:

- `--backend claude|codex` — which agent (Claude by default);
- `--permission-mode <mode>` — the starting permission mode;
- `--model <name>`;
- `-c` — continue the latest conversation in the folder;
  `--resume <id>` — continue a given one.

Anything after a lone `--` goes to the CLI unchanged.

## Settings

`%APPDATA%\AgentAloud\settings.txt`, or `config\settings.txt` beside the EXE
if that folder exists (portable mode). The file is written by hand for now;
one `key=value` per line, `#` starts a comment:

```
claude.permission-mode=auto
claude.model=opus
codex.permission-mode=plan
check-updates=0
language=en
```

The command line wins over the file. An unknown key or mode is refused at
start-up with the line it is on, rather than silently ignored.

## Updates

Once a day at start-up AgentAloud asks GitHub whether there is a newer
release and offers it: update, later, or skip this version. The whole
package is downloaded, checked against the published SHA-256 sum and only
then put in place; if any file cannot be replaced, nothing is. `check-updates=0`
turns the check off.

## Building from source

With [MSYS2](https://www.msys2.org/) and its UCRT64 toolchain:

```
pacman -S mingw-w64-ucrt-x86_64-gcc make git
./build.sh          # application, tests and the protocol spike
./build.sh check    # build and run the tests
```

The result is `bin/agentaloud.exe`, a single statically linked EXE, with
`nvdaControllerClient.dll` copied beside it.

`CLAUDE.md` (identical to `AGENTS.md` from its "AgentAloud" heading on) is
the project's design notes and rules. They are in Slovak.

## License

MIT — see `LICENSE.txt`. `nvdaControllerClient.dll` is part of NVDA and is
distributed under the GNU LGPL 2.1 (`vendor/nvda/LICENSE.txt`); it is loaded
at run time and not linked in.
