# probe_midturn — prompt počas bežiaceho ťahu (2026-10-20)

CLI 2.1.302, `--model haiku`, argumenty ako appka, prázdny dočasný priečinok,
`can_use_tool` → allow. Spustenie:
`python -I tools/probe_midturn.py <dir> mid|mid-text|interrupt [--replay] [--two]`
(`--replay` pridá `--replay-user-messages`, `--two` pošle 0,5 s po prompte 2
ešte prompt 3 "Also reply with the word CHERRY").
Surový záznam v `<dir>/raw.jsonl` (mimo repozitára).

Stav: HOTOVO, všetky tri scenáre prebehli bez aj s `--replay`, plus
`mid --replay --two`; disk skontrolovaný. Každý variant jeden beh.

## S `--replay-user-messages` (všetky tri scenáre + mid --two)
Tvar echa: `{"type":"user","message":{role,content},"isReplay":true,
"parent_tool_use_id":null,"session_id","timestamp","uuid"}` — rovnaký pre
prompt vstrebaný aj zaradený ako nový ťah; **žiadne pole ani system záznam
ich nerozlišuje**. `timestamp` = čas prijatia (zhodný s diskovým enqueue),
`uuid` = `source_uuid`/`commandUuid` z diskového `queued_command`/`remove`.

- **Prompt 1 sa ozve tiež**, ale nie hneď: tesne pred prvým `assistant`
  záznamom (po system/init a po prvom API volaní, ~2 s po init).
- **Prompt 2 sa ozve až keď ho CLI použije, nie pri prijatí:**
  - mid: prijatý 6.6, echo 16.4 — hneď za `user [tool_result]`, pred
    `assistant` toho istého ťahu (vstrebanie). 1 result.
  - mid-text: prijatý 5.0, echo 21.9 — po `result` ťahu 1 a po novom
    `system/init`, pred `assistant` ťahu 2. 2 results.
  - interrupt: prijatý 6.1, echo 10.0 — po `result/error_during_execution`
    a novom `system/init`. 2 results.
  - Rozlíšenie teda len podľa polohy: echo pred `result` (bez init medzi) =
    vstrebaný; echo po novom `system/init` = vlastný ťah.
- **mid --two:** prompty 2 aj 3 vstrebané **spolu** do toho istého API
  volania: za tool_result dve echá za sebou (14.6, 14.6), model odpovie
  „BANANA a CHERRY" v jednom texte, 1 result. Disk: 2× enqueue,
  2× `queued_command`, 2× `remove absorbed_mid_turn`.
- **Vedľajší účinok:** `--replay-user-messages` ozve aj klientovu
  `control_response` na `can_use_tool` (allow s `updatedInput`, `request_id`
  = id žiadosti od CLI) ako `type: control_response` na stdout. Bez prepínača
  to neprišlo. Appka by ju nesmela brať ako odpoveď na vlastnú žiadosť.

## interrupt — ako mid, interrupt 1 s po prompte 2
2.6 system/init → 5.0 assistant [tool_use] → 5.2 can_use_tool → **7.0 >>>
prompt 2** → **8.0 >>> interrupt** → 8.0 control_response success →
11.7 user [tool_result "The user doesn't want to proceed…",
`tool_result_meta.non_execution_kind: "user-rejected"`] → 11.7 user [text
"[Request interrupted by user for tool use]"] → 11.8
result/error_during_execution is_error=true → **12.0 system/init** →
14.1 assistant [text "BANANA"] → 14.1 result/success.

- **2 result, 2 system/init. Prompt 2 sa interruptom nezahodí**: ťah 1 skončí
  chybou a prompt 2 hneď spustí vlastný ťah, bez zásahu klienta.
- Medzi odpoveďou na interrupt a `result` ťahu 1 prešlo 3,7 s (ping
  v popredí dobiehal / sa zabíjal).
- Echo promptu 2 na stdout: nie. Na disku enqueue (pri poslaní) a dequeue
  (pri začatí ťahu 2), prompt 2 ako obyčajný `user` záznam.

