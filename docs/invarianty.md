# Invarianty AgentAloud — plný text

Skrátené pravidlá sú v `CLAUDE.md`; tu je ku každému dôvod, meranie a história.
Číslovanie bodov je to isté ako v `CLAUDE.md` a v beadoch.


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

   Zatvorenie session počas ťahu je ten istý prípad: `Stop()` v oboch
   adaptéroch najprv ťah preruší a až potom čaká (claude-gui-lkk.5.21).
   Predtým len čakal — 5 s z deštruktora — a potom zavrel stdin tak či tak,
   takže ťah dlhší než päť sekúnd (bežný) skončil presne tým, čo tento bod
   zakazuje. Kto chce ťah dobehnúť, volá najprv `WaitForTurn`.

   Ťah nezačína len náš prompt. Keď dobehne úloha na pozadí (Bash, subagent),
   CLI začne ťah samo: `system/task_notification` → `system/init` →
   `assistant` → `result`, **bez** `user` záznamu (odmerané 8. 10. 2026, CLI
   2.1.293, `tools/probe_selfturn.notes.md`). Codex to isté hlási
   `turn/started`. Preto `turnInFlight_` rozsvieti aj `system/init`
   (`Session::OnRecord`), resp. `turn/started` (`CodexBackend`), nielen
   `SendPrompt` — inak by Esc počas takého ťahu vrátil „nič nebeží" a `Stop()`
   by zavrel stdin uprostred neho (claude-gui-lkk.5.23).

   V Codexe chodia po tej istej rúre aj ťahy subagentov, každý s vlastným
   `threadId` (`tools/probe_codex_subagents.notes.md`). Stav ťahu
   (`turnInFlight_`, `TurnOver`, režim) sa preto berie **len z vlastného
   vlákna** (`own` v `CodexBackend::OnLine`); cudzie vlákno vidí iba
   translátor, ktorý z neho skladá úlohy na pozadí a jeho `turn/started`,
   `turn/completed` a spotrebu tokenov zahodí. Predtým `turn/completed`
   subagenta ukončil hlavný ťah — busy zhasol a Esc už nemal čo prerušiť
   (claude-gui-b8n.2).
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

   Von z modelu to platí presne po hranicu widgetu. Obyčajné `EDIT` pole
   (dialógy, nie RichEdit) zlomí riadok len na `\r\n` a osamotený `\n` nakreslí
   ako obdĺžnik — čo NVDA prečíta ako nič. Preklad späť do pravopisu widgetu
   robí `win::Dialog::SetTextLines` a robí ho na jednom mieste, aby si ho
   nekopíroval každý nový dialóg.
