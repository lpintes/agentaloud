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


## AgentAloud

Natívne Win32 GUI v C++, ktoré nahrádza terminál ako rozhranie ku coding
agentom (dnes Claude Code a Codex). Primárne pre autora, sekundárne pre
nevidiacich používateľov NVDA. Terminál je principiálne zlé rozhranie pre
konverzáciu so štruktúrou — je to jeden plochý buffer bez sémantiky.

**Aplikácia sa volá AgentAloud (`agentaloud.exe`), repozitár na GitHube
`lpintes/agentaloud`, lokálny adresár `claude-gui`, prefix beads
`claude-gui-…`, pracovná vetva `main`.** Rozdiel adresára a prefixu od mena nie
je nekonzistencia, ktorú treba opraviť — sú to interné identifikátory.

Do októbra 2026 sa appka volala **ClaudeLens**; premenovaná pred zverejnením
(claude-gui-lkk.51), lebo backendy sú dva a „Claude" je ochranná známka.
Meno je v kóde **jedno**, `src/app_name.h` (`APP_NAME`, `APP_EXE`), a C++ aj
`.rc` ho skladajú z makra. **Fixtúry a sondy v `tools/` ho nesú po starom**
(`claudelens-fixture-0000`, `claudelens_probe`) — je to záznam toho, čo sa
vtedy poslalo, a zástupná cesta scrubbera musí sedieť s už zapísanými
fixtúrami. Neopravovať.

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

