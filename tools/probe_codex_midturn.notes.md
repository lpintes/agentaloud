# Sonda: vstup od pouzivatela pocas beziaceho tahu (codex app-server)

codex 0.160.0 (natívny). Sonda tools/probe_codex_midturn.py <steer|start|interrupt|idle>.
Logy C:/b/codex-probe/logs/midturn-<rezim>.log, pracovny priecinok C:/b/codex-probe/midturn-repo
(prazdny). Schema: `codex app-server generate-json-schema --out C:/b/codex-probe/schema`.

## Stav
HOTOVO -- pozri koniec suboru

## Schema (v2/TurnSteerParams.json)
turn/steer params: threadId (req), expectedTurnId (req, "fails when it does not match the
currently active turn"), input (req, UserInput[] ako turn/start), clientUserMessageId (opt).
Odpoved: {turnId}.

## steer (midturn-steer.log) -- HOTOVE
Tah T (turn/start 5.56 s); prikaz item/started 10.28 + approval (accept); ping bezi do 24.12.
- 12.36 turn/steer expectedTurnId=bogus -> error -32600 "expected active turn id `bogus-turn-id`
  but found `<T>`"
- 12.41 turn/steer expectedTurnId=T -> {"result":{"turnId":"<T>"}} hned (nevola sa po prikaze).
- 24.12 commandExecution completed; 24.28 item/started+completed userMessage s textom steeru,
  turnId=T (echo prichadza az na hranici dalsej poziadavky na model, NIE pri volani steer);
  clientId null (clientUserMessageId sme neposlali).
- 26.82 agentMessage final_answer "Vrátilo sa 8 paketov. BANANA"; 26.88 jedno turn/completed T.
- Spolu 1 turn/started, 1 turn/completed. Ziadny novy tah.

## start (midturn-start.log) -- HOTOVE
turn/start #2 pocas beziaceho tahu T NEVRATI chybu ani novy tah: odpoved
{"result":{"turn":{"id":"<T>", "status":"inProgress", ...}}} = TO ISTE id ako T. Spravanie
zhodne so steer: userMessage "Also reply..." s turnId=T po dobehnuti prikazu (14.73), final
"Vrátilo sa 8 paketov. BANANA", 1 turn/started, 1 turn/completed. Teda turn/start za behu =
implicitny steer bez kontroly expectedTurnId. (Parametre effort/summary v #2 -- ci sa uplatnia,
neoverene.)

## interrupt (midturn-interrupt.log) -- HOTOVE (2 behy, zhodne)
turn/steer (6.90, ok, turnId=T) a o 0.5 s turn/interrupt (7.50 -> {}): turn/completed T
status=interrupted (7.46, este pred odpovedou na interrupt). Steer ZAHODENY: ziadne echo
userMessage, ziadny novy turn/started za 20 s. Dalsi turn/start "Reply with one word: ok." =
novy tah T2, echo len jeho textu, odpoved "ok" -- steer sa do dalsieho tahu neprenesie.
Pozor: commandExecution item/completed (status=completed) prisiel AZ PO turn/completed (14.00).

## idle (midturn-idle.log) -- HOTOVE
Po turn/completed: turn/steer s id prave skonceneho tahu aj s bogus id -> rovnako
error -32600 "no active turn to steer".

## Stav
HOTOVO (vsetky 4 scenare).

## Neoverene
- ci sa turn/start za behu uplatnia jeho ostatne parametre (model, effort, collaborationMode).
- ci clientUserMessageId v steeri vrati sa ako clientId v echo iteme (neposielali sme ho).
- steer pocas cakania na approval (tu bol approval hned prijaty), a steer pocas streamu textu
  bez nastroja (ci sa vlozi do toho isteho tahu, alebo tah skonci a vstup sa zahodi).
- zahodeny steer v thread/read historii (nekontrolovane).