5. **Index bloku sa hýbe, `Block::id` nie.** Výsledok nástroja sa vkladá za
   svoje volanie, nie na koniec — Claude volá nástroje paralelne a v poradí
   príchodu sa nedá zistiť, ktorý výstup patrí ku ktorému príkazu. Vloženie
   doprostred posunie indexy všetkých blokov za ním. Čokoľvek, čo pomenúva blok
   naprieč časom — záložka, prvý nový blok dávky — preto drží `id`, nie index. Indexy
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

   Ťah, ktorý agent začal sám (dobehla úloha na pozadí), nemá klávesu, ktorá
   by ho ohlásila. Ohlási ho port: `agent::TurnStarted` prichádza na začiatku
   **každého** ťahu a panel ho pri `!busy_` berie ako cudzí ťah
   (`SessionPane::OnTurnStartedByAgent`): `busy_`, „pracujem" v stavovom
   riadku a do fronty „agent pracuje sám" (invarianty 7 a 11). Predtým sa
   ozvala až odpoveď a „hotovo", a hneď po prerušení to znelo, akoby
   prerušenie neprešlo (claude-gui-lkk.5.23). Vlastný ťah je v tej chvíli už
   `busy_` a `TurnStarted` ho necháva tak — a stavový riadok sa na začiatku
   ťahu **nikdy nečistí**: `system/init` prichádza aj po našom prompte
   a čistenie tam raz zmazalo „pracujem".

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
   `model::Transcript::SpeakerPrefix`, ten istý, ktorým ho píše prepis, nech sa
   reč a prepis nikdy nerozídu v tom, ako sa hovoriaci volá.

   Meno je **backendu**, nie modelu: `Capabilities::agentName` („claude",
   „codex") a panel ho prepisu dá cez `SetAgentName` ešte pred `Start`, teda
   pred obnovenou históriou. Potom sa meniť nesmie — prefix je v texte každej
   odpovede a zmena by posunula rozsahy bez úpravy, ktorá by o tom vedela.
   Kým ho nikto nedal, je to neutrálne „agent", lebo `model/` nevie, ktoré
   CLI beží.

   **Blok subagenta hovorí menom subagenta, a to každý** — aj volanie
   nástroja a jeho výstup, ktoré od hlavného agenta inak predponu nemajú
   (claude-gui-b8n.3). Subagenti na pozadí sa striedajú medzi sebou aj
   s hlavným agentom v poradí, v akom prišli, takže „Glob: **/*" bez mena
   nepovie, kto hľadá. Meno nesie port (pole `by`, prázdne = hlavný agent),
   model ho uloží pri vzniku bloku do `Block::speaker` a výsledok nástroja ho
   zdedí po svojom volaní. Meno je typ a poradové číslo za session
   („Explore 2"), vždy s číslom; dáva ho adaptér (Claude podľa
   `subagent_type` volania `Agent`, Codex podľa konca `agentPath`), nie model.
   Súhrn volania `Agent` to isté meno nesie pred popisom, aby sa ďalšie bloky
   dali k volaniu priradiť. Overené naostro 8. 10. 2026 s troma Explore na
   pozadí.

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

   **Za zavretým dialógom sa nehovorí — tam sa nedá.** Zánik dialógu je zmena
   fokusu, NVDA ju ohlasuje a pritom reč **ruší**, a k tomu sa dostane až vo
   svojom vlastnom cykle, o desiatky milisekúnd po tom, čo naša `Announce`
   dohovorila. Veta teda zanikne. Zaradiť ju namiesto prerušenia nepomôže:
   `cancelSpeech` vyprázdni celú frontu. A odložiť ju časovačom za to ohlásenie
   znamená odložiť ju za titulok okna a cestu projektu — čiže za text, ktorý
   je dosť dlhý na to, aby ho čitateľ umlčal Ctrlom, a s ním umlčí aj ju.

   Invariant 6 tým porušený nie je, len ho napĺňa niekto iný: fokus pristane
   v poli a NVDA prečíta riadok, na ktorom stojí — teda ten, do ktorého sa
   práve vložilo. Vlastnú vetu má zmysel povedať len tam, kde sa fokus nehýbe.
   Preto `ShowCommands` po vložení príkazu mlčí, hoci pôvodne hovoril; hláška
   „vložené /x, argumenty: …" sa nestratila v kóde, stratila sa v uchu, a našlo
   sa to používaním (7. 9. 2026).

   Dialóg, ktorého odpoveď je veta, sa preto **nezatvára**. Ctrl+F sa najprv
   na Enter zatváral a odpoveďou bolo ohlásenie návratu fokusu — titulok rámu,
   session, „Prepis" a až potom riadok so zhodou; autor to používaním nazval
   ukecaným (7. 10. 2026). Teraz zostane otvorený ako v Poznámkovom bloku,
   Enter povie riadok (`SessionPane::SearchAndSay`) a dlhé ohlásenie zaznie
   len na Esc, keď odchádza čitateľ sám.

   **Načasovať sa to proti čítačke nedá.** Odložiť vetu za cudzie ohlásenie
   a potom ju pretlačiť `cancelSpeech`om **technicky funguje** — odskúšané
   7. 9. 2026 na poznámke pri štarte (claude-gui-edx): pri správnom oneskorení
   sa titulok okna useknul uprostred a poznámka zaznela celá. Zamietnuté aj
   tak, a nie kvôli tomu, ako to znie. To oneskorenie je konštanta, ktorú nemá
   kde vziať: závisí od rýchlosti reči, nastavenia NVDA a záťaže stroja, takže
   číslo namerané tu je inde buď prikrátke — veta zanikne — alebo pridlhé,
   a vtedy preruší niečo, čo ešte nedopovedalo. Obe zlyhania sú tiché a obe
   nastanú tam, kde ich autor tejto aplikácie neuvidí. Kde treba prehovoriť po
   zmene fokusu, musí to teda urobiť niekto iný než časovač — dialóg, alebo
   fokus položený rovno na text, ktorý má zaznieť.
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

   To isté platí pre **protokolový obal**: neúspešný nástroj vracia telo
   zabalené v `<tool_use_error>…</tool_use_error>`. Je to značka pre stroj,
   nie text pre čitateľa — nahlas znie ako „menšie ako tool podčiarkovník
   error". Strháva ju `UnwrapToolError` v adaptéri (`proto/claude/translate.cpp`),
   a **nie** vo `Widen()`, hoci by sa to
   ponúkalo: `Widen` prechádza všetok text zo streamu vrátane odpovede
   asistenta, takže by zožrala aj vetu, v ktorej o tom tagu niekto píše. Tag
   sa vyskytuje výlučne v `tool_result`, takže tam patrí aj jeho odstránenie.
   Obal je zároveň druhý svedok chyby: `isError` je `is_error || obal`. Pole
   v streame naozaj chodí (`tests/fixtures/basic.jsonl`), takže sa číta prvé —
   hádať chybu z obsahu by bolo horšie než prečítať pole.

   **Riadiaci znak sa nezahadzuje — vypíše sa ako `\x00`.** Prepis
   sa do RichEditu podáva ako reťazec ukončený nulou (`EM_REPLACESEL`), takže
   `NUL` uprostred bloku **ukončí vkladanie**: model drží text, ktorý widget
   nedostal, mapa rozsahov od toho miesta neplatí a každý skok za ním pristane
   inde. Odmerané 6. 9. 2026 na živej session — `grep -a` nad `.exe` vrátil 502
   znakov s 19 nulami (reťazce v binárke sú UTF-16LE, čiže každý druhý bajt je
   nula), widget zobral prvých 47 a titulok povedal „NESÚLAD MAPY ROZSAHOV" až
   vtedy, keď už bola navigácia mimo. Výstup nástrojov je výstup terminálových
   programov a niektoré z nich čítajú binárky, takže je to bežný prípad, nie
   exotika.

   Robí to `EscapeControls` vo `Widen()`, a **až za `NormalizeNewlines`**:
   `CR` je tiež riadiaci znak a osamotené `CR` je zlom riadku, takže vypísať ho
   skôr by z toho riadku spravilo `\x0d` namiesto `\n`. `TAB` a `\n` zostávajú
   sebou samými, tie sú text. `VT` (0x0B) a `FF` (0x0C) sa vypisujú aj kvôli
   invariantu 4 — nechané tak sú pre RichEdit zlomy, o ktorých model nevie. Tou
   istou cestou ide aj prompt (`AppendUserPrompt`): do editačného poľa sa dá
   vložiť text odkiaľkoľvek.

   **Vypísať, nie zahodiť**, a je to rozdiel medzi dvoma druhmi ticha.
   Zahodenie bola prvá oprava a bola to zlá polovica odpovede: škodu zastavila
   a informáciu stratila, čiže prepis by ticho mal menej znakov, než nástroj
   vypísal. Escape sekvencie sa zahadzujú preto, že sú to **pokyny terminálu**;
   riadiaci znak uprostred výstupu je **obsah** — niekto čítal binárku — a keď
   sa vypíše, je aj počuť: „spätná lomka x nula nula" oproti ničomu. Zápis je
   nerozoznateľný od nástroja, ktorý tie štyri znaky vypísal doslova, a berie
   sa to: terminál ich nerozozná tiež, a jediná notácia, ktorá by to vedela
   (U+2400 CONTROL PICTURES), sa nahlas číta ako ticho.

9. **Proces je DPI-aware, a nie kvôli ostrému textu.** Bez
   `SetProcessDpiAwarenessContext` (prvý riadok `wWinMain`) škáluje okno
   Windows sám a súradnice, ktoré si prečíta iný proces, sa pritom
   zaokrúhľujú: obdĺžnik stavového riadku hovorí, že siaha po posledný riadok
   klientskej oblasti, ale hit-test na tom istom riadku spadne na rámové okno.
   NVDA hľadá stavový riadok práve tam — `api.getStatusBar()` sa pýta, aký
   objekt sedí v ľavom dolnom rohu klientskej oblasti — a keď ho nenájde,
   prečíta namiesto neho posledný riadok plochého prehľadu, čiže text, ktorý
   je práve na obrazovke. Preto NVDA+End čítal prepis. Odmerané pri 150 %:
   unaware zlyhá, aware nájde bar; závisí to od parity rozmerov, takže to
   vyzerá ako náhodné („chvíľu po spustení to funguje").

   Cena je, že appku už neškáluje nikto iný: rozmery v 96-DPI jednotkách sa
   musia prenásobiť samy (`MulDiv(x, dpi, 96)` v `SessionPane::Layout`
   a `win::Window::Create`), font sa pýta cez `SystemParametersInfoForDpi`,
   nie `SystemParametersInfoW`, a `WM_DPICHANGED` musí prijať obdĺžnik, ktorý
   Windows ponúka. Vynechať ktorýkoľvek z tých krokov znamená okno o tretinu
   menšie alebo orezané popisky — a to je chyba, ktorú autor tejto aplikácie
   neuvidí.

10. **Dieťa nesmie prežiť rodiča.** Keď appka zomrie inak než poriadne —
    `taskkill /F`, pád — nebeží pri tom **žiadny náš kód**: ani deštruktor,
    ani `atexit`, ani handler. Upratovanie v `~Process()` teda principiálne
    nemôže stačiť, lebo nie je kam ho napísať, a osirelé `claude.exe` zostane
    visieť s pol gigabajtom a nikým na druhom konci rúry.

    Drží to jadro, nie my: `win::Process` dáva dieťa do job objectu
    s `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. Keď zomrie posledný handle na job
    — a ten držíme my — jadro pozabíja všetko vnútri. Handle mŕtveho procesu
    zatvára OS bez ohľadu na príčinu smrti, takže to platí aj pre pád.

    Tri veci na tom zlyhávajú ticho:

      • **`CREATE_SUSPENDED`, potom `AssignProcessToJobObject`, potom
        `ResumeThread`.** `claude.exe` je len launcher a robotu robí node,
        ktorý spustí. Priradenie až po štarte nechá vnuka vzniknutého v tom
        okne mimo jobu — čiže práve ten proces, kvôli ktorému to celé je.
      • **Handle jobu nesmie byť dediteľný.** `CreateProcessW` tu dedí handle
        kvôli rúram; zdedený job by znamenal, že dieťa drží handle na job,
        v ktorom sedí, posledný handle nezmizne nikdy a mechanizmus nerobí
        nič — potichu a presne v tom jedinom prípade, pre ktorý existuje.
      • **Zlyhanie priradenia nie je fatálne.** Session beží ďalej, len bez
        záruky. Od Windows 8 môže byť proces vo viacerých joboch naraz, takže
        spustenie z cudzieho jobu (terminál, debugger) to už nerozbije.

    Overiť sa to dá bez kreditu a musí sa to overiť **obojstranne**: rodič,
    ktorý cez `win::Process` spustí `cmd.exe /c ping -n 300 127.0.0.1` (teda
    dieťa aj vnuka), sa nechá zabiť cez `taskkill /PID <rodič> /F`. S jobom
    nezostane ani jeden potomok, bez neho prežijú všetci. Bez tej druhej
    polovice znamená „prešlo to" iba to, že ping medzitým dobehol.

    Nepomôže to, keď appka visí, ale žije. To je iná úloha.

11. **Priebežná reč má tri stavy a v dvoch z nich mlčí.** Invariant 7 zakazuje
    vlastnej reči skákať do fronty NVDA. Napĺňať tú frontu textom, ktorý nikto
    nečaká, je ten istý priestupok z druhej strany, a rozhoduje o tom
    `ui::SessionPane::WantsProgressSpeech`:

      1. **Okno nie je v popredí — alebo session nie je aktívna.** Čokoľvek
         čitateľ práve robí, nie je to toto. Session za inou session (MDI,
         invariant 24) je na tom rovnako ako okno za inou aplikáciou; panel
         sa to dozvie od hostiteľa (`SessionPane::SetActive`), sám sa pýtať
         nesmie. Mlčí sa. Koniec ťahu je výnimka, ale **zvukom, nie vetou** —
         kto medzitým píše mail, zruší našu vetu prvým stlačením klávesy, lebo
         NVDA pri písaní reč ruší, a „hotovo" zanikne uprostred. Reč je na
         pozadí nespoľahlivý nosič z princípu, nie kvôli nášmu kódu. Zvuk vo
         fronte nie je a nezruší ho nič.
      2. **Fokus je v prepise a kurzor si ho čitateľ presunul sám.** Číta si
         staršie miesto a NVDA mu pritom číta riadky, po ktorých sa pohybuje;
         naša priebežná reč sa s tým prepletie na nezmysel. Mlčí sa.
      3. **Fokus je v prompte, alebo kurzor sleduje koniec.** Vtedy sa čaká
         práve na toto. Hovorí sa všetko.

    Rozhoduje **poloha kurzora, nie ktoré pole má fokus** — kto stojí na konci,
    ten čaká na to, čo príde. Tá otázka už v appke bola zodpovedaná: je to
    `Following()`, teda „kurzor je na konci alebo presne tam, kam ho položil
    `Send`" (`anchor_`, ktorý `Apply` vedie cez `MoveOffset` pri každej úprave,
    takže platí celý ťah aj pri vkladaní nad kurzor). **Nulta záložka to nie
    je**, hoci to tak návrh tvrdil: `OnDrain` ju zapisuje pri každej dávke,
    ktorá pridala blok, bez ohľadu na kurzor.

    „Kurzor je na konci" pritom **neprežije ani jedno pripísanie.** Append
    začína presne tam, kde kurzor stojí, a `MoveOffset` necháva všetko na
    začiatku úpravy a pred ním na mieste — takže hneď po prvej dávke je kurzor
    nad novým textom a nesedí ani jeden z tých dvoch testov. Preto `Apply`
    kurzor, ktorý na konci stojí, prevezme za `anchor_` ešte kým to platí.
    Bez toho čitateľ, ktorý dobehol koniec cez Ctrl+End, počul jednu dávku
    a potom ticho až do konca session — a `Send` mu prestal presúvať kurzor,
    lebo je to tá istá otázka. Znie to ako pokazená reč, hoci sa pokazila
    odpoveď na „číta ešte?".

    Zvuk pre koniec ťahu na pozadí musí byť **iný než `MB_ICONASTERISK`** — ten
    už znamená užšiu vec („ťah skončil a nič nezaznelo", invariant 6). Jeden
    zvuk na dve udalosti neznamená ani jednu.

    **Modálne okno na pozadí je tretia udalosť a potrebuje tretí zvuk.**
    Otázka od modelu aj žiadosť o povolenie ťah **zastavia**, takže mlčať sa tu
    nedá — a odmerané (5. 9. 2026) je aj to, že Windows appke na pozadí
    popredie nedá: okno nevyskočí, len tam stojí. Bez ohlásenia to znamená
    zastavený ťah, o ktorom nikto nevie; čitateľ sa vrátil sám po minúte.
    Ohlasuje to `ui::SessionPane::SignalWaiting`: `FlashWindowEx`
    s `FLASHW_TIMERNOFG` (bliká, kým okno nepríde do popredia — a prísť môže
    len preto, aby sa odpovedalo) a zvuk. Reč nie, z toho istého dôvodu ako pri
    „hotovo".

    Zvuk **nie je** `MessageBeep`, a to je pointa: jeho dva použiteľné zvuky už
    významy majú (`MB_ICONASTERISK`, `MB_ICONEXCLAMATION` vyššie) a
    `MB_ICONQUESTION`, ktorý sa sám ponúka ako tretí, nemá vo východiskovej
    schéme Windows priradený **žiadny súbor** — zlyhal by presne tým jediným
    spôsobom, na ktorom tu záleží, teda ticho. Preto `PlaySoundW` s aliasom
    `Notification.Default`, a **bez** `SND_NODEFAULT`: nepriradený alias potom
    spadne na systémový východiskový zvuk namiesto na ticho.

    A keď sa čitateľ vráti, fokus musí pristáť **v tom okne**. Modál svojho
    vlastníka **zakáže**, a na dieťa zakázaného okna sa `SetFocus` nedá —
    zlyhá, a zlyhá ticho. Preto sa `MainWindow::RestoreFocus` na `WM_SETFOCUS`
    nevolá, kým je okno zakázané, a `WM_ACTIVATE` namiesto toho vytiahne
    dopredu `GetWindow(hwnd, GW_ENABLEDPOPUP)` — tá konštanta existuje presne
    na túto otázku. Bez toho NVDA prečítala titulok okna a stíchla a do
    dialógu sa dalo dostať až Tabom.

    `Announce` (odozva na klávesu) sa toto **netýka** a nikdy sa netlmí: môže
    prísť len vtedy, keď okno fokus má, a podľa invariantu 6 musí znieť vždy.

    Za zamlčané sa nič nedlží a nič sa nedobieha. Text stojí v prepise a
    `t`/`r`/`a`/`E` a `Ctrl+0` k nemu vedú — práve to terminál nemá.
    „Neohlásilo sa to samo" preto nie je to isté ako „zaniklo", a tento bod sa
    nedá porovnávať s prerušením, ktoré bez druhej hlášky znelo ako ticho,
    lebo po ňom nezostávalo nič.

12. **`AskUserQuestion` nie je žiadosť o povolenie, hoci ňou pricestuje.** Keď
    sa model spýta na výber z možností, na control kanál nepríde nový subtyp —
    príde obyčajný `can_use_tool` pre nástroj menom `AskUserQuestion`, s
    `requires_user_interaction: true`, a odpoveď sa vracia v poli, ktoré inak
    slúži na úpravu argumentov nástroja:

        allow + `updatedInput` = vstup nástroja doplnený o `answers`,
        objekt kľúčovaný **textom otázky** — nie indexom a nie hlavičkou.

    Odmerané cez `tools/probe_ask.py` (5. 9. 2026) celým okruhom: odpoveď
    `{"Čo piješ radšej?": "Čaj"}` dala `tool_result` „Your questions have been
    answered…" a model pokračoval. Jednovýberová otázka sa odpovedá reťazcom,
    viacvýberová poľom — validátor CLI ich rozlišuje a pri nezhode preformuluje
    výsledok. Odpoveď mimo ponúknutých návestí je dovolená; CLI ju prepustí
    modelu, len ju uvedie inak.

    Kým to appka nevedela, otázka s tromi možnosťami sa ukázala ako povolenie
    s „Áno" a „Nie": otázka na obrazovke bola a odpovedať sa na ňu nedalo.
    **Zlyháva to ticho v tom najhoršom zmysle** — vyzerá to ako odpovedaná
    otázka, len s nezmyselnou odpoveďou. Preto sa `AskUserQuestion` musí vetviť
    **pred** všeobecným promptom na povolenie — v adaptéri
    (`proto::ClaudeBackend::OnPermission`), ktorý z toho spraví
    `agent::QuestionRequest`; panel dostane otázku a povolenie zvlášť
    a nevie, že pricestovali rovnako —
    a vetví sa podľa **mena nástroja**, nie podľa `requires_user_interaction`:
    ten príznak hovorí, že sa čaká na človeka, nie ako vyzerá payload, a čítať
    payload podľa neho je to isté hádanie o krok neskôr.

    `request_user_dialog` je pritom skutočný subtyp so skutočnými druhmi
    (`permission_bash`, `permission_ask_user_question`, `refusal_fallback_prompt`
    a ďalších vyše tridsať, nájdené v binárke), ale CLI na ňom **zlyháva
    zatvorene**: druh pošle len klientovi, ktorý ho vymenoval v
    `initialize.supportedDialogKinds`, a my nevymenúvame žiadny. Tá vetva teda
    nie je neobslúžená, je nedosiahnuteľná — a chybová odpoveď, ktorou na ňu
    `session.cpp` odpovedá, je dvojnásobne neškodná: chybovú odpoveď na dialóg
    CLI zahadzuje a dialóg necháva zaparkovaný. Prihlásiť sa o druh je vlastný
    krok, nie oprava tohto (claude-gui-lkk.25).

    **Codex sa pýta dvoma spôsobmi a vyberá si model, nie appka.** Model
    gpt-6-luna dostane vždy oba nástroje — blokujúci `request_user_input`
    (serverová požiadavka, odpoveď kľúčovaná **id** otázky, vždy pole) aj
    `request_user_input_async`, ktorý pridáva katalóg modelov
    (`experimental_supported_tools`). Odmerané 4. 10. 2026 presmerovaním Codexu
    na lokálny server, ktorý požiadavky na model len zapisoval: ponuka bola
    rovnaká pri každej kombinácii plánu, effortu a prvého ťahu. Pri overení
    appky zvolil model dvakrát async, pri sonde šesťkrát sync.

    Async otázka pricestuje ako obyčajná správa asistenta s `delivery:"async"`
    a poľom `questions` a ťah pokračuje alebo skončí; **odpoveď je ďalší
    prompt**. Adaptér z nej robí ten istý blok otázky a navyše
    `agent::QuestionByPrompt`. Panel ju podrží do konca ťahu, potom pošle
    `kMsgQuestionByPrompt` (nie priamo z drainu — modálna slučka by v ňom
    spracovala ďalšie dávky uprostred jednej) a ponúkne **ten istý dialóg**;
    vybraté ide von cez `SendText`, teda ako prompt vo všetkom — v prepise
    „ja: Čaj", v stavovom riadku aj v reči. Zrušený dialóg nepošle nič a
    otázka zostane v prepise. Ťah prerušený Esc dialóg nedostane.

    Za volanie async otázky pripíše **panel** výsledok — vybratú odpoveď,
    alebo „bez odpovede" ako chybu —, presne ako ho blokujúcej otázke dáva
    adaptér. Preto `QuestionByPrompt` nesie id volania. Bez toho vyzerala
    zrušená otázka rovnako ako otázka, ktorá ešte čaká, a zistilo sa to
    overením naostro (claude-gui-lkk.44.11). Upravený
    katalóg bez async (`-c model_catalog_json=…`) by sync zaručil, ale je to
    snímka, ktorá s ďalšou verziou Codexu ticho zastará — zamietnuté.

    **Serverová požiadavka Codexu, ktorú adaptér nepozná, je zamietnutie.**
    Odpovedá sa na ňu `-32601` (nechať ju visieť by zastavilo ťah — server
    čaká bez časového limitu), a Codex chybu číta ako „nie". Tak sa stratilo
    povolenie MCP nástroja aj pluginu computer use: prichádza ako
    `mcpServer/elicitation/request` — raz od Codexu pred volaním nástroja,
    raz od samotného servera — a plugin potom modelu povedal „not approved"
    bez jediného dialógu (claude-gui-lkk.44.12). Formulár bez polí je dnes
    dialóg povolenia (`proto::codex::ElicitationPermission`); formulár s poľami
    a `mode: "url"` stále padajú na `-32601` (claude-gui-lkk.44.13). Kto
    hľadá, prečo Codex hlási zamietnutie, ktoré nikto nevidel, nech začne pri
    `Unrecognised` so serverovou metódou.

13. **Čo dialóg ukáže pred spustením nástroja, to prepis drží po ňom — a je to
    ten istý text.** Povolenie sa pýta na volanie, ktoré o chvíľu ukáže prepis
    znova. Keby to boli dve rôzne vykreslenia, čitateľ povolí jedno a prečíta
    si druhé, a ten rozdiel by nikto nenašiel: obe by vyzerali rozumne. Preto
    `model::RenderToolCall` vyšiel z anonymného priestoru `transcript.cpp` a
    berú ho obaja — `ui::AskPermission` aj `MakeToolUse`. A obaja ho volajú
    nad tým istým `agent::ToolCall`, ktorý pre Claude skladá jediná funkcia
    `proto::ToolCallFromInput` — z `tool_use` bloku aj zo žiadosti
    `can_use_tool`.

    Dovtedy tam bol `input.dump(2)` v `MessageBoxe` a bol to posledný zvyšok
    surového JSONu v UI. Dump je pravdivý a nečitateľný naraz: viacriadková
    commit správa je v ňom jeden riadok so `\n` namiesto zlomov — nahlas
    „spätná lomka en" — a `MessageBox` sa nedá prejsť po riadkoch ani po
    slovách. Taká správa sa neprečítala, iba odklikla.

    Volanie, ktoré mení súbor, začína riadkom **„súbor: <celá cesta>"**.
    Zhrnutie v prepise cestu má (skrátenú), ale dialóg zhrnutie nemá vôbec, a
    tak sa „obsah: …" pýtalo na zápis bez toho, aby povedalo kam. Našlo sa to
    naostro na `apply_patch` Codexu (4. 10. 2026) a platilo to rovnako pre
    Claudov `Write` aj `Edit`.

    Meno nástroja je v **titulku** dialógu, nie v poli, a fokus štartuje
    v argumentoch. NVDA číta pri otvorení najprv titulok a potom zameraný
    prvok, takže „povolenie: Bash" a samotný príkaz prídu ako jedno ohlásenie
    a v tomto poradí. V poli by bolo meno o Tab ďalej než odpoveď.

    **„Povoliť na túto session" povolí presne to, čo navrhlo CLI, a nezapíše
    nič na disk.** Kým dialóg vedel len áno/nie, pýtal sa computer use
    v Codexe pri každom kroku a Bash pri každom príkaze (claude-gui-lkk.61).
    Tlačidlo sa ukáže len tam, kde ho backend ponúka (`PermissionRequest::
    offered`), a backend ho ponúka len vtedy, keď ho ponúka jeho CLI:
    Claude, keď pošle `permission_suggestions`; Codex pri nástroji MCP podľa
    `_meta.persist` a pri príkaze podľa `availableDecisions`. Návrhy Claude
    idú späť s `destination` prepísaným z `localSettings` na `session`
    (`proto::SessionPermissions`) — inak by odpoveď v dialógu ticho zapísala
    do `.claude/settings.local.json`. Z toho istého dôvodu sa neponúka
    Codexovo „always" ani `acceptWithExecpolicyAmendment`. Vlastné širšie
    pravidlo (celý nástroj, prefix) by CLI prijalo, ale je to rozhodnutie,
    ktoré nikto nenavrhol. Odmerané `tools/probe_permission_session.py`.

    Rozsah je teda **taký, aký ho CLI navrhlo**, a dialóg ho zatiaľ nepovie
    (claude-gui-lkk.62): pri Bash je to celý presný príkaz, pri `Write`
    prepnutie do `acceptEdits` (CLI to ohlási `system/status` a stavový
    riadok to zachytí), pri nástroji MCP celý nástroj. Pravidlo `ask`
    v nastaveniach návrhy nepošle a má prednosť aj pred session pravidlom.

14. **Id session si appka určuje sama a pozná ho pred štartom procesu.** Zo
    streamu príde až so záznamom, ktorý ho nesie, a odpoveď na `initialize` ho
    nemá: v projekte bez `SessionStart` hookov je teda známe až po prvom ťahu
    (odmerané `tools/probe_init.py`). To, že ho F2 na tomto stroji ukázalo
    hneď, bola zásluha hooku `bd prime`, čiže vlastnosť stroja, nie appky.
    Preto `proto::Session::Start` vyrobí `--session-id` sám (`NewSessionId`,
    holý UUID malými písmenami z `CoCreateGuid`) a id platí od chvíle, keď
    proces vznikne. Pod tým menom leží aj súbor v `~/.claude/projects/…`.

    To isté id sa druhýkrát použiť **nedá** a `--session-id` nie je náhrada za
    obnovenie: CLI povie „Error: Session ID … is already in use.", skončí
    s kódom 1 a na stdout nepovie nič. Obnovuje `--resume`, ktoré si obnovené
    id ponechá, a druhé `--session-id` k nemu CLI odmietne bez
    `--fork-session`. Obe hlášky idú na stderr; do 8. 10. 2026 sa to prejavilo
    ako session, ktorá sa spustila a mlčí, dnes ako blok „Agent skončil
    s kódom 1." s tou hláškou (invariant 25). `Start` preto id nevyrobí, keď o rozhovore hovoria už
    `extraArgs` (`--resume`, `-r`, `--continue`, `-c`, vlastné `--session-id`).
    Odmerané 6. 9. 2026, `tools/probe_session_id.py`.

    Obnovuje sa `--resume <id|titul>`. `main.cpp::ReadArguments` ho prečíta aj
    s hodnotou a odovzdá ako `agent::StartOptions::Resume::ById`; na `--resume`
    pre CLI ho späť prepíše až adaptér (`proto::ClaudeBackend::Start`), lebo
    pravopis voľby patrí CLI, nie appke. Hodnota sa nekontroluje proti tvaru
    UUID: CLI tam berie aj
    titul session. Holé `--resume` pod `--print` **neotvorí picker** — odmietne
    sa hláškou „--resume requires a valid session ID or session title when used
    with --print" a ťah skončí `result`om s `is_error` (odmerané 6. 9. 2026).

    **Obnovená session o sebe nepovie nič, a je to zámer.** Stream históriu
    neposiela, takže sa číta z disku (invariant 18) — a čo sa prečítalo, stojí
    v prepise, kde sa to dá prečítať. Poznámka pred históriou tam bola
    a vypadla (8. 9. 2026): „Predchádzajúce ťahy nasledujú" hovorí to isté, čo
    o riadok nižšie hovorí sám prepis, a obnovenie session je vedomý úkon, čiže
    odpoveď na nepoloženú otázku. Kto chce vedieť, ktorý rozhovor to je, prejde
    Ctrl+Home na prvý prompt; ten to povie lepšie než veta o ňom.

    Prázdny prepis po `--resume` je preto bežný stav bez hlášky: `--resume`
    berie aj titul session, titul nie je meno súboru, a súbor sa teda nemusí
    nájsť. S poznámkou aj bez nej je odpoveď tá istá — v prepise nič nie je.

    S poznámkou zmizol `Transcript::AppendNote` aj `BlockKind::Note`, teda
    vlastný hlas appky v prepise. Keby sa niekedy vracal, patrí sem s ním aj
    dôvod, prečo `AssistantText` nestačí: boli by to slová vložené Claudovi
    do úst. Vlastný hlas appky v prepise dnes majú dva bloky, oba bez
    predpony hovoriaceho a oba ako odpoveď na udalosť, nie ako komentár:
    `Interrupted` a `SessionEnded` (invariant 25).

    `ResumesConversation` je pritom **užšia otázka** než `SaysWhichConversation`,
    ktorou sa riadi vlastné id vyššie: `--session-id` rozhovor pomenúva, ale
    v žiadnom nepokračuje, takže po ňom nie je čo čítať z disku a prepis je
    prázdny oprávnene.

15. **`-c` sa CLI neposiela; najnovšiu session si appka vyberie sama.** CLI
    odpovedá na otázku „ktorý rozhovor bol v tomto adresári posledný" z
    `~/.claude/history.jsonl`, a tam sa zapisuje **iba interaktívne napísaný
    prompt**. Žiadna session AgentAloud tam teda nie je ani jedna a `claude -c`
    by pokračoval v poslednom **terminálovom** rozhovore a tváril sa, že je náš.
    Zoznam si preto robí `proto::sessions` zo súborov v
    `~/.claude/projects/<kľúč>/*.jsonl` — tie headless session píše rovnako ako
    ktorékoľvek iné — a `proto::ClaudeBackend::Start` z toho pri
    `Resume::Latest` urobí obyčajné `--resume <id>`. Je to v adaptéri, nie
    v `main.cpp`, lebo kde CLI drží rozhovory, je vedomosť o Claude; Codex sa
    na to isté pýta vlastným volaním. Kľúč adresára je cesta projektu, v ktorej sa každý znak
    mimo `[A-Za-z0-9]` zmenil na pomlčku (odtiaľ to dvojité `--` za písmenom
    disku).

    **Najnovšia je podľa času posledného záznamu, ktorý čas nesie — nie podľa
    mtime.** Zatváranie session dopisuje záznamy bez `timestamp`
    (`last-prompt`, `atis-latch`), takže mtime hovorí, kedy sa naposledy
    zapisovalo, nie kedy sa naposledy rozprávalo; o poradí dvoch session
    zavretých tesne po sebe by rozhodlo niečo, čo s rozhovorom nemá nič
    spoločné. Čas sa preto hľadá odzadu, prvý záznam, ktorý ho má. ISO 8601
    so `Z` sa dá triediť ako text, takže sa nikdy neparsuje.

    A **ktorú session otvorilo, appka nehovorí.** Pôvodne to bola veta o čase
    a prvých slovách rozhovoru; vypadla s poznámkou (invariant 14), lebo prvý
    prompt toho rozhovoru je prvý blok prepisu a Ctrl+Home k nemu vedie —
    povedať to isté ešte raz vetou navyše je len blok, cez ktorý sa treba
    prejsť.

    Preto sa **projekt bez jediného rozhovoru správa presne ako prázdny
    projekt**: `-c` v ňom nemá čo obnoviť, začne sa nová session a nepovie sa
    nič. Hláška „tu zatiaľ žiadny rozhovor nie je" by rozlišovala dva stavy,
    ktoré sú pre čitateľa jeden a ten istý — prázdne okno, do ktorého sa píše
    prvý prompt.

    Explicitné `--resume <id>` vyhráva nad `-c` — kto napísal id, vie, čo chce.

    `SessionSummary::firstPrompt` a `proto::LocalTimeText` tým prišli o jediného
    volajúceho v produkčnom kóde a zostali len pod testami. Nezmazali sa: sú to
    práve tie dve veci, ktorými sa bude session v zozname pomenúvať
    (claude-gui-lkk.7), a prvý prompt je pritom ten, ktorý napísal **človek** —
    `user` záznam nesie aj výsledky nástrojov a výpisy slash príkazov (tie sa
    poznajú podľa značky na začiatku), takže session pomenovaná niečím, čo
    nikto nepovedal, by bola horšia než nepomenovaná.

16. **Appka nemá kam písať, a keď má niečo povedať pred oknom, musí si to
    miesto vypýtať.** `-mwindows` znamená žiadnu konzolu, teda ani stdout, ani
    stderr — a `printf` do neexistujúceho handle sa nesťažuje. Presne tak
    zmizli hlášky CLI o zle použitom `--session-id` (invariant 14) a presne tak
    by zmizla nápoveda.

    Miesto sa hľadá v tomto poradí a prvé nájdené vyhráva
    (`win::WriteToParentConsole`):

      • **Štandardný výstup, keď nejaký je.** Presmerovanie musí byť prvé,
        inak by `agentaloud --help > help.txt` napísalo na obrazovku a nechalo
        prázdny súbor. Odmerané: cmd.exe aj powershell.exe odovzdajú GUI
        procesu svoje konzolové handle, takže v bežnom prípade sa končí tu.
      • **Konzola rodiča** cez `AttachConsole(ATTACH_PARENT_PROCESS)`, a keď
        ani potom nie sú štandardné handle vyplnené, `CONOUT$` menom.
        Odmerané vynútením prázdnych handle (`STARTF_USESTDHANDLES` s `NULL`):
        vetva funguje a text pribudne pod už vypísaný prompt shellu — shell na
        GUI proces nečaká. Preto sa v tejto vetve, a len v nej, predradí
        prázdny riadok.
      • **Dialóg, a to je záchrana, nie voľba.** Ostáva na spustenie
        z Prieskumníka alebo zo skratky, kde konzola nie je nikde v strome.
        `AllocConsole` je horšia odpoveď: okno, ktoré vyrobí, zomrie s
        procesom, takže by text blikol a zmizol, čo je to isté ako nevypísať
        ho.

    Kódovanie sa pýta rovnako: `WriteConsoleW` berie UTF-16 a codepage si
    vyrieši sám, do súboru či rúry sa píše UTF-8. Rozlišuje sa podľa toho, či
    `GetConsoleMode` na tom handle prejde — inak sa to nedá, oba sú `HANDLE`
    a do oboch sa dá písať.

    Nápoveda samotná je v `src/arguments.cpp::HelpText`, v tom istom súbore
    ako `Parse`, ktorý ju napĺňa pravdou — a čo sa dá, berie z pravdy priamo:
    zoznam backendov a režimy predvoleného backendu z jeho `Capabilities`.
    Parser je mimo `main.cpp` preto, aby ho videli testy (`TestArguments`);
    `main.cpp` z neho drží len čítanie argv, výber priečinka a `Expand`.

    Tou istou cestou chodí aj odmietnutie, a je to tá druhá polovica toho
    istého pravidla: **argument začínajúci pomlčkou, ktorý appka nepozná, sa
    nesmie stať priečinkom projektu.** Povie sa „neznáma voľba X" a skončí sa
    kódom 2. Kým to tak nebolo, `--fork-session` sa ticho stal cestou a session
    sa spustila nikde — presne tá trieda chyby, kvôli ktorej vznikol tento
    invariant. To isté platí pre voľbu s chýbajúcou hodnotou: `--model` na
    konci riadku by inak zjedol priečinok o argument ďalej. Holá pomlčka spadá
    pod to isté pravidlo zámerne — na Windows to nie je cesta, ktorú by niekto
    myslel vážne, a pravidlo s jednou výnimkou si nikto nezapamätá. Cena je, že
    priečinok s pomlčkou na začiatku mena sa zadať nedá. Nápoveda tú cenu
    hovorí, lebo neuhádol by ju nikto.

    **Voľba pre CLI ide za oddeľovač `--`, a nijako inak.** Všetko za
    samostatným `--` ide do `StartOptions::extraArgs` bez čítania a bez
    kontroly, a adaptér to pošle svojmu CLI; pred `--` platí všetko vyššie.
    Appka tak nemusí vedieť, ktoré CLI má ktoré parametre — a nesmie hádať,
    ktoré slová za neznámou voľbou sú jej hodnoty, lebo práve tak sa hodnota
    stávala priečinkom. Kým to nebolo, `--chrome` sa do CLI nedalo dostať
    vôbec a jediná cesta bol terminál (claude-gui-lkk.44.7, 3. 10. 2026).

    Je to **zmena rozhodnutia**. `--` sa tu predtým zvažovalo a zamietlo ako
    „druhé pravidlo pre prípad, ktorý na Windows nenastáva" — no vtedy
    len ako spôsob, ako zadať priečinok s pomlčkou. Dôvod, ktorý ho teraz
    zaviedol, je iný a skutočný. CLI, ktoré parameter odmietne, to povie na
    stderr a skončí; appka to ukáže ako blok v prepise (invariant 25).

    **`--backend <meno>` je nepovinné** a predvolené je `claude` — povinné by
    rozbilo každú skratku a nepovedalo by nič, čo appka nevie. Neznáme meno sa
    odmietne s vymenovaním známych. Zoznam backendov a ich výroba sú
    v `main.cpp` (`kBackends`, `MakeBackend`), jedinom mieste, ktoré adaptéry
    pozná.

    **`--permission-mode` sa overuje proti režimom zvoleného backendu**
    (`app::CheckMode` nad `Capabilities::modes`) a pri preklepe sa vymenujú
    platné. Predtým sa neoverovalo s tým, že CLI odmietne, čo nepozná — no CLI
    odmieta na stderr, teda ticho, a s dvoma CLI sa slová režimov líšia.
    Backend sa preto vyrobí ešte pred výberom priečinka: jeho výroba proces
    nespúšťa, a odmietnutý režim nesmie dostať odpoveď „ktorý priečinok?".

    Zoznam režimov je teda zoznam toho, čo CLI **naozaj berie**, nie toho, čo
    sa cykluje — inak by appka odmietla slovo, ktoré CLI samo ponúka. Pre
    Claude je v ňom aj `manual`: `--help` CLI vypisuje `manual` namiesto
    `default`, CLI ho prijme a hlási späť ako `default` (odmerané na 2.1.288,
    `tools/probe_cli_args.py`, 3. 10. 2026). Adaptér ho preto CLI pošle rovno
    ako `default` a panel si počiatočný režim berie od backendu, nie
    z príkazového riadka — inak by prvé hlásenie režimu znelo ako zmena, ktorú
    nikto neurobil.

    `--help` pritom vyhráva nad odmietnutím, hoci stojí na riadku až za ním:
    kto napísal preklep aj `--help`, chce zoznam volieb, a ten je lepšou
    odpoveďou na oboje.

17. **Slash príkaz sa v headless režime nevykoná — vykoná ho model.** Text
    `/foo bar` poslaný ako prompt CLI nerozvinie a nespracuje; príde modelu
    tak, ako bol napísaný, a ten sám siahne po nástroji `Skill`
    (`{"skill": "probe-noop", "args": "pokus"}`, `tool_result` „Launching
    skill: …", potom `user` záznam s rozvinutým textom príkazu). Odmerané
    6. 9. 2026 cez `tools/probe_slash.py` na dočasnom projektovom príkaze.

    Z toho plynú tri veci, a každá zlyháva ticho:

      • **Príkaz sa nemusí vykonať vôbec.** Rozhoduje sa model, nie CLI. Pri
        tom istom texte poslanom cez `--input-format stream-json` sa raz
        namiesto spustenia rozbehlo hľadanie súborov po disku. Príkaz teda nie
        je volanie funkcie, je to prosba — a appka to nesmie sľubovať inak.
      • **Lokálne príkazy terminálu (`/model`, `/clear`, `/context`) tam
        nefungujú vôbec.** V zozname od CLI sú (79 položiek, z toho 43
        skillov), ale nič ich neodlišuje: položka má len `name`,
        `description`, `argumentHint` a niekedy `aliases`. Appka ich preto
        nefiltruje — vymyslieť si delenie, ktoré protokol nehovorí, je horšie
        než ponúknuť všetko, čo CLI ponúklo.
      • Preto `ui::SessionPane::ShowCommands` príkaz **vloží do promptu**
        a neodošle. Odosiela sa všetko rovnako, Ctrl+Enter.

    Zoznam je **len z odpovede na `initialize`**. `system/init` nesie
    `slash_commands` tiež, ale ako holé mená — bez popisu a bez
    `argumentHint`, a navyše až so začiatkom ťahu. Nie je to teda druhý zdroj,
    je to chudobnejší; obsahom je vlastnou podmnožinou (`commands` ⊇
    `slash_commands` ⊇ `skills`). Odpoveď na handshake pritom **nechodí hneď**
    — v tomto projekte, kde `SessionStart` hook púšťa `bd prime`, prišla až po
    dvadsiatich sekundách — takže „zoznam ešte nie je" je normálny stav
    a musí sa **povedať**. Prázdny dialóg by znel ako „táto session nemá
    príkazy", čo je iné a nepravdivé tvrdenie.

    Otvára sa to **F4, nie Ctrl+/**, ako hovoril návrh. Znak „/" na slovenskej
    klávesnici nie je: `VK_OEM_2`, teda kláves, na ktorom leží na americkom
    rozložení, tam dáva „=" (odmerané `MapVirtualKeyW`). Chord pomenovaný podľa
    znaku by mal na tejto klávesnici nesprávne meno — je to tá istá pasca ako
    pri číslach v záložkách. Funkčný kláves je polohový, negeneruje `WM_CHAR`
    a stojí vedľa F2, ktorý otvára ten druhý dialóg.

18. **História sa z disku prehráva tými istými volaniami ako živý ťah, a
    `user` záznam nie je to, čo sa zdá.** Stream po `--resume` pošle len to,
    čo príde odteraz, takže predchádzajúce ťahy sa čítajú zo súboru, ktorý CLI
    vedie v `~/.claude/projects/<kľúč>/<id>.jsonl`. Švík vedie tade, kade
    vedie vedomosť: `proto::ReadSessionRecords` vie, **čo je v tom súbore**
    (prepustí `user` a `assistant`, teda práve tie typy, ktoré disk a stream
    zdieľajú, a zahodí `isSidechain` — rozhovor subagenta vložený medzi tieto
    by znel, akoby si Claude odpovedal sám); `proto::TranslateHistory` z nich
    robí udalosti portu tým istým prekladačom ako živá cesta a pridá
    `UserPrompt` a `Interrupted`; `model::RestoreHistory` z udalostí robí
    bloky tým istým `Transcript::Append` ako živá cesta — po jednej, lebo
    v jednej dávke by výsledok paralelného nástroja nemal za čo zapadnúť.
    Čokoľvek iné by bolo druhé vykreslenie tej istej konverzácie a tie dve by
    sa rozišli bez svedka. Číta to `proto::ClaudeBackend::Start` a odovzdá
    cez `onHistory`.

    Filtrovať sa **musí v `proto/`**, nie až v modeli: nezdieľané typy by inak
    prešli ako záznam neznámeho typu, a to je práve ten poplach, ktorý stráži
    soak. Zvonil by pre úplne bežný prípad.

    Jedna vec sa prehráva inak, a je to práve tá, kvôli ktorej to nie je cyklus
    nad `Transcript::Append`: **prompt je na disku text v `user` zázname, a
    `Append` z tých záznamov zámerne berie len výsledky nástrojov.** Živý
    prompt tam dáva `AppendUserPrompt` vo chvíli odoslania — takže ho tam dáva
    aj obnovenie, tými istými dverami, len zo záznamu namiesto z editačného
    poľa.

    A `user` záznam **nie je vždy prompt**. Odmerané 7. 9. 2026 nad 191 súbormi
    (35 229 záznamov `user`+`assistant`): stroj tam píše `<command-name>`,
    `<local-command-stdout>`, `<local-command-caveat>`, periodickú správu
    `## Context Usage` s `isMeta`, a dve značky v hranatých zátvorkách —
    `[Request interrupted by user…]` (65×) a `[Your previous response had no
    visible output…]`. Rozhoduje `proto::HumanPromptText`; z prerušenia
    vzniká `BlockKind::Interrupted`, teda ten istý blok, aký píše živá cesta
    pri Esc, lebo odpoveď useknutá uprostred sa inak neskôr číta ako odpoveď,
    ktorá skončila sama.

    **Text sa musí prečítať tomu, komu patrí.** `assistant` záznam má text na
    tom istom mieste ako `user`, takže si typ overuje `HumanPromptText` sám.
    Kým to nerobil, každá odpoveď pristála v prepise dvakrát — raz ako
    „claude:", raz ako „you:" — a vyzeralo to ako konverzácia, len nie ako tá,
    ktorá sa stala. Chytil to test, nie čítanie.

    Kurzor po prehratí stojí **na konci, nie na nule.** Rozhovor sa obnovuje
    preto, aby sa v ňom pokračovalo, takže kurzor patrí tam, odkiaľ sa
    pokračuje — inak je prvá vec, ktorú čitateľ musí urobiť, prejsť cez
    všetko, čo už raz prečítal. Za ním nič nezostáva schované: Ctrl+Home vedie
    na prvý prompt, a to je odpoveď na „ktorý rozhovor to je" (invariant 14).
    Invariant 3 tým porušený nie je — ten je o texte, ktorý *prichádza*, nie
    o mieste, kde okno začína.

    S kurzorom sa musí presunúť aj `anchor_`. „Kurzor je na konci" neprežije
    ani jedno pripísanie (invariant 11), takže bez neho by prvý ťah obnovenej
    session zamlčal celý svoj priebeh — a znelo by to ako pokazená reč, hoci
    by to bola pokazená odpoveď na „číta ešte?".

    Vkladá sa **jednou** úpravou, nie jednou na blok — prehratie
    pridáva výhradne za to, čo v buffri je (výsledok nástroja sa zakladá za
    svoje volanie a to volanie prišlo v tom istom prehratí), takže je to jeden
    súvislý vsuv na starom konci. Cena, odmeraná na najväčšom súbore korpusu
    (4,18 MB, 1351 záznamov): 80 ms čítanie a parsovanie, 38 ms prehratie,
    93 575 znakov prepisu.

19. **Klávesa, ktorú F1 nevymenúva, neexistuje.** Kláves pribúda a nie sú
    v žiadnej tabuľke — sedia v dvoch procedúrach okna (`PromptProc`,
    `TranscriptProc`), časť ako `WM_KEYDOWN`, časť ako `WM_CHAR`, a nedajú sa
    z kódu vymenovať ani generovaním, ani testom. Zoznam, ktorý skladá
    `KeysText` (`ui/keys_dialog.cpp`) z textov `kKeys…` v katalógu
    (`src/i18n/<jazyk>.def`), preto drží pravdivý jediná vec, a je to
    pravidlo, nie stroj: **kláves nie je hotový, kým nie je v tom zozname —
    v každom jazyku.** Katalóg vynúti, aby text mal každý jazyk; že je v ňom
    nová klávesa, nevynúti nič.

    Čo sa medzi agentmi líši, sa do zoznamu **neopisuje, ale skladá
    z `Capabilities`**: riadky Shift+Tabu sú režimy backendu v jeho poradí
    s jeho popisom, a F4 povie, že agent príkazy nemá, namiesto toho, aby
    zmizlo — klávesa, ktorá nič nerobí, stále odpovedá (invariant 6). Pevný
    zoznam režimov Claude by pri Codexe sľuboval režimy, ktoré neexistujú.

    Zlyháva to ticho a horšie než chýbajúci zoznam: podľa zoznamu, ktorý
    klame, sa prestane hľadať. Nový kláves, ktorý v ňom nie je, teda pre
    čitateľa nevznikol, a kláves, ktorý z appky zmizol, v ňom zostane sľubovať.

    Popisok promptu unesie práve jednu klávesu („Ctrl+Enter odošle") a viac
    nie — NVDA ho číta pri **každom** vstupe do poľa, takže druhá by sa počula
    pri každom návrate k písaniu kvôli veci, ktorá je potrebná raz. Preto
    dialóg, a preto F1. Menu rám od MDI má (invariant 24) a zoznam je v ňom
    ako Pomocník > Klávesové skratky; F1 je v akcelerátoroch rámu, nie
    v procedúrach polí, aby odpovedalo aj bez session (vtedy povie, že žiadna
    session nie je — zoznam závisí od `Capabilities` backendu). Nápoveda
    (`--help`) o F1 hovorí tiež, lebo je to jediné miesto, kde sa dá zoznam
    nájsť skôr, než okno vznikne.

    Klávesa sa v tom zozname píše ako **skratka, nie ako znak**: „T" a
    „Shift+T", nikdy „t" a „T". Čítačka povie obe veľkosti písmena rovnako,
    takže dvojica malé/veľké je nahlas jedno slovo dvakrát a riadok prestane
    niečo znamenať — zmizne práve to rozlíšenie, kvôli ktorému ten riadok
    existuje. `Navigate` pritom číta veľkosť **znaku**, nie stav Shiftu (to je
    to, čo mu dáva fungovať na ľubovoľnom rozložení), takže pri zapnutom Caps
    Locku sa tie dve prehodia; napísané je meno, ktoré má kláves po zvyšok
    času.

    Zoznam je jedno read-only viacriadkové pole, nie listbox: v listboxe by
    každý nadpis bol vyberateľná položka a nedalo by sa z neho nič označiť ani
    skopírovať. Text sa píše s `\n` ako všetko ostatné a do widgetu ho podáva
    `win::Dialog::SetTextLines` (invariant 4) — osamotené `\n` by obyčajné
    `EDIT` nakreslilo ako obdĺžnik a NVDA by prečítala celý zoznam ako jeden
    prázdny riadok.

    Písanie toho zoznamu je zároveň jediná kontrola, ktorá tu existuje, a hneď
    prvýkrát niečo našla: **Ctrl+Enter odosiela len z promptu** — v prepise je
    `VK_RETURN` zbalenie bloku bez ohľadu na Ctrl. Všetky ostatné klávesy sú
    v oboch poliach tie isté zámerne, takže je to odchýlka; je zapísaná
    (claude-gui-lkk.34) a v zozname priznaná.

20. **Režim povolení je stav procesu, mení ho Shift+Tab aj CLI samo a nikto si
    ho nepamätá.** Ekvivalent Shift+Tab z terminálu: `control_request` so
    `subtype: "set_permission_mode"` a holým `mode`. Odpoveď je
    `control_response` `success`, telo vnorené ako pri `initialize`
    (`response.response.mode`) a headless CLI v ňom mód ozve späť; iný hostiteľ
    smie potvrdiť prázdnym objektom. Odmerané `tools/probe_mode.py`
    (10. 9. 2026): po `plan` vrátil druhý `initialize`
    `current_permission_mode: "plan"`, takže sa zmena **usadí**, nie len
    potvrdí. Cyklus má **štyri** módy a je terminálový —
    `default → acceptEdits → plan → auto → default`; presne taký zoznam ozve aj
    hint Shift+Tabu v binárke CLI („default — ask before every edit / accept
    edits — edit freely, ask for commands / plan — research and propose, never
    touch files / auto — Claude decides what is safe"). `auto` cez stdio
    **funguje** (odmerané `tools/probe_mode.py`: `success`, druhý `initialize`
    vrátil `current_permission_mode: "auto"`). Piaty mód `bypassPermissions` sa
    za behu zapnúť **nedá** — `set_permission_mode` naň vráti `subtype: "error"`
    s prázdnym telom, jediná cesta je `--dangerously-skip-permissions` pri
    štarte. `dontAsk` sa prepnúť dá, ale v rotácii terminálu nie je.
    Mimocyklový mód začína nanovo na `acceptEdits`, nech klávesa vždy pohne
    (`agent::NextMode` nad zoznamom `proto::ClaudeCapabilities()`; test drží
    zhodu so starým `proto::NextPermissionMode` pre každý mód). **Neznámy**
    — teda zatiaľ žiadny — mód klávesa neprepne a povie to
    („režim zatiaľ nie je známy") — to je jediný prípad, keď cyklus nemá z čoho
    vyjsť a hádanie by poslalo mód, na ktorý čitateľ nestlačil.

    **Mód nemení len Shift+Tab — mení ho aj CLI samo, a hlási to.** Po
    schválení `ExitPlanMode` sa session vráti do režimu, **z ktorého do
    plánovania vošla** (`prePlanMode`), a na `default` padne len vtedy, keď
    taký nie je (štart rovno v `plan`) alebo keď je to `auto` a jeho brána je
    zavretá; `auto` zhodí brána aj za behu. Odmerané 11. 9. 2026
    `tools/probe_plan_exit.py` na troch cestách: štart v `plan` → `default`;
    `auto` → `plan` priamo → `auto`; `auto` → `default` → `acceptEdits` →
    `plan`, teda cestou Shift+Tabu → **`acceptEdits`**. Cyklus appky ide
    `default → acceptEdits → plan → auto`, takže kto štartuje v `auto` a do
    plánovania sa preklikne, po schválení **nepristane v `auto`**. Terminál to
    rieši voľbou priamo v dialógu `ExitPlanMode` („Yes, and use auto mode",
    „Yes, auto-accept edits", „Yes, manually approve edits"), čo sú
    `setMode` úpravy odoslané so schválením; náš dialóg ich nemá
    (claude-gui-lkk.43). O **každej** zmene, nech ju urobil ktokoľvek, pošle
    CLI
    `{"type":"system","subtype":"status","status":null,"permissionMode":"…"}`
    (v binárke `onPermissionModeChanged`; status prišiel na všetkých troch
    cestách v tej istej sekunde ako schválenie `ExitPlanMode`). Prvá verzia tento záznam nečítala a mód brala
    len z vlastných potvrdení, takže po `ExitPlanMode` hovoril stavový riadok
    „plánovanie" až do konca session a ďalší Shift+Tab išiel zo zlého miesta.
    Zlyhávalo to ticho a horšie než predtým, lebo hodnota zo `Session`
    prebíjala `system/init`, ktorý pravdu mal (claude-gui-lkk.41).

    **`proto::Session::permissionMode()` je jediný zdroj pravdy o živom móde**
    a pravidlá sú v `proto::PermissionModeTracker`, vytiahnuté von preto, aby sa
    dali testovať na záznamoch, nie na bežiacom CLI — všetky tri chyby prvej
    verzie boli v poradí, v akom veci prichádzajú:

      • **Semienko je mód z príkazového riadku**, nie prázdno. Odpoveď na
        `initialize` príde až po `SessionStart` hookoch (tu ~20 s) a dovtedy
        prvý Shift+Tab cykloval od prázdna, čiže z `plan` do `acceptEdits`.
      • **Hlásením je všetko, čo mód povie:** handshake, `system/status`,
        `system/init` aj ozvaný mód v odpovedi na **ktorýkoľvek** náš
        `set_permission_mode`.
      • **Shift+Tab posunie mód optimisticky**, aby druhé rýchle stlačenie
        cyklovalo z novej hodnoty. Kým na **najnovšiu** požiadavku nepríde
        odpoveď, hlásenia menia len „posledné slovo CLI", nie `current()` — CLI
        píše v poradí, takže hlásenie môže byť staršie než požiadavka.
      • **Odmietnutie sa vracia na posledné slovo CLI**, nie na mód pred
        stlačením. Pri dvoch stlačeniach mohlo byť odmietnuté aj prvé.
        Odpoveď na staršiu požiadavku `current()` neurovná vôbec — vrátila by
        mód, cez ktorý čitateľ už prestlačil.
      • **Odmietnutý mód sa pamätá do konca session a Shift+Tab ho preskočí**
        (`PermissionModeTracker::refused` → `Backend::refusedModes` →
        `agent::NextMode`). Inak z `plan` išla klávesa vždy na `auto`,
        odmietnutie vrátilo `plan` a z plánovania sa nedalo odísť
        (claude-gui-lkk.46). Len do konca session: to isté konto `auto` raz
        odmietlo a o pár hodín pustilo. Naisto ho odmietne
        `permissions.disableAutoMode: "disable"` v `--settings`
        (`error_code: "auto_mode_settings"`, `tools/probe_mode.py`).
      • **Odpoveď na Shift+Tab sa ohlási ako `ModeChanged` vždy**, aj keď sa
        mód rovná naposledy ohlásenému (`IsModeAnswer` v
        `ClaudeBackend::OnRecord`). Panel sa na klávese posunul sám, takže
        odmietnutie vracia presne ten mód, ktorý backend ohlásil naposledy —
        a porovnanie bez tejto výnimky nechalo panel na odmietnutom.

    Mód sa **ohlasuje synchrónne na klávese** (`CyclePermissionMode`), nie až na
    potvrdení: podľa invariantu 7 smie prerušiť reč len odozva na klávesu.
    Zmenu, ktorú panel neurobil sám — odmietnutý Shift+Tab, `ExitPlanMode`,
    zhodené `auto` — ohlási `proto::ClaudeBackend` ako `agent::ModeChanged`
    (porovná `Session::permissionMode()` po každom zázname) a panel ju raz za
    dávku preberie v `SessionPane::FollowPermissionMode`:
    prepíše stavový riadok a povie tú istú vetu ako klávesa, **do fronty**
    a len v popredí (invariant 11). Nie je to priebežný komentár, ale oprava
    faktu, ktorý čitateľ drží — posledné, čo počul, bol mód, ktorý už neplatí.
    Mód z handshaku, ktorý sa objaví tam, kde žiadny známy nebol, zmena nie je
    a nehovorí sa. Po schválení `ExitPlanMode` veta **zaznie za zavretým
    dialógom**, teda tam, kde sa podľa invariantu 6 hovoriť nedá: v teste cez
    NVDA MCP (11. 9. 2026) odišla 34 ms pred ohlásením titulku okna, ktoré ju
    naživo zruší. Nechané tak zámerne — odchod z plánovania je čitateľov vlastný
    úkon práve v tom dialógu, a mód drží stavový riadok. F2 si mód z handshaku
    **neberie** — bol to snímok zo štartu a po Shift+Tabe ukazoval mód, z ktorého
    session už odišla.

    Stavový riadok aj reč hovoria **slovenský názov**
    (`agent::Mode::label`, pre Claude v `proto::ClaudeCapabilities()` — názov
    módu pozná adaptér, nie panel); dialóg F2 drží surové slovo CLI zámerne
    (`session_details.cpp`). Prežitie módu medzi spusteniami je samostatná
    práca (claude-gui-lkk.6.1 notes) — CLI ho headless behu neuchová.

21. **Model v stavovom riadku je ten, ktorý odpovedá — nie ten zo
    `system/init`.** `system/init.model` je „hlavný model" session, a pri
    aliase, ktorý závisí od módu, to nie je model, ktorý píše. `opusplan` sa
    preloží na Sonnet, a Opus dostane len požiadavka odoslaná v móde `plan`
    (v binárke `Vl()` → `mainLoopModel` pre `system/init`, `runtimeModel` pre
    API). Odmerané 11. 9. 2026 na jednom procese: `system/init`
    `{model: claude-sonnet-5, permissionMode: plan}`, všetky záznamy
    `assistant` toho ťahu `claude-opus-5`. Používateľ z toho usúdil, že režim
    plánovania je ignorovaný — bar mu povedal Sonnet, kým písal Opus
    (claude-gui-lkk.40).

    A vyberá sa **na každú požiadavku, nie na ťah**: po schválení
    `ExitPlanMode` ten istý ťah pokračoval na `claude-sonnet-5`
    (`tools/probe_plan_exit.py`). Jediný svedok, ktorý to stíha, je
    `message.model` záznamu `assistant`, a číta ho `proto::ParseAnsweringModel`.
    Vynecháva záznam subagenta (`parent_tool_use_id` vyplnené — môže bežať na
    inom modeli a session ním nie je) a náhradné správy CLI (`<synthetic>`).
    Záznam z disku `parent_tool_use_id` nemá vôbec a počíta sa, lebo
    sidechainy zahodil už `ReadSessionRecords`.

    Poradie zdrojov je poradie čerstvosti: `--model` zo spustenia → pri
    `--resume` posledný model z histórie → `system/init` → `assistant`. Keď raz
    prehovoril `assistant`, `system/init` už model **neprepisuje**
    (`proto::Translator`, ktorý to pravidlo drží a posiela výsledok ako
    `agent::ModelChanged`; testuje ho `TestTranslatorModelAndTurn`): chodí na
    začiatku každého ťahu a pri `opusplan` v `plan` by vrátil Sonnet na celé
    premýšľanie pred prvou správou. `ParseUsage` dostáva ten istý model —
    model, ktorý drží prekladač —, takže `contextWindow` patrí
    modelu, ktorý beží. F2 k modelu pripíše „zvolený opusplan", keď zvolené
    meno v modeli nie je — je to jediné miesto, ktoré povie, že sa model mimo
    `plan` zmení.

22. **Nastavenie mení len výslovný úkon, nie vedľajší účinok — a zlý riadok
    sa odmietne.** `%APPDATA%\AgentAloud\settings.txt`, alebo `config\settings.txt`
    vedľa `.exe`, keď ten priečinok existuje (prenosný režim; appka ho nikdy
    nevytvára, lebo zapnúť sa má vedome). Kľúče sú anglické a ploché,
    s backendom ako prefixom: `backend`, `claude.permission-mode`,
    `codex.model`. Prefix nie je kozmetika — slová režimov sa medzi CLI líšia
    (claude-gui-lkk.55): Claude nemá `read-only`, Codex nemá `acceptEdits`.
    `auto` kedysi znamenalo u každého niečo iné; od claude-gui-lkk.44.15 je
    to pri oboch „o povolení rozhoduje model" (pri Codexe `approvalsReviewer:
    auto_review`, sandbox platí ďalej) a bežný režim Codexu sa volá
    `default`.

    **Shift+Tab sa neukladá.** Mení bežiacu session a nič viac. Režim
    zapamätaný za čitateľovým chrbtom by preniesol jedno stlačenie navyše —
    `plan` — do každej ďalšej session, ticho. Zapisovať sa do súboru bude —
    dialóg nastavení (po lokalizácii, claude-gui-lkk.52) a stav aktualizácií
    (claude-gui-lkk.53) —, ale vždy tak, že **riadky, ktoré zápis nemení,
    zostanú, ako boli**, vrátane poznámok a poradia: súbor sa dá písať aj
    ručne a zápis, ktorý by ho prepísal celý, by ticho zmazal, čo doň niekto
    napísal. Robí to `Settings::Set` + `Serialize` (súbor bez zmeny vyjde
    bajt po bajte, vrátane BOM a CRLF) a `win::WriteFileBytes` (najprv
    `.tmp`, potom jedno premenovanie). Dnes appka zapisuje len stav
    aktualizácií (invariant 23).

    Prednosť je **príkazový riadok > súbor > zabudovaná hodnota**
    (`app::ApplySettings` medzi `Parse` a `CheckBackend`) a režim aj model sa
    berú pre backend, ktorý naozaj pobeží. Hodnota zo súboru potom prejde tými
    istými kontrolami, ako keby bola napísaná.

    **Zlý riadok sa nepreskočí, odmietne sa** — neznámy kľúč (aj s prázdnou
    hodnotou), režim, ktorý backend toho riadku nepozná, kľúč dvakrát.
    Preskočený preklep vyzerá presne ako nastavenie, ktoré nikto neurobil, a
    čitateľ sa potom pýta, prečo session beží v zlom režime. Odmietnutie ide
    tou istou cestou ako neznáma voľba (invariant 16): celá cesta k súboru,
    číslo riadku, kód 2. Kontrolujú sa riadky **všetkých** backendov, nielen
    zvoleného, inak by preklep v `codex.` čakal na deň, keď sa Codex použije.
    Chýbajúci súbor nie je chyba — prvé spustenie žiadny nemá.

    **`claude.permission-mode` prebije projektový `permissions.defaultMode`**
    z `.claude/settings.json` aj `settings.local.json`, lebo appka ho posiela
    ako `--permission-mode` a voľba vyhráva (odmerané
    `tools/probe_mode_precedence.py`, CLI 2.1.288, 5. 10. 2026). Prijaté
    vedome; nastavenia podľa projektu sú claude-gui-lkk.56. A pozor pri
    meraní: s `--model haiku` sa `auto` ticho zmení na `default`, aj bez
    akýchkoľvek nastavení.

23. **Aktualizuje sa celý balík, nie holé EXE, a nič sa nevymení, kým nie je
    všetko overené.** Kto si balík stiahol raz a potom už len aktualizuje,
    musí skončiť s tým istým priečinkom ako ten, kto ho stiahne dnes. Holé
    EXE by nechalo vedľa seba DLL z prvého stiahnutia — a knižnica čítačky
    v zlej verzii zlyhá ticho (claude-gui-lkk.53, rozhodnutie autora).

    Kontrola je **pri štarte, pred výberom priečinka a pred session**
    (`UpdateBeforeStart` v `main.cpp`): proces CLI ešte nebeží, takže
    reštart nič nestráca a nikto sa nepýta na priečinok dvakrát. Raz denne
    (`last-update-check`), so stropom 3 s aj na DNS, ktoré WinHTTP samo
    neobmedzí; deň sa zapíše len vtedy, keď prišla odpoveď. `check-updates=0`
    ju vypne.

    **Ručná kontrola interval nečíta ani nezapisuje a odpovie vždy**
    (claude-gui-lkk.65; konvencia Sparkle, Notepad++, VS Code). Volá ju
    `--check-updates` pri štarte namiesto dennej kontroly a Pomocník >
    Skontrolovať aktualizácie za behu (`CheckForUpdatesAsked` v `main.cpp`,
    pravidlo `update::AnswerAsked`). Ignoruje preskočenú verziu a povie aj
    „máte najnovšiu", „vydanie zatiaľ nie je", chybu a vývojovú zostavu —
    vývojová (`0.0.0-…`, `…-N-g…`, `-dirty`) ponuku nedostane nikdy, takže
    ponuka sa skúša len na stiahnutom vydaní. Z menu bežia sessions, preto sa
    po inštalácii nereštartuje, len povie, že nová verzia nabehne pri ďalšom
    štarte.

    Poradie je pevné a každý krok môže zastaviť všetko ďalšie:
    `releases/latest` (značka z presmerovania, bez API), stiahnutie
    `AgentAloud.zip` a `SHA256SUMS.txt`, **súčet celého ZIP-u**, rozbalenie do
    čerstvého `.update-new\` vedľa EXE, plán (`update::PlanReplace`: odreže
    vrchný priečinok `AgentAloud-<verzia>/`, vynechá `config\`, celý balík
    odmietne pri absolútnej ceste, `..`, `:` alebo bez EXE v koreni), a až
    potom výmena: starý súbor do `.update-old\`, nový na jeho miesto. Bežiace
    EXE aj načítaná DLL sa v rámci zväzku presunúť dajú, prepísať nie. Keď
    zlyhá ktorýkoľvek presun, vrátia sa **všetky** — priečinok skončí, ako
    bol. Oba pomocné priečinky zmaže ďalší štart (`RemoveLeftover`); prípona
    `.old` zo starších návrhov by mohla zmazať cudzí súbor s tým menom.

    **Rozbaľuje systémový `tar.exe` a pravidlá jeho použitia sú tri,**
    všetky odmerané (sonda 5. 10. 2026, bsdtar 3.8.8):

      • **Plnou cestou zo `GetSystemDirectoryW`**, nie z PATH — `tar` z msys
        alebo Gitu je iný program.
      • **Žiadna cesta na príkazovom riadku.** tar vidí len ANSI kódovú
        stránku; cesta so znakmi mimo nej (azbuka na stredoeurópskom stroji)
        sa zmení na `??` a zlyhá. Cieľ preto ide ako pracovný priečinok
        procesu (`lpCurrentDirectory`, Unicode) a ZIP leží v ňom pod menom
        `package.zip`.
      • **Rozhoduje len návratový kód.** Orezaný ZIP nechal v cieli súbor
        správnej veľkosti a zlého obsahu, s kódom 1. Preto čerstvý priečinok
        zakaždým a nič z neho sa nepoužije, kým tar nepovedal 0.

    Novú verziu treba spustiť, **kým je dialóg sťahovania otvorený**:
    Windows pustí okno dopredu len procesu, ktorý mal fokus, a po zavretí
    dialógu tento proces žiadne okno nemá. Preto `Finish` beží na vlákne
    dialógu, nie sťahovania — a preto aj Zrušiť počas sťahovania nemôže
    súťažiť s výmenou, ktorá už beží.

    `TaskDialogIndirect` je len v Common Controls 6, a **bez manifestu sa
    EXE, ktoré ho importuje, vôbec nenačíta** — z Prieskumníka bez slova.
    Manifest je `src/ui/app.manifest`, vložený cez `app.rc`. Každý ďalší
    program, ktorý linkuje `updater.o` (sonda, test), ho potrebuje tiež.

    **Manifest zmenil kreslenie všetkých ovládacích prvkov, nielen dialógu**,
    a stavový riadok to ticho rozbilo. Prázdnu časť stavového riadku číta NVDA
    podľa toho, čo tam naposledy videla nakreslené (`StaticText` → `displayText`).
    Klasický bar časť zmaže cez `FillRect`, ktorý NVDA sleduje; s témou zostal
    v jej pamäti starý text a NVDA+End hovorilo „pracujem" aj po ťahu, hoci
    `WM_GETTEXT` vracal prázdne pole (5. 10. 2026). Preto má stavový riadok
    tému vypnutú (`SetWindowTheme` v `StatusBar::Create`). Prázdne pole po ťahu
    je zámer — podľa toho, že NVDA+End začne modelom, sa pozná koniec práce.

    Repozitár bez vydania — alebo súkromný — vráti na `releases/latest` 404,
    čo appka berie ako „nie je vydanie" a mlčí. Naostro sa to dá overiť až
    s dvoma vydaniami.

24. **Viac sessions je MDI, a session nevie, čo ju hostí.** Rám
    (`ui::MainWindow`) má menu Súbor a Okno, MDICLIENT a jeden stavový
    riadok; každá session je MDI child (`ui::SessionWindow`) s jedným
    `SessionPane` (claude-gui-lkk.7.9). MDI a nie taby:
    Ctrl+Tab, Ctrl+F4 a zoznam okien v menu čitateľ pozná a NVDA ich číta
    dobre. Panel sa ďalej nepýta na rodiča; čo potrebuje vedieť, mu povie
    hostiteľ — `SetActive` (reč, invariant 11) a `SetStatusBar` (bar dostane
    len aktívna session a panel mu pri tom odovzdá všetky štyri polia, ktoré
    si drží sám, aj prázdne).

    Štyri veci na tom zlyhávajú ticho:

      • **Child bez `WS_MAXIMIZEBOX` sa nedá maximalizovať.** Prvý vznikne
        maximalizovaný (`WS_MAXIMIZE`), ale Ctrl+Tab, Ctrl+F4 aj
        `WM_MDIMAXIMIZE` nechajú ďalší obnovený a rám stratí cestu
        z titulku. Odmerané 7. 10. 2026; s tým bitom prenáša maximalizáciu
        MDI klient sám.
      • **Holý MDI child NVDA neohlási.** Pri zmene fokusu hovorí
        kontajnery, do ktorých fokus vošiel, ale klientsku oblasť obyčajného
        okna len vtedy, keď má okno `WS_SYSMENU` (`isPresentableFocusAncestor`
        v `NVDAObjects/IAccessible`) — a ten MDI klient maximalizovanému
        childu **berie** (odmerané). Ctrl+Tab aj Ctrl+F4 preto povedali len
        „prompt" a do ktorej session sa prešlo, nebolo počuť (7. 10. 2026,
        používaním). `SessionWindow::Annotate` preto dá klientskej oblasti
        cez Dynamic Annotation (`IAccPropServices`) rolu zoskupenia a meno
        priečinka; zoskupenie s menom NVDA hovorí vždy. `WS_SYSMENU` child
        nemá: v menu bare by pribudla ikona bez mena pred „Súbor" a tri
        tlačidlá, a NVDA by to aj tak nepomohlo. Overiť naostro sa to dá
        `AccessibleObjectFromWindow(OBJID_CLIENT)`; UI Automation okno
        a klienta zlúči a anotáciu neukáže.
      • **Klávesy rámu sú v jeho tabuľke akcelerátorov, nie
        v `TranslateMDISysAccel`.** Ten chodí cez systémové menu childu,
        ktoré nie je. Ctrl+N, Ctrl+Tab/Ctrl+F6 (aj so Shiftom) a Ctrl+F4
        chytá `TranslateAcceleratorW` v `win::RunMdiMessageLoop`, teda skôr,
        než ich uvidí prompt — Ctrl+Tab predtým prompt bral ako Tab.
      • **Session z menu nepokračuje v rozhovore.** `--resume`, `-c` aj
        slová za `--` platia len pre prvú: druhé použitie toho istého id CLI
        odmietne (invariant 14). Backend a priečinok dialóg predvyplní
        naposledy zvolenými.

    **Session sa volá podľa priečinka, a dve v tom istom priečinku dostanú
    číslo** — „cesta (2)" v titulku (teda aj v ráme a v menu Okno), v mene
    z anotácie aj v poli projektu stavového riadku. Číslo má len priečinok
    s viac než jednou session a ide 1..n v poradí otvorenia **bez dier**:
    zatvorenie druhej z troch spraví z tretej druhú, posledná zostávajúca
    číslo stratí. Číslo teda hovorí „ktorá z tých v tomto priečinku", nie
    „koľkú som otvoril". Pravidlo je `app::SessionOrdinals`, prečísluje
    `MainWindow::Renumber` po každom otvorení a zatvorení, a priečinok sa
    porovnáva bez ohľadu na veľkosť písmen a lomky (`SessionWindow::FolderKey`).

    Klávesa rámu, ktorá nemá čo urobiť (Ctrl+Tab pri jednej session, čokoľvek
    bez session), to povie vlastnou `Speech` rámu — panel, cez ktorý by
    hovoril, nemusí existovať. Zatvorením poslednej session appka nekončí;
    rám zostane prázdny so zmazaným stavovým riadkom, a ten treba zmazať, inak
    by NVDA+End čítal fakty session, ktorá už nebeží.

25. **Proces CLI, ktorý skončil sám, je blok v prepise, a jeho stderr je
    obsah toho bloku.** CLI hovorí na stderr presne to, čo čitateľ potrebuje,
    keď session nejde — odmietnutú voľbu, obsadené id, holé `--resume`
    (invarianty 14 a 16) — a appka s `-mwindows` vlastný stderr nemá. Kým ho
    `win::Process` dieťaťu podával (claude-gui-lkk.49), session sa spustila
    a mlčala: prázdny prepis, prázdny stavový riadok a nič, z čoho by sa
    dalo zistiť prečo.

    `win::Process` má teraz rúru aj na stderr a **vlastné čítacie vlákno, a to
    nie je kozmetika**: nečítaná rúra sa zaplní a dieťa sa zasekne na zápise,
    a CLI tam píše varovania aj v bežnej session. Drží sa hlava (4 kB) a chvost
    (12 kB) — hláška pri odmietnutí je na začiatku, posledné slová pádu na
    konci. Koniec procesu hlási `onExit` až po EOF na stdout, teda za
    posledným záznamom, a na zvyšok stderr počká najviac sekundu: vnuk môže
    rúru držať aj po smrti dieťaťa.

    Adaptér z toho spraví `agent::SessionEnded` — **ale nie po vlastnom
    `Stop()`**; ten si nastaví `stopping_` skôr, než čokoľvek zavrie. A uvoľní
    `turnInFlight_`: ťah, v ktorom proces zomrel, svoj `result` nedostane
    a `Stop()` by pri zatvorení okna čakal celý timeout.

    Blok (`BlockKind::SessionEnded`) je obsah, nie mechanizmus, takže sa
    nezbalí; prvý riadok je náš („Agent skončil s kódom 1.", NTSTATUS hexa),
    za ním text CLI cez `Widen` ako všetko z rúr, a prázdny stderr sa povie
    vetou — inak by blok vyzeral useknutý. `isError`, aby ho našlo E. Panel
    v popredí povie prvý riadok **bez ohľadu na kurzor** (invariant 11 tu
    neplatí, je to fakt, ktorý mení, čo urobí každá ďalšia klávesa), na
    pozadí zvuk konca ťahu. Stavový riadok povie „agent nebeží" — prázdne
    pole by znamenalo „hotovo, pýtaj sa" — a Ctrl+Enter odmietne s odkazom
    na Ctrl+N. Session sa na mieste nereštartuje.

    Overené naostro bez kreditu (8. 10. 2026): `agentaloud <priečinok> --
    --fafa` dá blok „Agent skončil s kódom 1. / error: unknown option
    '--fafa'" a zavretie okna trvá 0,2 s.

    **Proces, ktorý nevznikol vôbec, blok nemá — nemá kam.** Session bez
    procesu sa nespustí, okno zanikne, a dôvod ide von ako veta pod
    „Nepodarilo sa spustiť session." (claude-gui-lkk.17): backend vráti
    `agent::StartFailure` (program a `GetLastError`), panel z neho spraví
    vetu — chýbajúci program na PATH vlastnou, čokoľvek iné slovami systému
    s číslom. Hranica je **vznik procesu**: `Start` oboch adaptérov po ňom
    vracia `true` aj vtedy, keď zlyhá prvý zápis do kanála, lebo také dieťa
    je mŕtve a jeho koniec povie viac než `GetLastError` nula. Priečinok
    z príkazového riadka sa overuje už v `main.cpp` a odmieta ako zlá voľba
    (kód 2); dialóg novej session overuje svoj. Hláška ide na konzolu len
    vtedy, keď sa priečinok písal — po dialógu sa čitateľ pozerá na okná.

26. **Pri úlohách na pozadí odpovie Claude `success` aj vtedy, keď nič
    nespravil — čo sa stalo, hovoria len záznamy pred odpoveďou.** Namerané
    sondou (`tools/probe_subagents.py`, scenáre C1, fg-bash, bg-bash,
    9. 10. 2026) a odhalené až overovaním naostro, nie testmi:

    - **`interrupt` mimo ťahu zastaví subagentov, nie príkazy.** Príkaz Bash
      na pozadí beží ďalej a odpoveď je tá istá. Prvá verzia dvojitého Esc
      preto povedala „zastavujem" a stavový riadok na tom ostal visieť.
      `Session::StopAllTasks` pošle `interrupt` (subagenti, bez samovoľného
      ťahu) **a** `stop_task` na každý príkaz z posledného
      `background_tasks_changed` (`ShellTaskIds`, `shellTasks_`). Zastavený
      príkaz samovoľný ťah nevyvolá, zastavený subagent áno — preto nie
      `stop_task` na všetko.
    - **Ctrl+B skôr než ~8 s po spustení príkazu nespraví nič.** Príkaz sa
      stane úlohou (`task_started`) až tak neskoro; dovtedy je
      `background_tasks` bez účinku. O presune hovorí len `system/task_updated`
      s `{is_backgrounded:true}` pred odpoveďou. Translator ho spáruje
      s odpoveďou na žiadosť s predponou `bg-` (`kBackgroundRequestPrefix`)
      a pošle `agent::BackgroundMoved{moved}`; panel povie „presunuté do
      pozadia" alebo „zatiaľ nie je čo presunúť". Kláves sám povie
      „presúvam do pozadia" (invariant 6, dvakrát).
    - **Codex: prerušenie ťahu nezastaví nič, čo beží vedľa neho**
      (`tools/probe_codex_subagents.notes.md`, spawn-int, spawn-kill,
      bgterm-int; naostro 9. 10. 2026, claude-gui-b8n.11). `turn/interrupt`
      hlavného vlákna subagentov nezastaví a `turn/interrupt` na vlákno
      subagenta nezastaví jeho príkaz — `ping` bežal ďalej, kým ho nezabil
      job object. `CodexBackend::StopTask` subagenta preto pošle
      `turn/interrupt` **a** `thread/backgroundTerminals/clean` na jeho
      vlákno, príkaz `terminate` podľa `processId`; `StopAllTasks` preruší
      každý ťah a vyčistí terminály každého známeho vlákna. Ctrl+B Codex
      nemá (`backgroundNow` false) — subagent beží vždy vedľa ťahu.

    Potvrdenie zastavenia všetkého je **druhé Esc, nie dialóg**: za zavretým
    dialógom NVDA vetu zahluší (invariant 6) a zastavenie trvá menej než
    desatinu sekundy. Zavretie session (Ctrl+F4) alebo appky (Alt+F4) pri
    bežiacich úlohách sa naopak pýta `MessageBox` s **OK/Zrušiť** — Áno/Nie
    by nepustilo Esc — lebo job object ich zabije bez opýtania (invariant
    10); Alt+F4 jednou otázkou za všetky sessions, ktoré sa potom už len
    `ShutDown()`. Veta „zastavené: …" z Ctrl+T sa hovorí aj vtedy, keď je
    v popredí dialóg úloh, nie rám — `InForeground()` na ňu povie nie.

Zhodu modelu s widgetom nedá overiť žiadny unit test, tak ju appka kontroluje
za behu: po každej úprave porovná dĺžku bufferu s `EM_GETTEXTLENGTHEX`. Keď sa
rozídu, titulok tej session sa zmení na **„<cesta> — NESÚLAD MAPY ROZSAHOV"**
(po anglicky „RANGE MAP MISMATCH") a rám ho ukáže v zátvorke za svojím menom,
kým je tá session aktívna. Ak
to niekedy uvidíš, neladí invariant 3, 4 alebo 8 a navigácia bude zameriavať
zle. Raz sa to už stalo (6. 9. 2026) a bol to `NUL` z binárky — hľadaj teda
najprv znak, ktorý widget spočíta inak než model, a hľadaj ho v poslednom
výstupe nástroja pred tým, než sa titulok zmenil. Nájsť sa to dá aj po
skončení: session je na disku, a čo hľadať, ukazuje soak nad korpusom, ktorý
odvtedy riadiace znaky v prepise hlási.

Titulok je pritom **jediné**, čo appka spraví — zotaviť sa z toho zatiaľ nevie
(claude-gui-lkk.31), takže session, ktorá to raz ohlási, má navigáciu zlú až do
zatvorenia.

Bez `--permission-prompt-tool stdio` sa z pravidla „ask" stane „deny" a nikto
sa nás na nič nespýta. Prepínač je z `--help` vypadnutý, ale CLI ho prijíma.