`check` **neprekladá appku**, iba testy — `bin/agentaloud.exe` po ňom zostane
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
AGENTALOUD_CORPUS=/tmp/corpus.txt ./bin/tests.exe
```

`cygpath -m` nie je kozmetika. Bash dáva cesty ako `/c/users/...`, natívny
`.exe` im nerozumie a otvorenie **zlyhá ticho** — soak potom nahlási nula
súborov namiesto chyby.

**Korpus je posuvné okno, nie archív.** CLI zametá prepisy staršie než
`cleanupPeriodDays`, čo je **30 dní** a v `~/.claude/settings.json` to nemusí
byť napísané — je to východisková hodnota (overené v binárke: „Number of days
to retain chat transcripts before automatic cleanup (default: 30)"). Deväť
mesiacov používania teda znamená dvadsaťosem dní na disku. Poznať to podľa
toho, že korpus má **každý** deň bez medzery až po ostrý spodok; to nie je
vzorec používania. Súbory, nad ktorými soak niečo raz našiel, o mesiac
neexistujú, takže nález, ktorý sa nezapíše sem alebo do fixtúry, sa nedá
zopakovať. Kto chce dlhší korpus, nastaví si `cleanupPeriodDays` vyššie —
a fixtúra `thinking.jsonl` je práve ten prípad: záznamy, z ktorých vznikla, sa
zmažú koncom septembra a ona bude jediná kópia.

Fixtúry sa negenerujú v testoch. `python tools/make_fixtures.py` sa púšťa
ručne, keď sa zmení formát CLI; diff fixtúry je práve tá informácia, ktorú
chceš vidieť. Fixtúry sú tri a `disk.jsonl` nie je stream, ale **súbor session
z `~/.claude/projects`** — ten formát, z ktorého sa obnovuje história po
`--resume`. Bez nej by tú cestu testoval iba soak, teda nikto, kto ho nepúšťa.

**Fixtúra je artefakt a nie každá sa dá zopakovať.** `basic.jsonl`,
`denied.jsonl` a `disk.jsonl` sa pregenerovať smú; `thinking.jsonl` **nie** —
a nie preto, že by sa nechcelo, ale preto, že sa to už nedá. CLI prestalo
posielať text premýšľania: bloky `thinking` chodia s prázdnym textom a samotným
podpisom, takže `Transcript` z nich blok nespraví — a je to správne,
„premýšľanie (0 riadkov)" je šum. Odmerané dvakrát. Nad korpusom: z vyše 6000
častí `thinking` v 197 súboroch má text **32**, a všetkých 32 je z CLI 2.1.258
a modelu haiku (2. 9. 2026) — pričom opus mal na **tej istej** verzii nulu zo
478, takže nerozhoduje len verzia, ale dvojica verzia + model. A priamo,
lebo korpus o sonnete nehovorí nič (na 2.1.258 nikdy nebežal):
`tools/probe_thinking.py` na CLI 2.1.263 dá pri haiku, sonnete aj opuse blok
bez textu. `thinking.jsonl` je teda jediný skutočný záznam premýšľania
s obsahom, ktorý existuje, a preto stojí bokom od `basic.jsonl`
(claude-gui-lkk.35). Každý súbor má zároveň vlastný `Scrubber`: spoločné
číslovanie identifikátorov by fixtúry zviazalo tak, že zahodenie jednej by
zneplatnilo druhú.

**Do fixtúry nesmie vojsť to, čo CLI poskladalo z tohto stroja.** Diskový
formát má typ `attachment` a v ňom sedí globálny `CLAUDE.md` používateľa, jeho
e-mail a celý `prompt_snapshot` — 186 z 209 kB prvej verzie fixtúry, ktorá
mala ísť do verejného repozitára. Zahadzuje ich `write_fixture` tým istým
pravidlom ako hooky: je to vlastnosť stroja, nie formátu, a `ReadSessionRecords`
ich aj tak neprepúšťa. Zlyhalo by to ticho — fixtúra vyzerá ako fixtúra a
nikto ju nečíta celú.

To isté pravidlo má **tri ďalšie vrstvy a všetky sedia v `system/init`**, ktorý
`attachment` nie je a zahodiť sa nedá:

  • **Meno účtu v cestách.** `Scrubber.text_for` ho prepisuje v každom reťazci,
    nie v poliach, ktoré vyzerajú ako cesta — cesta je aj v argumente nástroja,
    aj vo výsledku, aj v `memory_paths`, aj v ceste pluginu, a vymenovať tie
    polia znamená minúť to, ktoré pribudne. Tvary sú tri, lebo CLI ich píše
    tromi spôsobmi: `C:\Users\…`, `C:/Users/…` a s pomlčkami (kľúč adresára
    v `~/.claude/projects`). Cesta sa **neškrtá, len prepisuje** — z toho
    istého dôvodu ako čas nižšie.
  • **Inventár stroja.** `plugins`, `mcp_servers`, `skills`, `slash_commands`,
    `agents` a `terminal_slash_commands` sú zoznam toho, čo má autor
    nainštalované. Kľúč zostáva a hodnota sa vyprázdni, takže fixtúra ďalej
    hovorí, že to pole existuje a že je to pole — čo je jediné, čo o ňom
    appka vie. Overené grepom: zo `system/init` číta appka `model`, `cwd`,
    `permissionMode` a `session_id`, nič viac (dnes `proto::Translator`
    a `Session`).
  • **`tools` je dvoch druhov naraz.** `Bash`, `Read` a `Edit` sú tvar
    protokolu a v tej istej fixtúre sa aj používajú, kdežto mená s prefixom
    `mcp__` sú MCP servery tohto stroja — v `basic.jsonl` ich bolo 104
    a všetkých 104 iba tu, ani jedno v bloku `tool_use`. Preto sa vyhadzujú
    ony a zoznam zostáva.

Pregenerovať sa kvôli tomu nemuselo nič: `python tools/make_fixtures.py
--rescrub <súbor>` prežene existujúcu fixtúru scrubberom znova, bez CLI
a bez kreditu. Je to jediná cesta k `thinking.jsonl`, ktorá sa smie použiť —
prepísať reťazce v nej tvar záznamu nemení. Opakovanie je bezpečné: id aj časy
dostávajú náhrady v poradí prvého výskytu a to poradie je v zapísanej fixtúre
rovnaké ako pri jej vzniku.

Čas sa naopak **neškrtá, len sa nahrádza stabilným a platným** ISO 8601.
Diskový formát je jediný, kde na čase záleží (invariant 15), a `<scrubbed>`
namiesto času by tú vlastnosť otestovať nedal.

Overenie protokolovej vrstvy naostro (potrebuje jednorazový git repozitár,
míňa kredit, `allow` naozaj vykoná commit):

```bash
./bin/spike_console.exe <prazdny-git-repo> allow   # commit prejde
./bin/spike_console.exe <prazdny-git-repo> deny    # commit neprejde
./bin/spike_console.exe <prazdny-git-repo> interrupt  # tah sa prerusi zvonku
```

Režim `interrupt` vypisuje každý záznam celý: pri ňom je tvar záznamov práve
ten výsledok, po ktorom siaha.

**Do bežiaceho okna sa dá pozrieť bez očí** — `tools/lens.ps1`. Dot-source ju
a `Get-LensWindows <pid>`, `Get-LensChildren`, `Get-LensFocus`, `Send-LensKey`
odpovedia, čo je v ktorom poli a čo má fokus. Text sa ťahá `WM_GETTEXT`om,
ktorý systém marshaluje aj cez hranicu procesu; `GetWindowText` na to
nepoužívaj — cez hranicu vráti prázdny reťazec, čiže **zlyhá ticho** a vyzerá
to ako prázdne pole.

**Skutočné klávesy sa tak posielať nedajú.** `SendKeys` aj `keybd_event` idú do
okna v popredí, takže by pristáli u používateľa. `PostMessage WM_KEYDOWN`
priamo prvku obchádza slučku správ, ale pre klávesu, ktorú chytá subclass
procedúra (F1, F2, F4, chordy), je to plnohodnotné overenie — obsluhuje ju tá
istá procedúra, do ktorej by prišla aj skutočná správa. Pre klávesu, o ktorej
rozhoduje až dialógová slučka, nedokazuje nič. A `SendMessage` do modálneho
dialógu zablokuje volajúci shell, kým sa dialóg nezavrie; na otvorenie dialógu
teda `PostMessage`. Ten zase dorazí aj do okna, ktoré modál zakázal, takže
klávesa poslaná pod otvoreným dialógom spraví niečo, čo skutočná klávesa
nedokáže (napríklad otvorí druhý dialóg). Tlačidlo OK v `MessageBox` nemá id 1.

**Chord s Ctrl alebo Shift — a teda aj odoslanie promptu — sa poslať dá**, a
tiež bez popredia: `Set-LensText` dá text do promptu a `Send-LensChord $prompt
0x0D -Ctrl` ho odošle. Procedúra sa na modifikátor pýta cez `GetKeyState`,
ktorý `PostMessage` nastaviť nevie; `Send-LensChord` sa preto z pomocného
vlákna pripojí `AttachThreadInput` na vlákno appky a modifikátor nastaví
`SetKeyboardState` — stav kláves je po pripojení spoločný. Overené 6. 10. 2026
(claude-gui-lkk.52): celý ťah naostro, aj s dialógom povolenia a otázky, bez
jediného stlačenia u používateľa. Na overenie naostro stačí `--model haiku`.
Dialóg povolenia pritom nevyvolá hocijaký príkaz: `echo hello` CLI povolí
samo ako read-only, `echo hello > hello.txt` už nie.

Testovaciu inštanciu zatváraj **podľa PID**, nikdy `taskkill /IM` — používateľ
má vlastnú AgentAloud (či staršiu ClaudeLens) spustenú.

### Vydanie

```bash
./release            # push, spusti vydanie.yml, pocka a vypise adresu
```

`.github/workflows/zostavenie.yml`
beží pri každom pushi do main a pri PR: Windows, msys2 **UCRT64**, ten istý
`./build.sh all` a `./build.sh check` ako lokálne, a varovanie prekladača je
chyba. `vydanie.yml` dopočíta číslo `vRRRR.M.N`, zavolá `zostavenie.yml` so
značkou, overí, že EXE nesie tú istú verziu, a až potom značku a vydanie
zverejní. Balík: `agentaloud.exe`, `nvdaControllerClient.dll`, `LICENSE.txt`,
licencia DLL ako `nvdaControllerClient-LICENSE.txt` a `README.md` ako
`README.txt` (dvojklik na `.md` sa na Windows pýta, čím ho otvoriť).

**Verzia nie je napísaná nikde** — dáva ju `git describe` (Makefile →
`build/app_version.h` → VERSIONINFO, `--version`, nápoveda). Mimo
značky je to `0.0.0-<hash>` alebo `<značka>-N-g<hash>`, s `-dirty` pri
necommitnutých zmenách. Hlavička sa prepíše len pri zmene obsahu, takže
preklad bez nového commitu neprekladá nič.

**Konce riadkov sú LF, a drží ich `.gitattributes`** (`* text=auto eol=lf`;
`vendor/` a `tests/fixtures/` sú `-text`, ich bajty sa nemenia). Kým tam
nebol, mal index pár súborov CRLF a zvyšok LF, a `sed -i` z Git Bash — ktorý
CR zahadzuje, kým nedostane `-b` — taký súbor ticho prepísal celý; commit
potom zmenil každý riadok (claude-gui-lkk.64). Renormalizácia bola kedysi
zamietnutá práve preto, že tie súbory prepíše celé — no to je cena raz,
zmiešaný stav sa platil pri každej úprave. Commit renormalizácie je
v `.git-blame-ignore-revs`; `git blame` ho preskočí s
`git config blame.ignoreRevsFile .git-blame-ignore-revs`.

Runner má `core.autocrlf=true` a workflow ho pred checkoutom stále vypína:
`build.sh` s CRLF bash zhodí a fixtúry by sa zmenili pod testami. Odkedy je
`.gitattributes`, je to len poistka.

Repozitár je od 5. 10. 2026 verejný, a na tom stojí updater: súbor
z vydania súkromného repozitára sa bez prihlásenia stiahnuť nedá
(invariant 23). Databáza beadov (Dolt) sa na GitHub neposiela — remote je
odstránený a jedinou kópiou v gite je pasívny export `.beads/issues.jsonl`.


ucrt64, nie mingw64 — UCRT je systémové CRT novších Windowsov a odpadá
`msvcrt` a jeho zaobchádzanie s UTF-8. Prekladač sa volá absolútnou cestou;
globálnemu PATH sa never (viď poznámku o 32/64-bit v globálnom `CLAUDE.md`).

**Codex (`--backend codex`) musí byť z natívneho inštalátora**
(`powershell -c "irm https://chatgpt.com/codex/install.ps1 | iex"`), nie z npm.
Appka spúšťa `codex app-server` rovnako ako `claude` — `CreateProcessW` si
`codex.exe` nájde po PATH — a npm tam dáva len shimy (`.cmd`, `.ps1`, shell
skript), ktoré `CreateProcessW` nespustí. Inštalátor dá
`%LOCALAPPDATA%\Programs\OpenAI\Codex\bin` na začiatok používateľského PATH,
ale **starú npm inštaláciu v `scoop\apps\nodejs` nespozná** ako konflikt.
Overenie: `where.exe codex` musí na prvom mieste ukázať ten priečinok.

## Architecture Overview

Štyri vrstvy a port medzi nimi. Každá vidí len tú pod sebou a **iba `ui/`
pozná `HWND`.**

| Vrstva | Obsah | Nesmie vedieť |
|---|---|---|
| `src/win/` | `window`, `dialog`, `process`, `paths` | čo je na druhom konci rúr |
| `src/agent/` | port: `events` (udalosti), `backend` (rozhranie, `Capabilities`) | ktoré CLI beží; JSON; `windows.h` |
| `src/proto/` | `jsonl` (spoločné) a adaptér na CLI v podadresári: `claude/` (`session`, `translate`, `claude_backend` …) | ako sa transkript zobrazuje |
| `src/model/` | `transcript`, `bookmarks`, `history` | že existuje RichEdit; **ktoré CLI beží** |
| `src/ui/` | pohľady, dialógy, stavový riadok, reč | **ktoré CLI beží** |

**Appka nehovorí s Claudom, hovorí s portom** (claude-gui-lkk.44). `ui/` drží
`agent::Backend` a pýta sa ho, čo vie (`agent::Capabilities`), nikdy nie, aké
CLI to je. `model/` dostáva `agent::Event`, nie záznamy. Čo Claude volá ako,
vie len jeho adaptér: `proto::Translator` prekladá záznamy na udalosti,
`proto::ClaudeBackend` obaľuje `Session` a pozná režimy, `AskUserQuestion`
aj históriu na disku. Ktorý adaptér sa vyrobí, rozhoduje **iba `main.cpp`** —
fabrika v `agent/` by znamenala, že port závisí od svojich adaptérov. Ďalšie
CLI (Codex, claude-gui-lkk.44.5) je nový adaptér, nie prechod celým UI.

Vzor je ports and adapters a slovník portu je podľa ACP (Agent Client
Protocol), ale nie je to ACP a nikomu sa nehovorí — prečo, je v
`claude-gui-lkk.44`. Text ide portom ako UTF-8 a čistí sa až v `model/`
(invarianty 4 a 8), raz pre všetky CLI.

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
Tu je pravidlo a kde ho kód drží; dôvod, merania, odmietnuté alternatívy
a história sú v **`docs/invarianty.md`** pod tým istým číslom. Číslovanie sa
nemení — odkazujú naň beady. **Pred zmenou, ktorá sa bodu týka, si prečítaj
jeho plné znenie.** Nový invariant alebo zmenu píš do oboch súborov.

1. **Stdin JE control kanál.** Zavrieť ho až po `type: result`, inak CLI hlási
   zamietnutie pravidlom. Prerušenie je `control_request` `subtype: interrupt`
   (nie `control_cancel_request`) a ťah končí až jeho `result`;
   `Session::Interrupt()` preto `turnInFlight_` nezhasína. `Stop()` v oboch
   adaptéroch najprv ťah preruší, potom čaká.
2. **Callbacky bežia na čítacom vlákne** (`EventCallback`,
   `PermissionCallback`). Na okno len cez `PostMessage`.
3. **Kurzor v prepise sa nehýbe kvôli textu, ktorý prichádza** — zostane pri
   tom istom texte: kurzor, výber aj prvý viditeľný riadok (ako znakový
   offset), posun cez `ui::MoveOffset`. Postup v `ui::ApplyEdit`. Odoslanie
   promptu kurzor zámerne presunie na koniec.
4. **Každý zlom riadku je jeden znak `\n`** — `model::NormalizeNewlines` na
   každom vstupe do bloku. Do obyčajného `EDIT` poľa ho späť na `\r\n`
   prekladá jedine `win::Dialog::SetTextLines`.
5. **Index bloku sa hýbe, `Block::id` nie.** Výsledok nástroja sa vkladá za
   svoje volanie. Čo pomenúva blok naprieč časom, drží `id`.
6. **NVDA neohlási posun kurzora, ktorý nespravila sama.** Každá klávesa musí
   prehovoriť (`SessionPane::Announce`), aj keď sa nič nestalo; hovorí sa
   riadok, kde kurzor stojí. Dlhá akcia dvakrát (začiatok aj koniec —
   „prerušujem"/„prerušené", „hotovo" cez `SignalTurnEnd`). Ťah sa ohlasuje
   priebežne v poradí, celými blokmi (`AnnounceProgress`), text asistenta
   s menom hovoriaceho (`Transcript::SpeakerPrefix`, meno z
   `Capabilities::agentName`, nastavené pred `Start` a potom nemenné).
   Pípnutie nie je náhrada reči; chýbajúca `nvdaControllerClient.dll` sa
   ohlási dialógom (`WarnIfMute`). **Za zavretým dialógom sa hovoriť nedá**
   a časovač proti čítačke sa nepoužíva — dialóg, ktorého odpoveď je veta,
   zostane otvorený (Ctrl+F, `SearchAndSay`).
7. **Reč, ktorá prišla sama, sa neprerušuje.** `interrupt=true` v
   `Speech::Say` len pre odozvu na klávesu; `cancelSpeech` vyprázdni celú
   frontu NVDA.
8. **Do bloku nevstúpi terminálová escape sekvencia** (`StripEscapes` vo
   `Widen()`). Obal `<tool_use_error>` strháva adaptér
   (`UnwrapToolError`), nie `Widen`; `isError` = pole `is_error` || obal.
   **Riadiaci znak sa vypíše ako `\x00`** (`EscapeControls`, až za
   `NormalizeNewlines`), nezahadzuje sa — `NUL` by ukončil `EM_REPLACESEL`.
9. **Proces je DPI-aware** (`SetProcessDpiAwarenessContext`, prvý riadok
   `wWinMain`), aby NVDA našla stavový riadok. Rozmery preto násobiť
   `MulDiv(x, dpi, 96)`, font cez `SystemParametersInfoForDpi`, prijať
   `WM_DPICHANGED`.
10. **Dieťa nesmie prežiť rodiča** — job object s
    `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` vo `win::Process`:
    `CREATE_SUSPENDED` → `AssignProcessToJobObject` → `ResumeThread`; handle
    jobu nedediteľný; zlyhanie priradenia nie je fatálne. Overovať
    obojstranne (s jobom aj bez).
11. **Priebežná reč má tri stavy** (`SessionPane::WantsProgressSpeech`):
    okno na pozadí alebo neaktívna session → mlčí (koniec ťahu zvukom);
    kurzor si čitateľ presunul → mlčí; fokus v prompte alebo kurzor sleduje
    koniec (`Following()`, `anchor_`, ktorý `Apply` prevezme, kým kurzor stojí
    na konci) → hovorí. Modál na pozadí: `SignalWaiting` (`FlashWindowEx` +
    `PlaySoundW` `Notification.Default` bez `SND_NODEFAULT`). Fokus pod
    modálom cez `GW_ENABLEDPOPUP`. `Announce` sa netlmí nikdy.
12. **`AskUserQuestion` nie je povolenie, hoci ako `can_use_tool` pricestuje.**
    Vetví sa podľa mena nástroja v `ClaudeBackend::OnPermission` →
    `agent::QuestionRequest`; odpoveď je allow + `updatedInput.answers`
    kľúčované textom otázky. Codex: sync `request_user_input` (kľúč id) aj
    async otázka ako správa — odpoveď ďalším promptom
    (`QuestionByPrompt`, `kMsgQuestionByPrompt`). Neznáma serverová
    požiadavka Codexu = `-32601` = zamietnutie (pozri `Unrecognised`).
13. **Dialóg povolenia ukazuje ten istý text ako prepis** —
    `model::RenderToolCall` nad `agent::ToolCall` z
    `proto::ToolCallFromInput`. Zápis do súboru začína „súbor: <cesta>"; meno
    nástroja v titulku. „Povoliť na túto session" len to, čo CLI navrhlo
    (`PermissionRequest::offered`), `destination` prepísané na `session`
    (`proto::SessionPermissions`) — nikdy zápis na disk.
14. **Id session určuje appka pred štartom** (`--session-id` z
    `NewSessionId`), okrem prípadu, keď rozhovor pomenúvajú `extraArgs`. To
    isté id druhýkrát nejde; obnovuje `--resume` (`Resume::ById`, prepis
    pravopisu v adaptéri). Obnovená session o sebe nič nehovorí.
15. **`-c` sa CLI neposiela** — `history.jsonl` pozná len terminál. Najnovšiu
    session vyberá `proto::sessions` podľa posledného `timestamp` v súbore
    (nie mtime) a pošle `--resume <id>`. Projekt bez rozhovoru = nová session
    bez hlášky.
16. **Appka nemá stdout** (`-mwindows`): `win::WriteToParentConsole` —
    štandardný výstup → konzola rodiča → dialóg. Neznáma voľba s pomlčkou sa
    odmietne (kód 2), nikdy sa nestane priečinkom. Voľby pre CLI len za `--`.
    `--backend` predvolene `claude`; `--permission-mode` sa overuje proti
    `Capabilities::modes` (`app::CheckMode`). Parser a `HelpText` v
    `src/arguments.cpp`.
17. **Slash príkaz v headless režime vykoná model, nie CLI** (cez `Skill`),
    a nemusí ho vykonať vôbec. F4 (`ShowCommands`) príkaz vloží do promptu,
    neodošle. Zoznam len z odpovede na `initialize`; „ešte nie je" sa povie.
18. **História z disku ide tými istými volaniami ako živý ťah**:
    `ReadSessionRecords` (len `user`/`assistant`, bez sidechainov) →
    `TranslateHistory` → `model::RestoreHistory` po jednej udalosti. `user`
    záznam nie je vždy prompt (`HumanPromptText`, overuje aj typ). Kurzor
    a `anchor_` po prehratí na konci; vkladá sa jednou úpravou.
19. **Klávesa, ktorú F1 nevymenúva, neexistuje.** Nový kláves nie je hotový,
    kým nie je v `kKeys…` každého `src/i18n/<jazyk>.def`. Čo sa medzi
    agentmi líši, skladá `KeysText` z `Capabilities`. Klávesa sa píše ako
    skratka („Shift+T"), nie ako znak.
20. **Režim povolení je stav procesu; mení ho Shift+Tab aj CLI** (po
    `ExitPlanMode`, zhodené `auto`) a hlási `system/status`. Jediný zdroj
    pravdy je `Session::permissionMode()`, pravidlá v
    `proto::PermissionModeTracker` (semienko z príkazového riadka,
    optimistický posun, odmietnutie späť na posledné slovo CLI, odmietnutý
    mód sa preskakuje). Cyklus `default → acceptEdits → plan → auto`;
    `bypassPermissions` za behu nejde. Zmenu zvonku ohlási `agent::ModeChanged`
    → `FollowPermissionMode`. Shift+Tab sa ohlasuje synchrónne.
21. **Model v stavovom riadku je ten, ktorý odpovedá** — `message.model`
    záznamu `assistant` (`ParseAnsweringModel`, bez subagentov a
    `<synthetic>`), nie `system/init`. Poradie: `--model` → história →
    `system/init` → `assistant`; drží `proto::Translator`.
22. **Nastavenia mení len výslovný úkon; zlý riadok sa odmietne.**
    `%APPDATA%\AgentAloud\settings.txt` alebo prenosné `config\settings.txt`.
    Kľúče s prefixom backendu. Shift+Tab sa neukladá. Zápis mení len svoje
    riadky (`Settings::Set` + `Serialize`, `win::WriteFileBytes`). Prednosť:
    príkazový riadok > súbor > zabudované (`app::ApplySettings`). Kontrolujú
    sa riadky všetkých backendov.
23. **Aktualizuje sa celý balík a nič sa nevymení, kým nie je všetko
    overené**: súčet ZIP-u → rozbalenie do `.update-new\` →
    `update::PlanReplace` → výmena so zálohou v `.update-old\` a návratom
    všetkého pri chybe. `tar.exe` plnou cestou, bez cesty na príkazovom
    riadku, rozhoduje len návratový kód. Manifest (Common Controls 6) je
    povinný; stavový riadok má vypnutú tému. Ručná kontrola
    (`--check-updates`, menu) ignoruje interval a odpovie vždy.
24. **Viac sessions je MDI a session nevie, čo ju hostí** (`SetActive`,
    `SetStatusBar`). Child musí mať `WS_MAXIMIZEBOX`; NVDA ho ohlási len
    vďaka `SessionWindow::Annotate`; klávesy rámu sú v akcelerátoroch.
    Session z menu nepokračuje v rozhovore. Dve v tom istom priečinku dostanú
    číslo (`app::SessionOrdinals`, `Renumber`).
25. **Proces CLI, ktorý skončil sám, je blok `SessionEnded` so stderr ako
    obsahom** (`win::Process` číta stderr vlastným vláknom, hlava + chvost).
    Nie po vlastnom `Stop()` (`stopping_`). Stavový riadok „agent nebeží".
    Proces, ktorý nevznikol, blok nemá — `agent::StartFailure` a hláška pri
    štarte.

**NESÚLAD MAPY ROZSAHOV** v titulku session znamená, že dĺžka modelu
nesedí s `EM_GETTEXTLENGTHEX` — neladí invariant 3, 4 alebo 8. Hľadaj znak,
ktorý widget spočíta inak (raz to bol `NUL` z binárky) v poslednom výstupe
nástroja. Zotaviť sa appka nevie (claude-gui-lkk.31).

Bez `--permission-prompt-tool stdio` sa z „ask" stane „deny". Prepínač nie je
v `--help`, ale CLI ho berie.

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
- **Dialógy sú z resource šablóny.** Šablóny v `src/ui/app.rc`,
  identifikátory v `src/ui/resource.h`, `win::Dialog::ShowModal` berie id
  šablóny. Prečo skutočný dialóg a nie okno, ktoré tak vyzerá, je vo
  `win/dialog.h`. Hodnoty sú v nich **read-only editačné polia, nie statické
  texty**: statický text sa nedá zamerať, takže sa nedá prečítať po znakoch ani
  označiť, a session id je 36 znakov hexa, ktorých jediné použitie je kopírovanie.
  `.rc` je v UTF-8 a windres to musí vedieť (`#pragma code_page(65001)`, plus
  `--codepage` v Makefile) — inak sa diakritika v popiskoch **ticho** zmení na
  dvojice znakov a preloží sa to.
