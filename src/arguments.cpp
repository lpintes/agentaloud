#include "arguments.h"

#include "app_name.h"
#include "i18n/i18n.h"
#include "model/utf.h"

namespace app {

using i18n::Str;

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
      complain(i18n::Format(Str::kArgNeedsValue, {word}));
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
      complain(i18n::Format(Str::kArgUnknownOption, {word}));
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
  arguments->error = i18n::Format(Str::kUnknownBackend,
                                  {Wide(arguments->backend), Joined(known)});
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
  arguments->error =
      i18n::Format(Str::kUnknownMode, {arguments->permissionMode,
                                       Wide(capabilities.agentName),
                                       Joined(valid)});
}

// Whoever types --help types it at a prompt, so this is the one place the
// application answers in text rather than in a window.  The wording is the
// catalog's, one block per language (kHelp), so it no longer sits beside
// Parse; what keeps the two in step is TestArguments, which looks for every
// option in every language.  What can be filled in from the truth -- the
// backends, the modes, the languages -- is.
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
  const std::wstring settings =
      settingsFile.empty()
          ? std::wstring(i18n::Text(Str::kHelpNoSettingsFolder))
          : settingsFile;
  return i18n::Format(Str::kHelp,
                      {L"" APP_NAME, version, Joined(backends),
                       Wide(kDefaultBackend), Wide(defaultBackend.agentName),
                       modes, settings, Joined(languages), L"" APP_EXE});
}

}  // namespace app
