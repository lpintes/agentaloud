# Testy nad korpusom, fixtúry, sondy a pohľad do okna

Detaily, na ktoré odkazuje sekcia Build & Test v koreňovom `CLAUDE.md`.
Platia pre `tools/`, `tests/fixtures/` a overovanie naostro.

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
