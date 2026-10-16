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
  dialogs, showing exactly the command that will run; "Allow for this
  session" says beforehand what exactly it will allow, and a subagent's
  request names the subagent;
- a plan from plan mode has its own dialog: approve it and choose the mode
  the session goes on in, or send it back with what to change;
- Ctrl+Space switches the permission mode (Shift+Tab in the terminal); F1 lists every
  key, F2 shows the session's details — id, model, context, cost — and F4
  offers the agent's slash commands;
- Ctrl+F and F3 search the transcript, collapsed blocks included;
  Ctrl+Shift+1 to 9 bookmark a block and Ctrl+1 to 9 return to it;
  Alt+Enter opens a link;
- subagents and background commands are named in the transcript; Ctrl+T
  lists the background tasks and stops one, Ctrl+B moves what the turn
  waits for to the background, and Esc twice outside a turn stops them all;
- the end of a turn and an agent waiting for an answer have their own
  tones, repeated every 30 seconds while the window is in the background;
- several sessions, each in its own window inside the main one: Ctrl+N
  opens a new one — in another folder, with another agent, or carrying on
  an older conversation picked from a list — and Ctrl+Tab moves between
  them;
- an agent that is not signed in says so, and Ctrl+L signs it in through
  the browser, with the address and code in a dialog.

## Requirements

- Windows 10 (version 1803 or later) or Windows 11, 64-bit.
- **Claude Code** — the `claude` command on the PATH, or wherever
  `claude.executable` in the settings points. Signing in can be done from
  AgentAloud with Ctrl+L.
- **Codex** (optional, `--backend codex`) — installed with the native
  installer, not from npm:
  `powershell -c "irm https://chatgpt.com/codex/install.ps1 | iex"`.
  npm only puts script shims on the PATH, and those cannot be started
  directly. A portable `codex-x86_64-pc-windows-msvc.exe` works too, through
  `codex.executable`.
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

The folder is the project the agent works in. Without it, AgentAloud opens
the New session dialog, where the folder, the agent and the model are
chosen and an older conversation of the folder can be picked to carry on.
`agentaloud --help` lists every option; the most used are:

- `--backend claude|codex` — which agent (Claude by default);
- `--permission-mode <mode>` — the starting permission mode;
- `--model <name>`;
- `--executable <path>` — the agent's CLI, when it is not on the PATH or is
  called something else;
- `-c` — continue the latest conversation in the folder;
  `--resume <id>` — continue a given one; `--resume` alone opens the list
  of the folder's conversations;
- `--check-updates` — look for a new version right now and say the result
  even when there is none;
- `--version`.

Anything after a lone `--` goes to the CLI unchanged. An option before it
that AgentAloud does not know is refused, not passed on.

## Settings

`%APPDATA%\AgentAloud\settings.txt`, or `config\settings.txt` beside the EXE
if that folder exists (portable mode). The file is written by hand for now;
one `key=value` per line, `#` starts a comment:

```
backend=codex
claude.permission-mode=auto
claude.model=opus
codex.permission-mode=plan
codex.executable=codex\codex-x86_64-pc-windows-msvc.exe
codex.env.CODEX_HOME=E:\codex
check-updates=0
sounds=0
language=en
```

- `<backend>.executable` is that agent's CLI; a relative path is relative to
  the folder of `agentaloud.exe`, so a portable installation can carry its
  CLI along.
- `<backend>.env.<NAME>` sets an environment variable for the CLI, such as
  `CODEX_HOME` or `CLAUDE_CONFIG_DIR` when its configuration is not in the
  user profile.
- `sounds=0` turns the tones off.

The command line wins over the file. An unknown key or mode is refused at
start-up with the line it is on, rather than silently ignored. AgentAloud
itself writes only the time of the last update check, and leaves the other
lines as they are.

## Updates

Once a day at start-up AgentAloud asks GitHub whether there is a newer
release and offers it: update, later, or skip this version. The whole
package is downloaded, checked against the published SHA-256 sum and only
then put in place; if any file cannot be replaced, nothing is. `check-updates=0`
turns the daily check off. The Help menu (Alt) and `--check-updates` check
at once and always answer.

## Building from source

With [MSYS2](https://www.msys2.org/) and its UCRT64 toolchain:

```
pacman -S mingw-w64-ucrt-x86_64-gcc make git
./build.sh          # application, tests and the protocol spike
./build.sh check    # build and run the tests
```

`./build.sh` compiles on all cores; `-j1` gives a readable error output.
The result is `bin/agentaloud.exe`, a single statically linked EXE, with
`nvdaControllerClient.dll` copied beside it. The version comes from
`git describe`, not from any file.

`CLAUDE.md` is the project's design notes and rules, with the full wording
of its invariants in `docs/invarianty.md`; `AGENTS.md` only points to it.
They are in Slovak.

## License

MIT — see `LICENSE.txt`. `nvdaControllerClient.dll` is part of NVDA and is
distributed under the GNU LGPL 2.1 (`vendor/nvda/LICENSE.txt`); it is loaded
at run time and not linked in.
