# Probe: are Codex item ids stable between live stream and thread/resume history?

Probe: tools/probe_anchor_codex.py <live|resume|all>. Logs in C:/b/codex-probe/logs/anchor-*.log,
live items in C:/b/codex-probe/logs/anchor.live.json, thread id in anchor.thread.
Workdir C:/b/codex-probe/anchor-repo (empty temp dir, outside the repo).
codex-cli 0.167.0 (native, %LOCALAPPDATA%\Programs\OpenAI\Codex\bin first on PATH).
Model gpt-6-luna, effort low (same as probe_codex_midturn.py).

## Plan
1. live: thread/start, turn/start (clientUserMessageId set) asking to run ping, steer mid-command
   (clientUserMessageId set), record every item/started + item/completed id/type/turnId.
2. resume: new app-server, thread/resume (default view) + initialTurnsPage itemsView full +
   thread/turns/list itemsView full; compare ids, types, order with live.

## live (anchor-live.log) -- DONE
thread 01a1cf2a-1bc5-..., one turn. Items (start order): userMessage (uuidv7 id, clientId
probe-prompt-1), commandExecution `exec-<uuid>`, userMessage of the steer (uuidv7, clientId
probe-steer-1, turnId = same turn, echoed at 11.35 WHILE the command was still running --
differs from 0.160 where echo came after the command), agentMessage `msg_<hex>` phase
final_answer. No reasoning item at effort low, no commentary message.

## resume (anchor-resume.log) -- DONE
New process, thread/resume with initialTurnsPage itemsView full: thread.id same. thread.turns,
initialTurnsPage and thread/turns/list(full) all identical: same 4 ids, same types, nothing
missing/extra, clientId preserved on both userMessages. ORDER = completion order (prompt,
steer, exec, msg), not start order (exec started before steer echo).

## default (anchor-default.log, anchor-resume2.log) -- DONE
thread/resume WITHOUT initialTurnsPage on 0.167.0 still returns thread.turns with itemsView
"full" and the same 4 items (schema says default "summary"; not what happens here).
Second turn with effort medium + summary detailed: still NO reasoning item live (only
userMessage, agentMessage), so reasoning id stability is unverified. Third process resume:
both turns, all ids identical, nothing missing.

## Status
DONE. Unverified: reasoning items (none emitted by gpt-6-luna), interrupted/dropped steer in
history, items of subagent threads.
