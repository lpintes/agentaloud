# Ďalší krok

*Dočasná poznámka pre ďalšiu session. Netrackovaná, po prečítaní zmazať.*

## Kde sme skončili (6. 9. 2026, piata session dňa)

**`claude-gui-lkk.16` je zavretá** — `ClaudeLens --help` a `-h` vypíšu nápovedu
na konzolu a okno vôbec nevznikne. Vznikol `win/console.{h,cpp}`
(`WriteToParentConsole`): štandardný výstup, ak nejaký je → konzola rodiča cez
`AttachConsole(ATTACH_PARENT_PROCESS)` a `CONOUT$` → `MessageBox` ako záchrana.
`AllocConsole` zamietnuté — okno zomrie s procesom, text by blikol a zmizol.

Overené naostro **všetky štyri vetvy** (presmerovanie do súboru, skutočná
konzola z powershellu aj `cmd /c`, vynútene prázdne std handle cez
`STARTF_USESTDHANDLES`, spustenie cez `wscript.exe`). Odmerané pritom, že
cmd.exe aj powershell.exe **odovzdajú GUI procesu svoje konzolové handle**,
takže bežný prípad `AttachConsole` vôbec nepotrebuje.

Nový **invariant 16** v `CLAUDE.md` aj `AGENTS.md` (overené `diff`om).

Nový bead **`claude-gui-lkk.27`** (P3): nerozpoznaná voľba sa ticho stane
cestou k projektu — `--fork-session` skončí ako priečinok. Nápoveda to už
aspoň hovorí, ale odpovedať sa na to má textom na konzolu tou istou cestou.

**Dva commity na `main` sú nepushnuté** — push je na používateľovi.
(Sedem z minulej session medzitým odišlo, `origin/main` je na `d6a6768`.)

## Čo robiť ďalej

1. **`.6`** — zostávajú slash príkazy, dialóg Ctrl+/ so zoznamom, filtrovacím
   poľom a listboxom. Vzory: `src/ui/ask_dialog.{h,cpp}`,
   `src/ui/permission_dialog.{h,cpp}`, šablóny v `src/ui/claudelens.rc`.
   Zoznam prichádza v `system/init` aj v `control_response` na `initialize`.
2. **`.7`** (obnovenie prepisu z disku): `proto::ProjectSessionDir`
   a `ReadSessionSummary` ukazujú, ako sa k súborom dostať a ako sa čítajú.
   Pozor, formát na disku nie je formát streamu. Poznámku, ktorú história
   nahradí, treba nechať v tej časti, ktorá hovorí, **ktorá** session sa
   otvorila — tá platí aj po načítaní histórie.
3. **`.27`** je lacná a priamo nadväzuje: `win::WriteToParentConsole` už je
   napísaná, treba len povedať „neznáma voľba X" a skončiť nenulovým kódom.
4. **`.20`** (zoznam kláves) — nápoveda na konzole teraz existuje, ale klávesy
   do nej nepatria: tie treba, keď appka beží.

## Mechanika

```bash
./build.sh          # appku aj testy
./build.sh check    # LEN testy, appku neprekladá
./bin/claudelens.exe --help    # bezpečné, okno nevznikne
```

Novú binárku netreba nikam kopírovať: druhá inštancia sa spustí priamo
z `bin/claudelens.exe` popri používateľovej kópii z `go\bin`; prvý pozičný
argument je priečinok projektu. Pred spustením GUI to **vždy ohlás a počkaj** —
okno vezme fokus a do okna nevidíš, screenshot je čierny.

Testovaciu inštanciu zatváraj **podľa PID**, nikdy
`taskkill /IM claudelens.exe` — tým by si zabil živú session používateľa.

**Do okna sa dá pozrieť bez očí.** `SendMessage` `WM_GETTEXT` na
`RICHEDIT50W` (prepis) a `Edit` (prompt) cez `EnumChildWindows` — systém tú
správu marshaluje aj cez hranicu procesu. To isté funguje na `Static`
v `MessageBoxe` (takto sa overil fallback nápovedy) a zatvoriť sa dá
`PostMessage WM_CLOSE`. Písať sa dá `WM_SETTEXT` plus `SendKeys('^{ENTER}')`
po `SetForegroundWindow`, lebo chord sa číta zo skutočného stavu klávesnice.
**`GetWindowText` na to nepoužívaj**: cez hranicu procesu vráti pre `Edit` aj
`RichEdit` prázdny reťazec.

**Do konzoly sa dá pozrieť tiež.** Minimalizované okno `powershell` (Start-Process
`-WindowStyle Minimized`, nekradne fokus) spustí čo treba a potom si odfotí
`$Host.UI.RawUI.GetBufferContents()` do súboru. Tak sa overila nápoveda
v skutočnej konzole.

Príkazový riadok potomka: `Get-CimInstance Win32_Process -Filter
"ParentProcessId=<PID>"`.

Sondy v `tools/` (v hlavičke každej je, na akú otázku odpovedá):
`probe_init.py` (zadarmo), `probe_ask.py`, `probe_dialog.py`,
`probe_session_id.py` (stoja kredit), `sessions.py` (zadarmo).

Zvyšok je v `CLAUDE.md`, hlavne sekcia Invarianty a Uzavretie kroku.