- **Texty dialógu sú z katalógu, nie zo šablóny.** Šablóna je jedna pre všetky
  jazyky a jej slovenské slová sú len náhrada pre rozloženie; titulok a každý
  popisok či tlačidlo dosadí `ui::LocalizeDialog` (`ui/dialog_texts.cpp`) ako
  prvú vec v `OnInit`. Prvok s textom v `.rc`, ktorý nie je v jej tabuľke,
  zostane **ticho** po slovensky. Skratky (`&`) si volí každý jazyk sám
  a v rámci jedného dialógu sa nesmú opakovať.
- **Všetko, čo čitateľ počuje alebo vidí, ide cez `i18n`** (`src/i18n/`):
  `ids.def` je zoznam, `<jazyk>.def` texty, chýbajúci preklad zastaví build.
  Jazyk sa volí raz pri štarte (`language` v nastaveniach, inak jazyk
  Windows) a za behu sa nemení — súhrny blokov sú text prepisu. Výnimka je
  text pre agenta (pokyn modelu), ten je vždy anglicky a píše ho adaptér, nie
  panel. **CLI ho vráti v streame ako text výsledku nástroja** (odmerané
  6. 10. 2026: obyčajný reťazec, `is_error`, bez `system/permission_denied`),
  takže by ho prepis ukázal čitateľovi; `proto::MakeToolResult` preto vlastné
  pokyny (`kDeniedInstruction`, `kQuestionDeclinedInstruction`) spozná a
  nahradí slovom z katalógu. Nový pokyn modelu patrí tam tiež. Čitateľovi sa
  vyká.
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
   pravidlo platí pre viac než jeden súbor, patrí sem: skrátene sem, plné
   znenie s dôvodom a meraním do `docs/invarianty.md`.
