# probe_noauth — neprihlásený agent (2026-10-13, claude-gui-lkk.71.1)

Claude Code 2.1.296, codex-cli 0.160.0 (natívny). Prázdny `CLAUDE_CONFIG_DIR`,
resp. `CODEX_HOME`; prostredie bez `ANTHROPIC_*`, `CLAUDE_*`, `OPENAI_*`,
`CODEX_*`. Spustenie: `python tools/probe_noauth.py [claude] [codex]`, logy
v `C:/b/noauth-probe/logs` (`claude.log`, `codex-cli.log`, `codex-rpc.log`).

Stav: HOTOVO. Kredit sa nemíňa, prihlásenie sa len začne a zruší.

## Izolácia
Ostrý `~/.claude/.credentials.json`, `~/.claude.json` a `~/.codex/auth.json`
sa nezmenili (porovnanie mtime pred a po). Raz sa `~/.claude.json` zmenil,
ale pri opakovaní nie, a jeho mtime sedel s chvíľou mimo behu Claude CLI —
zapísala ho rodičovská session Claude Code, nie sonda. Claude si
`.claude.json` založí v `CLAUDE_CONFIG_DIR`.

## Claude
- `claude auth status --json` → `{"loggedIn": false, "authMethod": "none",
  "apiProvider": "firstParty", ..., "configDirectory": ...}`, exit 1.
  `--text` → „Not logged in. Run claude auth login to authenticate.", exit 1.
- Headless stream: štart prejde. `initialize` dostane normálne
  `control_response` success (aj so zoznamom príkazov). Nič neohlási vopred.
- Až prompt: `system/init` (normálny) → `assistant` s `model: "<synthetic>"`,
  `error: "authentication_failed"`, `is_api_error_message`, text
  „Not logged in · Please run /login" → `result` so `subtype: "success"`(!),
  `is_error: true`, `terminal_reason: "api_error"`, `api_error_status: null`,
  `result` = ten istý text. Celé za ~0,2 s.
- Strojovo spoľahlivé je pole `error: "authentication_failed"` na zázname
  `assistant`; `subtype` výsledku klame a text je pre človeka.
- Po zatvorení stdin proces skončí s kódom 1. Bez zatvorenia sa nemeralo
  (appka stdin drží, takže proces čaká na ďalší prompt).
- `claude auth login` má `--claudeai`, `--console`, `--sso`, `--email`.
- `claude auth login` bez TTY (`python tools/probe_noauth.py login`,
  13. 10. 2026, claude-gui-lkk.71.4) **funguje cez rúry**:
  - hneď na stdout „Opening browser to sign in…" a „If the browser didn't
    open, visit: https://claude.com/cai/oauth/authorize?code=true&…"
    (riadok je dlhý, sonda ho láme po 400 bajtoch). Prehliadač otvorí CLI
    samo.
  - `redirect_uri` je `https://platform.claude.com/oauth/code/callback`, teda
    ručný tok: stránka ukáže kód a ten sa vloží na stdin. Riadok so zlým
    kódom vyvolal výmenu, „Login failed: Request failed with status code
    400" na stderr a exit 1. Kód sa teda zo stdin číta.
  - výzva „Paste code here if prompted > " príde na stdout **bez konca
    riadku** a až po prečítaní kódu, takže čakať na ňu nemá zmysel.
  - po neúspechu je v `CLAUDE_CONFIG_DIR` `.claude.json` a `backups`,
    ostré súbory ostali netknuté.
  - úspech sa nemeral (vyžaduje skutočné prihlásenie).

## Codex
- `codex login status` → stderr „Not logged in", exit 1.
- `initialize` prejde. `account/read {refreshToken:false}` →
  `{"account": null, "requiresOpenaiAuth": true, "workspaceRouting": null}`.
  Rozpoznať sa to dá teda **pred prvým ťahom**.
- `thread/start` prejde (model `gpt-6.1-sol`), `turn/start` prejde, potom
  na stderr `401 Unauthorized` a notifikácie `error` s `willRetry: true`
  („Reconnecting... N/5", `codexErrorInfo.responseStreamDisconnected
  .httpStatusCode: 401`), prechod z WebSocket na HTTPS (`warning`), znova
  5 pokusov, a po ~15 s `error` s `willRetry: false`
  (`codexErrorInfo.httpConnectionFailed.httpStatusCode: 401`) a
  `turn/completed` so `status: "failed"` a tou istou chybou v `turn.error`.
- `account/login/start {type:"chatgpt"}` → `{type, loginId, authUrl}`,
  okamžite; app-server počúva na `127.0.0.1:1455` a prehliadač neotvára.
- `account/login/start {type:"chatgptDeviceCode"}` →
  `{type, loginId, verificationUrl: "https://auth.openai.com/codex/device",
  userCode: "XXXX-XXXXX"}` za ~0,3 s. Najlepšia cesta pre nevidiaceho:
  adresa aj kód sú text, ktorý sa dá prečítať a skopírovať.
- `account/login/cancel {loginId}` → `{status: "canceled"}` a hneď
  `account/login/completed {loginId, success: false, error: "..."}`.
- Úspešné dokončenie sa nemeralo (vyžaduje skutočné prihlásenie);
  podľa schémy príde `account/login/completed success: true` a
  `account/updated`.
