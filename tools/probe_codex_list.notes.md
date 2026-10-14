# Sonda: thread/list ako zoznam rozhovorov projektu (claude-gui-lkk.7.11.1)

codex-cli 0.160.0 (natívny). Sonda tools/probe_codex_list.py, logy C:/b/codex-probe/logs/list-*.log.
Schema z `codex app-server generate-json-schema --out C:/b/codex-probe/schema`
(v2/ThreadListParams.json, v2/ThreadListResponse.json). Merane 14. 10. 2026 nad 83 skutocnymi
vlaknami tohto stroja (sondy aj vlastne rozhovory autora). ~/.codex zmenilo len archive+unarchive
jedneho vlakna sondy, ktore je vratene.

## Odpoved
`result` = {data: Thread[], nextCursor, backwardsCursor}. Polia polozky (vsetky pritomne, vela null):
id, sessionId, preview, name, createdAt, updatedAt, recencyAt (unix sekundy), cwd, path (rollout
subor, [UNSTABLE]), source, originator, model, modelProvider, reasoningEffort, cliVersion, gitInfo
{sha, branch, originUrl}, status {type: notLoaded}, parentThreadId, forkedFromId, agentNickname,
agentRole, ephemeral, historyMode, section, projectId, turns (tu vzdy []).

- preview: prva sprava pouzivatela, cely text (server ho neskracuje). Prazdny v 0 z 92 vlakien.
- name: volitelny nazov, null v 82 z 83 vlakien zoznamu -- popisok z neho byt nemoze, z preview ano.
- updatedAt: posledna aktivita; so sortKey updated_at je zoznam od najnovsieho (predvolene desc).
  Predvoleny sortKey je created_at.
- cwd sa vracia tak, ako je ulozene: spatne lomky a velkost pismen podla klienta, ktory vlakno
  zalozil (dve vlakna maju `c:\users\...` malymi).
- source: vlakna z AgentAloud su `vscode` s originator `agentaloud`, z terminalu `cli`.

## Strankovanie
- Bez limitu 25 na stranku, nextCursor ked je dalej. limit 2 nad 54 vlaknami = 27 stranok, tie iste
  id v tom istom poradi ako jedna stranka s limit 100. limit 0 nie je "nic": vratil 1 polozku.
- Posledna stranka ma nextCursor null.

## Co v zozname je a nie je
- Vlakna subagentov: predvolene NIE (bez sourceKinds = interaktivne zdroje). So vsetkymi
  sourceKinds pribudne 7 (source {"subAgent": ...}, parentThreadId vyplneny) a 2 `exec`.
  Predvolba je spravna, filtrovat netreba.
- Archivovane: predvolene NIE. thread/archive -> zmizne z predvoleneho, je v archived=true;
  thread/unarchive ho vrati.
- Prazdne vlakno (thread/start bez tahu): NIE, ani v tom istom procese, ani v novom -- vlakno
  vznikne na disku az s prvym tahom.
- ephemeral: 0.

## Filter cwd
Nie je textovy. `C:\b\x`, `c:\b\x`, `C:\B\X`, `C:/b/x`, s koncovym `\` aj `/` -- vsetky daju tie iste
vlakna, aj ked je ulozene cwd malymi a filter nie. Nadradeny priecinok da 0: presny priecinok,
nie podstrom. Pole ciest funguje (bez duplikatov, ked dva zapisy trafia to iste).

## Cena (spustenie + initialize + jeden thread/list + zavretie stdin)
Tri behy: initialize 0,17 s, list (54 vlakien, limit 50) 0,25-0,30 s, koniec po zavreti stdin
0,03-0,05 s; spolu 0,46-0,50 s. Pred startom session sa to zmesti, ale nie je to okamzite:
mimo vlakna UI.