## Zhrnutie pre appku
- Prompt počas ťahu je bezpečné poslať; CLI ho zaradí do fronty. Či z neho
  bude **samostatný ťah** (ďalší init + result), alebo sa **vstrebe** do
  bežiaceho (žiadny ďalší result), rozhoduje CLI podľa toho, či ťah ešte
  robí ďalšie API volanie (po výsledku nástroja áno). Klient to zo stdout
  vopred nevie; počítanie "1 prompt = 1 result" neplatí.
- Rozlíšiť sa to dá len dodatočne: ďalší `system/init` po `result` = prompt
  bol vo fronte; žiadny = vstrebaný (resp. odpoveď je v texte ťahu 1).

## Neoverené
- Či sa vstrebanie deje vždy, keď prompt príde pred ďalším API volaním
  (jeden beh na scenár). Viac promptov vo fronte bez nástroja (mid-text
  s dvoma) — jeden ťah, alebo dva?
- Interrupt v čase, keď je prompt vstrebaný, ale ťah ešte beží.
- Bez `--include-partial-messages` overené len to, že stdout pri vstrebaní
  bez `--replay-user-messages` neposiela nič, čo by prompt označilo.

## mid-text — prompt 2 počas generovania textu (esej, bez nástroja)
1.9 control_response → system/init → **3.6 >>> prompt 2** → 8.6 assistant
[thinking] → 16.0 assistant [text 4084 znakov, bez BANANA] →
16.0 result/success num_turns=1 → **system/init** → 18.1 assistant [thinking]
→ 18.3 assistant [text "BANANA"] → 18.3 result/success num_turns=1.

- **2 result, 2 system/init.** Bez nástroja nie je v ťahu ďalšie API volanie,
  kam by sa prompt vstrebal, takže čaká vo fronte a po prvom `result` začne
  samostatný ťah (s vlastným init) hneď, bez ďalšieho zásahu klienta.
- Echo na stdout: nie.
- Disk: enqueue/dequeue (bez content); dequeue až 12,5 s po enqueue, keď
  ťah 1 skončil. Prompt 2 je na disku ako obyčajný `user` záznam (2 `user`
  záznamy spolu), žiadny `queued_command` ani `remove`.

## mid — prompt 2 počas Bash v popredí (ping -n 8)
Časy od štartu procesu:
3.3 control_response (initialize) → 3.8 system/init → 5.6 assistant [thinking]
→ 5.9 assistant [tool_use] → 6.0 can_use_tool → **7.9 >>> prompt 2** →
13.7 system/task_started (Bash v popredí dostane task po ~8 s behu) →
18.1 system/task_notification completed → 18.5 user [tool_result] →
20.2 assistant [thinking] → assistant [text "BANANA\n\nZ 8 … 8 odpovede."] →
result/success num_turns=2, result obsahuje BANANA.

- **1 result, 1 system/init.** Prompt 2 nezačal nový ťah, vstrebal sa do
  bežiaceho: model ho dostal v ďalšom API volaní toho istého ťahu (za
  tool_result) a odpovedal na oba naraz.
- Bez `--replay-user-messages` sa prompt 2 na stdout **neozve** ako `user`
  záznam (žiadny isReplay, nič).
  Žiadny queue-related záznam na stdout; jediný `user` je tool_result.
- Na disku (session jsonl) — tvar:
  - `queue-operation` `operation: enqueue` (kľúče len type, operation,
    timestamp, sessionId — **bez content**), hneď po assistant [tool_use].
    (Prvý prompt má tiež enqueue + dequeue 2 ms po sebe na začiatku súboru.)
  - po tool_result: `attachment` typu `queued_command` (`prompt` = pole
    content častí, `source_uuid`, `delivery_id`, `commandMode: "prompt"`,
    `timestamp`; na zázname aj `rendered`, `renderedRole`) →
    `queue-operation` `operation: remove`, `content` = text promptu,
    `reason: "absorbed_mid_turn"`, `commandUuid`, `deliveryId`.
  - **Žiadny `user` záznam s textom promptu 2** na disku nie je; modelu ide
    ako system-reminder: "The user sent a new message while you were
    working:\n<text>\n\nThis is how Claude Code surfaces messages the user
    sends mid-turn — within the running turn, often alongside the next tool
    result, rather than as a separate conversation turn. …".
  - Dôsledok pre históriu po `--resume`: `ReadSessionRecords` (len
    user/assistant) prompt 2 neuvidí.