4. **`bd create`** na všetko, čo z kroku vypadlo alebo pribudlo.
5. **`bd close`**, a až potom commit. `.beads/issues.jsonl` je pasívny export,
   ktorý sa prepíše pri zavretí — keď sa commituje skôr, zostane v ňom bead
   otvorený a treba druhý commit, ktorý nehovorí nič než „doexportované".
   Poradie bolo dlho opačné a stálo presne toľko.
6. **Commit** so správou, ktorá hovorí prečo, nie čo. Diff hovorí čo. Píše sa
   **cez PowerShell a jeho here-string `@'…'@`**, nie do súboru a `git commit
   -F`: v dialógu na schválenie musí byť vidieť **text správy**, nie len
   príkaz, ktorý ju odniekiaľ prečíta. Súbor z toho robí schvaľovanie naslepo.
   Bashový heredoc na to nie je — toto prostredie mu žerie spätné lomky (viď
   Conventions). Uzatváracie `'@` musí stáť na začiatku riadku, inak to
   PowerShell neprečíta.

Bod 2 a 3 sú tie, ktoré sa vynechávajú, a sú to práve tie, ktoré rozhodujú
o tom, či sa dá pokračovať zajtra.

Codex číta `AGENTS.md`, a ten sa sem od nadpisu `## AgentAloud` jednou vetou
odvoláva. Projektové inštrukcie sa preto píšu **len sem**; `AGENTS.md` drží
iba hlavičku od beads a ten odkaz.
