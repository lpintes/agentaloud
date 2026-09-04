# Project Instructions for AI Agents

This file provides instructions and context for AI coding agents working on this project.

<!-- BEGIN BEADS INTEGRATION v:1 profile:minimal hash:6cd5cc61 -->
## Beads Issue Tracker

This project uses **bd (beads)** for issue tracking. Run `bd prime` to see full workflow context and commands.

### Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --claim  # Claim work
bd close <id>         # Complete work
```

### Rules

- Use `bd` for ALL task tracking — do NOT use TodoWrite, TaskCreate, or markdown TODO lists
- Run `bd prime` for detailed command reference and session close protocol
- Use `bd remember` for persistent knowledge — do NOT use MEMORY.md files

**Architecture in one line:** issues live in a local Dolt DB; sync uses `refs/dolt/data` on your git remote; `.beads/issues.jsonl` is a passive export. See https://github.com/gastownhall/beads/blob/main/docs/SYNC_CONCEPTS.md for details and anti-patterns.

## Agent Context Profiles

The managed Beads block is task-tracking guidance, not permission to override repository, user, or orchestrator instructions.

- **Conservative (default)**: Use `bd` for task tracking. Do not run git commits, git pushes, or Dolt remote sync unless explicitly asked. At handoff, report changed files, validation, and suggested next commands.
- **Minimal**: Keep tool instruction files as pointers to `bd prime`; use the same conservative git policy unless active instructions say otherwise.
- **Team-maintainer**: Only when the repository explicitly opts in, agents may close beads, run quality gates, commit, and push as part of session close. A current "do not commit" or "do not push" instruction still wins.

## Session Completion

This protocol applies when ending a Beads implementation workflow. It is subordinate to explicit user, repository, and orchestrator instructions.

1. **File issues for remaining work** - Create beads for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **Handle git/sync by active profile**:
   ```bash
   # Conservative/minimal/default: report status and proposed commands; wait for approval.
   git status

   # Team-maintainer opt-in only, unless current instructions forbid it:
   git pull --rebase
   git push
   git status
   ```
5. **Hand off** - Summarize changes, validation, issue status, and any blocked sync/commit/push step

**Critical rules:**
- Explicit user or orchestrator instructions override this Beads block.
- Do not commit or push without clear authority from the active profile or the current user request.
- If a required sync or push is blocked, stop and report the exact command and error.
<!-- END BEADS INTEGRATION -->


## ClaudeLens

Natívne Win32 GUI v C++, ktoré nahrádza terminál ako rozhranie ku Claude Code.
Primárne pre autora, sekundárne pre nevidiacich používateľov NVDA. Terminál je
principiálne zlé rozhranie pre konverzáciu so štruktúrou — je to jeden plochý
buffer bez sémantiky.

**Aplikácia sa volá ClaudeLens, adresár repozitára je `claude-gui`, pracovná
vetva `main`.** Nie je to nekonzistencia, ktorú treba opraviť.

**Nereplikujeme terminál.** Komunikuje sa cez headless režim, ktorý posiela
štruktúrované JSONL. Terminál ten istý dátový model iba vykresľuje; my ho
vykresľujeme inak. Ak sa niekedy objaví nutkanie parsovať ANSI alebo použiť
ConPTY, je to znak, že sa niečo robí zle.

Celý návrh vrátane zavrhnutých alternatív je v beads. Začni `bd show
claude-gui-lkk` (epic) a `bd ready`.

## Build & Test

```bash
./build.sh           # app, spike aj testy
./build.sh check     # testy zostavi a spusti
./build.sh clean
./build.sh V=1 app   # ukecany vystup: aj cele prikazy prekladaca
```

`check` **neprekladá appku**, iba testy — `bin/claudelens.exe` po ňom zostane
taký, aký bol. Zelené testy teda nie sú dôkaz, že beží nový kód: appka spustená
po samotnom `check` je stará a odskúšaš zmenu, ktorá v nej nie je. Poznať to
podľa toho, že `LINK` vypíše len `bin/tests.exe`. Pred manuálnym odskúšaním
patrí `./build.sh` alebo `./build.sh app`. A keď appka beží, linker do nej
nezapíše (`cannot open output file ... Permission denied`) — to je jediné
miesto, kde sa to ohlási nahlas, takže zavri ju skôr, než prekladáš.

`build.sh` je tenká vrstva nad `make`: predradí ucrt64 na PATH a obnoví
`compile_commands.json`. Argumenty prechádzajú do `make` nezmenené, takže
`-j8`, `-B` aj ciele fungujú. Holé `PATH=/c/msys64/ucrt64/bin:$PATH make`
platí ďalej.

Výstup je tichý — jeden riadok na zdroják. `V=1` vráti pôvodné príkazy.

Diagnostiku dáva clangd z `compile_commands.json`, ktorý generuje
`make compdb` z tých istých premenných, ktorými sa prekladá. Do gitu
nejde: sú v ňom absolútne cesty. Po upgrade gcc stačí `touch Makefile`.

**Databáza nesie aj toolchain**, nielen prepínače — `--target` a `-isystem`
cesty vytiahnuté z nášho `g++`. Bez nich si clangd na Windows nájde MSVC
a Windows SDK, **preloží to bez jedinej chyby** a diagnostika potom platí
pre iný prekladač, než ktorým sa prekladá: iný `<windows.h>`, iné STL,
`_MSC_VER` namiesto `__GNUC__`. Vlastného `clangd` netreba prehovárať
prepínačom `--query-driver` — LSP plugin Claude Code ho aj tak spúšťa
holý.

Testy majú tri úrovne s odlišným účelom — sú vysvetlené v hlavičke
`tests/test_main.cpp`. Tretia, soak nad súkromným korpusom, sa zapína
premennou a beží ručne:

```bash
ls ~/.claude/projects/*/*.jsonl | xargs -d'\n' cygpath -m > /tmp/corpus.txt
CLAUDELENS_CORPUS=/tmp/corpus.txt ./bin/tests.exe
```

`cygpath -m` nie je kozmetika. Bash dáva cesty ako `/c/users/...`, natívny
`.exe` im nerozumie a otvorenie **zlyhá ticho** — soak potom nahlási nula
súborov namiesto chyby.

Fixtúry sa negenerujú v testoch. `python tools/make_fixtures.py` sa púšťa
ručne, keď sa zmení formát CLI; diff fixtúry je práve tá informácia, ktorú
chceš vidieť.

Overenie protokolovej vrstvy naostro (potrebuje jednorazový git repozitár,
míňa kredit, `allow` naozaj vykoná commit):

```bash
./bin/spike_console.exe <prazdny-git-repo> allow   # commit prejde
./bin/spike_console.exe <prazdny-git-repo> deny    # commit neprejde
./bin/spike_console.exe <prazdny-git-repo> interrupt  # tah sa prerusi zvonku
```

Režim `interrupt` vypisuje každý záznam celý: pri ňom je tvar záznamov práve
ten výsledok, po ktorom siaha.

ucrt64, nie mingw64 — UCRT je systémové CRT novších Windowsov a odpadá
`msvcrt` a jeho zaobchádzanie s UTF-8. Prekladač sa volá absolútnou cestou;
globálnemu PATH sa never (viď poznámku o 32/64-bit v globálnom `CLAUDE.md`).

## Architecture Overview

Štyri vrstvy. Každá vidí len tú pod sebou a **iba `ui/` pozná `HWND`.**

| Vrstva | Obsah | Nesmie vedieť |
|---|---|---|
| `src/win/` | `window`, `dialog` (prevzaté z `c:/b/eureka-a4`), `process` | čo je na druhom konci rúr |
| `src/proto/` | `jsonl`, `events`, `control`, `session` | ako sa transkript zobrazuje |
| `src/model/` | `transcript`, `bookmarks`, `history` | že existuje RichEdit |
| `src/ui/` | pohľady, dialógy, stavový riadok, reč | — |

`session` je v `proto/`, nie v `model/`, lebo proces, rúry, JSONL aj control
kanál prestanú platiť naraz — keď sa zmení CLI.

`model/transcript` drží bloky a mapu blok → rozsah znakov. `ui/transcript_view`
je jediný, kto tú mapu prekladá na pozície kurzora.

Buffer transkriptu je **UTF-16**, nie UTF-8, hoci protokol je UTF-8. RichEdit
počíta v UTF-16 a prepočítavanie offsetov pri každej navigácii a každom
zbalení je trieda chýb, ktorá sa prejaví presne tým, čomu sa tu vyhýbame —
kurzorom na zlom mieste. Konverzia sa robí raz, pri vzniku bloku
(`model/utf.h`, vlastná implementácia, aby `model/` nemuselo ťahať
`windows.h`).

**Formát záznamu session na disku nie je formát streamu.** Zdieľané sú
`assistant` a `user` (teda práve tie, z ktorých sa robia bloky), ale disk má
navyše `attachment`, `queue-operation`, `mode`, `permission-mode` a ďalších
vyše desať typov, a nemá `system/init`, `result` ani `rate_limit_event`.
Aktuálny zoznam je v `kKnownDiskOnlyTypes` v `tests/test_main.cpp` a vznikol
soakom, nie čítaním dokumentácie.

## Invarianty

Pravidlá, ktoré platia naprieč projektom. Každé z nich zlyháva **ticho**.

1. **Stdin JE control kanál.** Zavrieť ho pred koncom ťahu spôsobí
   `"Tool permission request failed: AbortError: Stream closed"` s
   `non_execution_kind: "permission-rule"` — číta sa to ako zamietnutie
   pravidlom, nie ako pokazený kanál, a model to skúša znova. Zavrieť až po
   zázname `type: result`. Vynucuje `proto::Session::Stop()`.

   Platí to aj pre ťah, ktorý prerušil používateľ. Prerušenie sa posiela ako
   `control_request` so `subtype: "interrupt"` — **nie** ako
   `control_cancel_request`; ten iba stiahne našu vlastnú nezodpovedanú
   požiadavku a s ťahom nemá čo robiť. Ťah sa nekončí odoslaním prerušenia,
   ale až vlastným `result` (overené: `subtype: "error_during_execution"`,
   `terminal_reason: "aborted_streaming"`). `Session::Interrupt()` preto
   `turnInFlight_` zámerne nechá tak — kto ho zhasne skôr, dovolí `Stop()`
   zavrieť stdin uprostred ukončovania ťahu, čiže presne to, čo tento bod
   zakazuje.
2. **Callbacky bežia na čítacom vlákne.** `EventCallback` aj
   `PermissionCallback`. Čokoľvek, čo siahne na okno, musí ísť cez
   `PostMessage`.
3. **Kurzor vo výstupnom poli sa nehýbe kvôli textu, ktorý pribudol.** Ani pri
   rozbalení bloku, ani pri pripísaní obsahu na koniec. Hýbu sa tri veci a
   všetky tri treba obnoviť: kurzor, výber a prvý viditeľný riadok. Postup je
   v `ui::ApplyEdit` — `EM_EXGETSEL` + `EM_GETFIRSTVISIBLELINE`, zmena
   s vypnutým `WM_SETREDRAW`, potom nastavenie späť.

   „Nehýbe sa" znamená **zostane pri tom istom texte**, nie „zostane na tom
   istom čísle". Úprava môže pristáť aj nad čitateľom — výsledok nástroja sa
   vkladá za svoje volanie — a vtedy sa každá pozícia musí posunúť o rozdiel
   (`ui::MoveOffset`). Prvý viditeľný riadok sa preto pamätá ako znakový offset
   (`EM_LINEINDEX` pred, `EM_EXLINEFROMCHAR` po), nie ako číslo riadku: riadkov
   nad ním práve pribudlo.

   Pravidlo je o texte, ktorý **prichádza**, nie o akciách používateľa.
   Odoslanie promptu kurzor zámerne presunie na koniec prepisu — inak by si
   sa k odpovedi musel prečítať cez vlastný prompt. Zistené až používaním;
   pôvodná formulácia invariantu to nerozlišovala.
4. **Každý zlom riadku je práve jeden znak, a je to `\n`.** RichEdit počíta
   odstavcový zlom ako jeden; text s `\r\n` by bol dva znaky v modeli a jeden
   vo widgete. Výstup nástrojov `\r\n` obsahuje — sú to windowsové programy.
   Normalizuje `model::NormalizeNewlines`, ktorým prechádza **každý** text
   vstupujúci do bloku, vrátane promptu z editačného poľa.
5. **Index bloku sa hýbe, `Block::id` nie.** Výsledok nástroja sa vkladá za
   svoje volanie, nie na koniec — Claude volá nástroje paralelne a v poradí
   príchodu sa nedá zistiť, ktorý výstup patrí ku ktorému príkazu. Vloženie
   doprostred posunie indexy všetkých blokov za ním. Čokoľvek, čo pomenúva blok
   naprieč časom — záložka, začiatok ťahu — preto drží `id`, nie index. Indexy
   sú platné len v rámci jednej obsluhy.
6. **NVDA neohlási posun kurzora, ktorý nespravila sama.** Overené skúšaním.
   Každá akcia, ktorej jedinou odozvou mal byť presun kurzora — skok na blok,
   zbalenie, návrat na záložku — musí prehovoriť sama, cez
   `ui::SessionPane::Announce`. Bez toho odpovedá klávesa tichom, čo sa nedá
   odlíšiť od klávesy, ktorá nedošla. Hovorí sa riadok, na ktorom kurzor
   skutočne stojí (`model::Transcript::FirstLine`), nie zhrnutie bloku — inak
   by sa ohlásilo niečo iné, než čo si čitateľ prečíta ďalej.

   Akcia, ktorá sa nedokončí hneď, sa musí ohlásiť **dvakrát**: raz, že sa
   začala, a raz, že skončila. Esc povie „prerušujem" (odozva na klávesu,
   prerušuje) a koniec ťahu povie „prerušené" (odozva na ťah, ide do fronty).
   Kým tam druhá hláška nebola, prerušenie, ktoré prešlo, znelo rovnako ako
   prerušenie, ktoré neprešlo — ticho. Stavový riadok to nezachráni, ten NVDA
   sám nečíta.

   To isté platí pre odoslanie promptu, a platí aj vtedy, keď sa **nič
   nestalo**: Ctrl+Enter je chord a chord sa dá minúť — samotný Enter urobí
   nový riadok a nič viac. Preto hovoria všetky tri konce `Send()`: odoslanie
   („pracujem"), bežiaci ťah aj prázdny prompt.

   A ťah, ktorý beží, nesmie byť ticho celý — a ohlasuje sa **v poradí, v akom
   sa deje**: „premýšľam" pri prvom `system/thinking_tokens` daného úseku, text
   asistenta celý, zhrnutie `ToolUse` a zhrnutie `ToolResult`
   (`ui::SessionPane::AnnounceProgress`) — teda to, čo terminál ukazuje ako
   riadok so spinnerom, plus to, čo ukazuje medzi nimi. Nie po tokenoch:
   `--include-partial-messages` je zvážený a zamietnutý (claude-gui-lkk.5.17),
   lebo práve tá reč bola na termináli chaotická. Celé bloky nie sú chaos, je
   to jedna veta na nástroj. Zhrnutie výsledku je pritom buď celý jednoriadkový
   výstup, alebo len jeho veľkosť („výstup (12 riadkov)"), takže nástroj s
   tisíckou riadkov stojí jednu vetu.

   Text asistenta sa **nesmie odložiť na koniec ťahu.** Pôvodne sa čítal až po
   `result`, celý naraz, a bežné striedanie „veta, nástroj, veta, nástroj"
   znelo ako dve holé „Bash: echo …" a potom obe vety odtrhnuté od toho, čo
   uvádzali. Poradie je informácia a práve v tom bol terminál lepší. Zistené
   používaním a odmerané cez NVDA MCP: predtým „pracujem, Bash: …, Bash: …,
   Nastroj 3. Nastroj 4."; potom „pracujem, claude: Nastroj 5., Bash: …,
   výstup: piata somarina, claude: Nastroj 6., …, hotovo".

   A hovorí sa **s menom hovoriaceho** — „claude: Nástroj 1.", nie holé
   „Nástroj 1." Rečou je všetko jeden hlas: bez mena sa veta asistenta nedá
   odlíšiť od zhrnutia nástroja, a práve to bolo na pôvodnej sťažnosti to
   druhé. Prefix nie je v `Block::body`, aby kópia textu zostala čistá; dáva ho
   `model::SpeakerPrefix`, ten istý, ktorým ho píše prepis, nech sa reč
   a prepis nikdy nerozídu v tom, ako sa hovoriaci volá.

   Koniec ťahu preto musí povedať, že je koniec — „hotovo"
   (`ui::SessionPane::SignalTurnEnd`). Kým odpoveď chodila až na konci, koniec
   sa poznal po nej; keď chodí priebežne, ťah končiaci vetou znie ako ťah,
   ktorý sa chystá povedať ďalšiu. Slovo, nie pípnutie: `MessageBeep` zaznie
   hneď, kým reč, za ktorú patrí, ešte stojí vo fronte NVDA, a pípnutie sa do
   tej fronty zaradiť nedá — NVDA hovorí text.

   Pípnutie nie je náhrada reči. `MessageBeep` už jeden význam má — „ťah
   skončil a nič nezaznelo" (`SignalTurnEnd`) — takže keď naň spadne aj
   `Announce`, odpovie každá klávesa tým istým zvukom ako koniec ťahu a znie
   to, akoby ju appka nepoznala. Presne to urobila kópia `.exe` bez
   `nvdaControllerClient.dll` vedľa seba: klávesy vrátane Ctrl+Enter fungovali
   a pípali. Chýbajúcu knižnicu preto appka ohlási pri štarte dialógom
   (`ui::MainWindow::WarnIfMute`) — bez knižnice nemá vlastný hlas a dialóg je
   jediné, čo NVDA prečíta sama. `Speech::loaded()` je preto iná otázka než
   `available()`: nebežiaca NVDA je normálny stav, chýbajúca DLL je pokazená
   inštalácia.
7. **Reč, ktorá prišla sama, sa neprerušuje.** `interrupt=true` v `Speech::Say`
   patrí výlučne odozve na klávesu (`ui::SessionPane::Announce`); čokoľvek, čo
   prišlo zo streamu, ide do fronty a čaká. Dôvod nie je zdvorilosť:
   `interrupt` nie je parameter NVDA API, `Say` ho robí ako `cancelSpeech()` +
   `speakText()`, a `cancelSpeech` vyprázdni **celú** frontu NVDA — aj naše
   staršie správy, aj reč, ktorú NVDA generuje sama (čítanie riadku, ohlásenie
   fokusu). Prerušiť „len tú svoju poslednú vetu" sa teda nedá ani teoreticky.
   Ruší výlučne používateľ, klávesou, tak ako je zvyknutý z terminálu.
8. **Do bloku nevstúpi terminálová escape sekvencia.** Výstup nástrojov je
   výstup terminálových programov: v korpuse sú farby (`ESC[36;1m`) aj
   kurzorové riadenie z progress barov (`ESC[2K`, `ESC[1A`, `ESC[G`). NVDA ich
   prečíta znak po znaku. Zahadzuje ich `StripEscapes` v `model/transcript.cpp`
   na tom istom mieste ako `NormalizeNewlines` — vo `Widen()`, ktorým prechádza
   všetok text zo streamu. Zahadzujú sa, nie prekladajú na farby: kurzorové
   sekvencie znamenajú „vráť sa a prepíš riadok", čo je prekresľovanie
   terminálu, a to táto aplikácia nerobí.

Zhodu modelu s widgetom nedá overiť žiadny unit test, tak ju appka kontroluje
za behu: po každej úprave porovná dĺžku bufferu s `EM_GETTEXTLENGTHEX`. Keď sa
rozídu, titulok okna sa zmení na **„ClaudeLens — NESÚLAD MAPY ROZSAHOV"**. Ak
to niekedy uvidíš, neladí invariant 3 alebo 4 a navigácia bude zameriavať zle.

Bez `--permission-prompt-tool stdio` sa z pravidla „ask" stane „deny" a nikto
sa nás na nič nespýta. Prepínač je z `--help` vypadnutý, ale CLI ho prijíma.

## Conventions & Patterns

- Komentáre v `src/` po anglicky (tak sú písané prevzaté `win/` súbory),
  v `Makefile` po slovensky bez diakritiky. Komentár hovorí **prečo**, nie čo.
- Beads polia a commit správy bez diakritiky.
- **Spracovať `WM_KEYDOWN` v subclasse nezabráni tomu, aby prišiel `WM_CHAR`.**
  `TranslateMessage` beží v slučke správ, teda skôr než sa správa dostane
  k procedúre okna. Ctrl+Enter preto do editačného poľa vloží `0x0A` a Tab
  vloží tabulátor, nech `PromptProc` vráti čokoľvek. Každý nový chord treba
  zahodiť **dvakrát** — raz ako klávesu, raz ako znak. Zlyháva ticho: pri
  odoslaní sa pole vyčistí, takže vložený znak vidno až vtedy, keď sa prompt
  neodošle.
- **Zdrojáky nepíš cez shell heredoc.** Toto prostredie v ňom žerie spätné
  lomky, takže `L'\\'` sa ticho zmení na `L'\'`. Používaj Write/Edit.
- Žiadny Python ani Node v produkte. Python je na prieskum správania CLI
  (`tools/`), nie závislosť.
- Ústupky vidiacim používateľom sa nerobia. Vloženie dialógov do prepisu,
  streamovanie po tokenoch a farebné diffy boli zvážené a zamietnuté.

## Uzavretie kroku

Krok je hotový až keď sa dá bezpečne skončiť — teda keď by nová session
s jediným slovom „pokračuj" nestratila nič podstatné. Pred ohlásením hotového
kroku:

1. **Zostav a spusti** — nestačí, že sa to preložilo.
2. **`bd update <id> --notes`** — čo sa zistilo, aké sú overené tvary dát,
   a **každá odchýlka od pôvodného návrhu aj s dôvodom.** Odchýlku zapíš aj do
   `--design` epicu, nech tam nezostane nepravda.
3. **Nový invariant → do sekcie Invarianty vyššie**, nielen do komentára. Ak
   pravidlo platí pre viac než jeden súbor, patrí sem.
4. **`bd create`** na všetko, čo z kroku vypadlo alebo pribudlo.
5. **Commit** so správou, ktorá hovorí prečo, nie čo. Diff hovorí čo.
6. **`bd close`** až nakoniec.

Bod 2 a 3 sú tie, ktoré sa vynechávajú, a sú to práve tie, ktoré rozhodujú
o tom, či sa dá pokračovať zajtra.

`CLAUDE.md` a `AGENTS.md` sú nezávislé súbory s odlišnou hlavičkou od beads,
ale od nadpisu `## ClaudeLens` nižšie musia byť **zhodné** — Codex číta ten
druhý. Pri zmene tejto časti zrkadli do oboch a over `diff`om.
