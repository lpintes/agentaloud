# probe_permission_session — priebeh (claude-gui-lkk.61)

Otazka: da sa "Povolit na tuto session" (terminalove "Yes, and don't ask again
for ...") cez control kanal, bez zapisu do settings?

Pracovne priecinky: C:\b\perm-probe\<scenar> (mimo repozitara).
CLI 2.1.288, --model haiku.

## Stav

Vsetky scenare dobehli; zaznamy v C:\b\perm-probe\<scenar>\stream.jsonl.

## Zistenia

Pocet otazok `can_use_tool` na scenar (tri, resp. dva prompty):

- `ping` (navrhy CLI prepisane na session): 2 -- `ping -n 1` druhykrat
  bez otazky, `ping -n 2` znova. Navrh je `addRules` s `ruleContent` = cely
  presny prikaz, `destination: "localSettings"`.
- `bash` (`echo hello > hello.txt`, to iste): 3 -- presmerovanie do suboru sa
  pyta zakazdym aj s navrhom (`addRules "echo hello *"` + `addDirectories`
  s `destination: "session"`, `blocked_path`).
- `write` (Write): 1 -- navrh je `{"type":"setMode","mode":"acceptEdits",
  "destination":"session"}`; CLI to ohlasi `system/status` s
  `permissionMode: "acceptEdits"`.
- `rule` (ask pravidlo `Bash(echo:*)` v projekte): 3 -- `permission_suggestions`
  vtedy CHYBA (`decision_reason_type: "rule"`) a ask ma prednost aj pred
  vlastnym session pravidlom.
- `tool` (vlastne `addRules` bez `ruleContent`): 1 -- cely nastroj povoleny.
- `prefix` (vlastne `ping *`): 1.
- Ziadny scenar nezapisal do `.claude/settings*.json` projektu ani do
  `~/.claude/settings.json`.

Zaver: "Povolit na tuto session" = navrhy CLI s `destination` prepisanym na
`"session"`, ponuknute len ked navrhy prisli. Vlastne sirsie pravidla
(cely nastroj, prefix) funguju tiez, ale su to rozhodnutia, ktore CLI
nenavrhlo -- nepouzite.

## Overenie v appke (6. 10. 2026)

`bin/agentaloud.exe --model haiku C:\b\session-test`, backend claude, ovladane
cez `tools/lens.ps1` (Set-LensText + Send-LensChord Ctrl+Enter, BM_CLICK cez
PostMessage). Bez stlacenia klaves u pouzivatela. Zatvorene WM_CLOSE na hlavne
okno testovacej instancie (PID 14836); potomok claude.exe (13780) zanikol s nou.

1. Prompt "Spusti prikaz ping -n 1 127.0.0.1 nastrojom Bash." -> dialog
   titulok "AgentAloud — povolenie: Bash". Polia: Popis "Test loopback network
   interface with single ping", Dovod "other", Argumenty
   "command: ping -n 1 127.0.0.1 / description: ...". Tlacidla "&Povolit" (1),
   "Povolit na &tuto session" (1053, IsWindowVisible = true), "&Zamietnut" (2).
   Fokus v Argumentoch. Stlacene 1053 -> prikaz prebehol, v prepise
   "Bash: ping -n 1 127.0.0.1 / vystup (7 riadkov)". PRESLO.
2. Ten isty prompt znova -> dialog NEPRISIEL, prikaz prebehol, vystup (7 riadkov).
   PRESLO.
3. `ping -n 2 127.0.0.1` -> dialog znova (pravidlo je na presny prikaz), aj
   s tlacidlom 1053. Zamietnute -> v prepise "zamietnuté", model sa spytal, co
   dalej. PRESLO.
4. `C:\b\session-test\.claude\` neexistuje; hash `~/.claude/settings.json`
   pred aj po rovnaky (8E53BF53...088A0E). PRESLO.
5. Write a.txt -> dialog "AgentAloud — povolenie: Write", Popis "a.txt",
   Dovod "neuvedený", Argumenty "subor: C:\b\session-test\a.txt / obsah: / alfa".
   1053 viditelne, stlacene -> subor vytvoreny, stavovy riadok (cast 1)
   "model claude-haiku-4-5-20251001, rezim automatické úpravy" (pred tym prazdne
   pole 0; ostatne casti "projekt session-test" a limity). Dalsi Write b.txt sa
   nepytal, subor vytvoreny. Ani potom ziadny .claude v projekte, settings.json
   nezmeneny. PRESLO.

Postrehy:
- Pole "Dovod" pri Bash ukazuje surove slovo CLI "other" (pri Write
  "neuvedený" z katalogu) — "other" je nic nehovoriace a neprelozene.
- Stavovy riadok: WM_GETTEXT vrati len cast 0 (prazdnu); mod je v casti 1,
  cita sa cez SB_GETTEXTW s bufferom v cielovom procese (VirtualAllocEx).

