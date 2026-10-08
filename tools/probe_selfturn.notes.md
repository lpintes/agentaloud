# probe_selfturn — samovolný ťah (2026-10-08)

CLI 2.1.293, `--model haiku`, argumenty ako appka (`-p --verbose
--input-format stream-json --output-format stream-json
--permission-prompt-tool stdio`), prázdny dočasný priečinok, `can_use_tool`
→ allow. Spustenie: `python -I tools/probe_selfturn.py <dir> plain|interrupt|prompt2 [s]`.

Stav: HOTOVO, všetky tri scenáre prebehli (plain, interrupt, prompt2).

## Normálny ťah (prompt → result)
hook_started, hook_response (SessionStart hooky stroja, len raz na začiatku
procesu) → control_response (initialize) → system/init → system/thinking_tokens ×0–2
→ assistant [thinking]? → assistant [tool_use] → rate_limit_event →
control_request can_use_tool → system/background_tasks_changed →
system/task_started → user [tool_result "Command running in background with ID …"]
→ assistant [text] → result/success.

## Samovoľný ťah (po ~10 s, keď ping dobehne)
system/task_updated (patch.status=completed) → system/task_notification
(task_id, tool_use_id, status, output_file, summary) →
system/background_tasks_changed (tasks: []) → system/init → assistant [text]
→ result/success.

- `user` záznam s `<task-notification>` na stdout NEPRÍDE (ani pred, ani po).
- `system/init` príde na začiatku každého ťahu: prvého, samovoľného aj
  ďalšieho od klienta (prompt2: init 0.3 s po prompte).

## Interrupt počas samovoľného ťahu (poslaný hneď po jeho system/init)
control_response success → user [text "[Request interrupted by user]"] →
result/error_during_execution is_error=true. Všetko do 10 ms.

## Neoverené
- Čo príde, keď task dobehne POČAS ťahu od klienta (notifikácia sa asi
  pridá do bežiaceho ťahu, nový init by nemal prísť).
