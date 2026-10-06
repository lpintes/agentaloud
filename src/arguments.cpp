#include "arguments.h"

#include "app_name.h"
#include "i18n/i18n.h"
#include "model/utf.h"

namespace app {

const char kDefaultBackend[] = "claude";

namespace {

std::wstring Wide(const std::string& text) { return model::Utf16FromUtf8(text); }

std::wstring Joined(const std::vector<std::string>& words) {
  std::wstring out;
  for (const std::string& word : words) {
    if (!out.empty()) out += L", ";
    out += Wide(word);
  }
  return out;
}

}  // namespace

Arguments Parse(const std::vector<std::wstring>& words) {
  Arguments arguments;
  // The first complaint is the one that gets said, and the reading carries on
  // regardless: --help typed after a typo is still a request for help, and the
  // help text is the better answer to both.
  auto complain = [&arguments](const std::wstring& text) {
    if (arguments.error.empty()) arguments.error = text;
  };
  for (size_t i = 0; i < words.size(); ++i) {
    const std::wstring& word = words[i];
    // Everything after it belongs to the CLI, as it is.  Not read, not
    // checked, not taken for the project: this is the one door through which
    // an option AgentAloud does not know reaches the CLI -- --chrome,
    // --add-dir -- and it is a door because the alternative, guessing which
    // words after an unknown option are its values, is how a value used to
    // become the project folder.
    if (word == L"--") {
      arguments.cliArgs.assign(words.begin() + static_cast<ptrdiff_t>(i) + 1,
                               words.end());
      break;
    }
    // An option that takes a value and stands last would otherwise fall
    // through to the branches below and be taken for the project folder --
    // the same silent swap this whole check exists to stop, only one word
    // further along.
    const bool wantsValue = word == L"--permission-mode" || word == L"--model" ||
                            word == L"--backend";
    if (wantsValue && i + 1 >= words.size()) {
      complain(L"voľba " + word + L" potrebuje hodnotu");
      break;
    }
    if (word == L"--backend") {
      // ASCII by nature, and checked against the list in CheckBackend; a word
      // with anything else in it fails there by not matching.
      const std::wstring& name = words[++i];
      arguments.backend.assign(name.begin(), name.end());
    // Checked in CheckMode, against the chosen backend's own modes.
    } else if (word == L"--permission-mode") {
      arguments.permissionMode = words[++i];
    // An alias -- sonnet, haiku, opus -- or a full model id.  Not checked: the
    // list of what the CLI takes is the CLI's, it changes with every release,
    // and a copy of it here would be a copy that goes stale.
    } else if (word == L"--model") {
      arguments.model = words[++i];
    // Carry on an earlier conversation.  Taken with its value wherever it
    // stands -- so `AgentAloud --resume <id> .` works and `AgentAloud .
    // --resume <id>` works too.
    //
    // The value is not checked against the shape of a UUID, because the CLI
    // takes a session title there as well ("--resume requires a valid session
    // ID or session title when used with --print"), and a check here would be
    // a second, narrower idea of what is legal.  A --resume with nothing after
    // it is passed on bare and the CLI says so itself: measured 2026-09-06, it
    // does NOT open the interactive picker under --print, it refuses with that
    // message and ends the turn with a `result` carrying is_error.
    } else if (word == L"--resume" || word == L"-r") {
      arguments.resume = true;
      if (i + 1 < words.size() && words[i + 1] != L"--") {
        arguments.resumeId = words[++i];
      }
    // Not answered here: it is a question about this folder, and which
    // conversation in it counts as the latest is the backend's to know.
    } else if (word == L"--continue" || word == L"-c") {
      arguments.continueLatest = true;
    // Read wherever it stands and nothing after it is looked at: `AgentAloud .
    // --help` is a request for help, not a session in this folder.
    } else if (word == L"--help" || word == L"-h") {
      arguments.help = true;
      break;
    } else if (word == L"--version") {
      arguments.version = true;
      break;
    // Anything else that starts with a dash is an option this process does not
    // have, and the one thing it must not become is the project folder: that
    // is how --fork-session used to end up as a path and the session started
    // somewhere that does not exist, with nothing said about why.  A bare "-"
    // is caught by the same rule rather than by an exception -- it is not a
    // folder anybody means on Windows, and a rule with one exception is a rule
    // nobody remembers.  An option meant for the CLI goes behind "--".
    } else if (!word.empty() && word[0] == L'-') {
      complain(L"neznáma voľba " + word +
               L" (voľby pre CLI patria za oddeľovač --)");
    } else if (arguments.project.empty()) {
      arguments.project = word;
    }
  }
  return arguments;
}

void CheckBackend(Arguments* arguments, const std::vector<std::string>& known) {
  if (arguments->help || arguments->version || !arguments->error.empty()) {
    return;
  }
  if (arguments->backend.empty()) arguments->backend = kDefaultBackend;
  for (const std::string& name : known) {
    if (name == arguments->backend) return;
  }
  arguments->error = L"neznámy backend " + Wide(arguments->backend) +
                     L"; známe sú: " + Joined(known);
}

void CheckMode(Arguments* arguments, const agent::Capabilities& capabilities) {
  if (arguments->help || arguments->version || !arguments->error.empty()) {
    return;
  }
  if (arguments->permissionMode.empty()) return;
  const std::string mode = model::Utf8FromUtf16(arguments->permissionMode);
  if (agent::FindMode(capabilities, mode) != nullptr) return;
  std::vector<std::string> valid;
  for (const agent::Mode& known : capabilities.modes) valid.push_back(known.id);
  arguments->error = L"režim " + arguments->permissionMode + L" " +
                     Wide(capabilities.agentName) + L" nepozná; platné sú: " +
                     Joined(valid);
}

// Whoever types --help types it at a prompt, so this is the one place the
// application answers in text rather than in a window.  Written out here, and
// not read from anywhere: a help text in a resource or a file is a help text
// that gets out of step with Parse above, and the two are in one file
// precisely so that they do not.  What can be filled in from the truth -- the
// backends, the modes -- is.
//
// It names every option this process understands, and then says what happens
// to the ones it does not, because that is the failure nobody would guess.
std::wstring HelpText(const std::wstring& version,
                      const std::vector<std::string>& backends,
                      const agent::Capabilities& defaultBackend,
                      const std::wstring& settingsFile) {
  std::wstring modes;
  for (const agent::Mode& mode : defaultBackend.modes) {
    modes += L"        " + Wide(mode.id) + L" — " + Wide(mode.label) + L"\n";
  }
  std::vector<std::string> languages;
  for (const i18n::Lang lang : i18n::Languages()) {
    languages.push_back(i18n::LanguageCode(lang));
  }
  return
      L"" APP_NAME L" " + version +
      L" — okno namiesto terminálu pre coding agentov (Claude Code, Codex).\n"
      L"\n"
      L"Použitie:\n"
      L"  " APP_NAME L" [voľby] [priečinok] [-- parametre CLI]\n"
      L"\n"
      L"  priečinok\n"
      L"      Pracovný adresár session: rozhoduje o tom, ktoré CLAUDE.md\n"
      L"      a ktorý git repozitár platia a čoho sa smú dotknúť nástroje.\n"
      L"      Keď sa neuvedie, " APP_NAME L" sa naň spýta dialógom Nová\n"
      L"      session, v ktorom sa dá zvoliť aj backend a model.\n"
      L"\n"
      L"  -- parametre CLI\n"
      L"      Všetko za samostatným -- ide do CLI bez zmeny a bez kontroly,\n"
      L"      napríklad: " APP_NAME L" C:\\projekt -- --chrome --add-dir D:\\iny\n"
      L"      Či CLI parameter v headless režime prijme, " APP_NAME L" nevie;\n"
      L"      parametre CLI vypíše jeho vlastné --help.\n"
      L"\n"
      L"Voľby:\n"
      L"  --backend <meno>\n"
      L"      Ktoré CLI beží za oknom: " + Joined(backends) + L".\n"
      L"      Bez neho " + Wide(kDefaultBackend) + L".\n"
      L"\n"
      L"  --permission-mode <režim>\n"
      L"      Režim povolení v pravopise CLI.  Pre " +
      Wide(defaultBackend.agentName) + L":\n" + modes +
      L"      Iný backend má vlastné režimy; neznámy režim " APP_NAME L"\n"
      L"      odmietne a vymenuje platné.  Bez voľby platí to, čo má\n"
      L"      nastavené CLI.\n"
      L"\n"
      L"  --model <alias|id>\n"
      L"      sonnet, haiku, opus alebo úplné id modelu.  Bez neho platí\n"
      L"      model z nastavení, teda ten drahý.\n"
      L"\n"
      L"  --resume <id|titul>, -r <id|titul>\n"
      L"      Pokračuje v pomenovanom rozhovore.  Predchádzajúce ťahy sa\n"
      L"      prečítajú z disku a kurzor stojí za nimi, na mieste, kde sa\n"
      L"      pokračuje; keď sa súbor nenájde — pod titulom sa nenájde\n"
      L"      nikdy — okno začne prázdne.\n"
      L"\n"
      L"  --continue, -c\n"
      L"      Pokračuje v poslednom rozhovore tohto priečinka.  Ktorý to je,\n"
      L"      vyberá " APP_NAME L" sám zo súborov v ~/.claude/projects — nie\n"
      L"      CLI, ktoré o headless session nevie.  Priečinok bez jediného\n"
      L"      rozhovoru začne novú session.  Spolu s --resume vyhráva\n"
      L"      --resume.\n"
      L"\n"
      L"  --help, -h\n"
      L"      Tento text.\n"
      L"\n"
      L"  --version\n"
      L"      Iba verziu " APP_NAME L".\n"
      L"\n"
      L"Voľba pred --, ktorú " APP_NAME L" nepozná — napríklad --fork-session —\n"
      L"sa neprepošle a ani sa z nej nestane cesta: povie to a skončí.  Kto\n"
      L"ju chce poslať CLI, napíše ju za --.  Priečinok projektu je prvý\n"
      L"argument pred --, ktorý sa nezačína pomlčkou, takže priečinok\n"
      L"s pomlčkou na začiatku mena sa takto zadať nedá.\n"
      L"\n"
      L"Nastavenia:\n"
      L"  " + (settingsFile.empty() ? std::wstring(L"(priečinok sa nedá zistiť)")
                                    : settingsFile) + L"\n"
      L"  Kým nie je dialóg nastavení, súbor sa píše ručne.  Riadok je\n"
      L"  kľúč=hodnota, # začína poznámku:\n"
      L"      backend=codex\n"
      L"      claude.permission-mode=auto\n"
      L"      claude.model=opus\n"
      L"      codex.permission-mode=plan\n"
      L"      check-updates=0\n"
      L"      language=en\n"
      L"  Platí to, čo nepovie príkazový riadok.  language je jeden z: " +
      Joined(languages) + L";\n"
      L"  bez neho hovorí " APP_NAME L" jazykom Windows, a keď ho nevie,\n"
      L"  po anglicky.  check-updates=0 vypne\n"
      L"  kontrolu novej verzie, ktorú " APP_NAME L" inak robí pri štarte raz\n"
      L"  denne; kedy kontroloval naposledy, si zapíše do toho istého súboru\n"
      L"  a ostatné riadky pritom nechá tak.  Neznámy kľúč alebo režim\n"
      L"  " APP_NAME L" odmietne a povie riadok, rovnako ako neznámu voľbu.\n"
      L"  Priečinok config vedľa " APP_EXE L", keď existuje, sa použije\n"
      L"  namiesto toho v %APPDATA%.\n"
      L"\n"
      L"Čo vie klávesnica, povie " APP_NAME L" sám: F1 vypíše všetky klávesy, F2\n"
      L"podrobnosti session a F4 otvorí zoznam slash príkazov.\n";
}

}  // namespace app
