#include "proto/claude/claude_backend.h"

#include "i18n/i18n.h"
#include "proto/claude/ask.h"
#include "proto/claude/sessions.h"

namespace proto {
namespace {

// A mode name or a model id from one spelling into the other.  Both are ASCII
// -- the CLI's own words -- so this is a copy, not a conversion, and it keeps
// proto/ from reaching up into model/ for Utf16FromUtf8 (see sessions.h).
std::wstring Ascii(const std::string& text) {
  return std::wstring(text.begin(), text.end());
}

std::string Ascii(const std::wstring& text) {
  std::string out;
  out.reserve(text.size());
  for (wchar_t character : text) out.push_back(static_cast<char>(character));
  return out;
}

// `decision_reason_type` as a sentence.  The wire's words are for a program:
// "rule" alone in a dialog says nothing about which rule or whose.
//
// All values below were measured, not read -- "rule" in tools/spike_control.py
// (a git commit against an ask rule), "subcommandResults" during the probes
// on claude-gui-lkk.25, and "other" on a Bash command with no rule at all
// (claude-gui-lkk.61), where decision_reason said "This command requires
// approval".  Anything else is passed through as it came: an unknown
// word is still more than no word, and inventing a translation for it would be
// the one failure the dialog cannot afford.  Empty stays empty; the dialog
// says that the CLI gave none.
std::string ReasonSentence(const std::string& type) {
  if (type == "rule") return i18n::Utf8(i18n::Str::kReasonRule);
  if (type == "subcommandResults") {
    return i18n::Utf8(i18n::Str::kReasonSubcommands);
  }
  if (type == "other") return i18n::Utf8(i18n::Str::kReasonOther);
  return type;
}

}  // namespace

ClaudeBackend::ClaudeBackend() : capabilities_(ClaudeCapabilities()) {}

bool ClaudeBackend::Start(const agent::StartOptions& options,
                          Callbacks callbacks) {
  callbacks_ = std::move(callbacks);

  Session::Options session;
  session.workingDir = options.projectDir;
  session.model = options.model;
  // "manual" is the CLI's other name for "default", and the CLI answers with
  // "default" whichever was asked for.  Said as "default" from the start, so
  // the handshake does not read as a change of mode that nobody made.
  session.permissionMode =
      Ascii(options.mode == "manual" ? std::string("default") : options.mode);
  session.extraArgs = options.extraArgs;
  if (options.resume == agent::StartOptions::Resume::ById) {
    // An empty id is a bare --resume, passed on bare: under --print the CLI
    // refuses it with a message of its own rather than opening a picker
    // (invariant 14), and saying that is its job, not ours.
    session.extraArgs.push_back(L"--resume");
    if (!options.resumeId.empty()) session.extraArgs.push_back(options.resumeId);
  } else if (options.resume == agent::StartOptions::Resume::Latest) {
    // Decided here and turned into a plain --resume, rather than passed to the
    // CLI as --continue: the CLI answers "which conversation was last" out of
    // ~/.claude/history.jsonl, where only interactively typed prompts are
    // written, so for a folder used from both a terminal and AgentAloud it
    // would carry on the terminal's conversation and call it ours (invariant
    // 15).
    //
    // Nothing to carry on is not an error and not a reason to refuse: a folder
    // nobody has worked in yet gets a new session, which is what was wanted,
    // and it is left looking exactly like one.  Nor is it said which one was
    // picked: the restored transcript is the answer, and Ctrl+Home leads to
    // its first prompt.
    SessionSummary latest;
    if (LatestSession(options.projectDir, &latest)) {
      session.extraArgs.push_back(L"--resume");
      session.extraArgs.push_back(latest.id);
    }
  }

  // What usage is read for until the stream names a model: the one asked for,
  // or -- below -- the one the history ended on.  Only a seed, not an answer:
  // the resume may have been launched with another --model, and the first
  // system/init is then the fresher word.
  translator_.SeedModel(Ascii(options.model));

  // A resumed session gets nothing of its history from the stream, so it is
  // read back off the file the CLI keeps (invariant 18).  A resume whose file
  // cannot be found -- --resume takes a session title, and a title is not a
  // file name -- has no history, and that is said by saying nothing.
  std::vector<Json> records;
  if (ResumesConversation(session.extraArgs) &&
      ReadSessionRecords(
          SessionFilePath(session.workingDir,
                          ResumedConversation(session.extraArgs)),
          &records)) {
    std::vector<agent::Event> history = TranslateHistory(records, &translator_);
    for (const agent::Event& event : history) {
      if (const auto* changed = std::get_if<agent::ModelChanged>(&event)) {
        translator_.SeedModel(changed->model);
      }
    }
    if (callbacks_.onHistory) callbacks_.onHistory(std::move(history));
  }

  return session_.Start(
      session, [this](const Event& event) { OnRecord(event); },
      [this](const PermissionRequest& request) {
        return OnPermission(request);
      },
      [this](const win::Process::Exit& exit) {
        if (!callbacks_.onEvents) return;
        callbacks_.onEvents({agent::SessionEnded{exit.codeKnown, exit.code,
                                                 exit.errorOutput}});
      });
}

void ClaudeBackend::OnRecord(const Event& event) {
  std::vector<agent::Event> batch = translator_.Translate(event.raw);

  // The mode, off Session rather than off the record: it arrives on four kinds
  // of record and Session has already folded in all of them, together with
  // the rules about which report may move it (PermissionModeTracker).
  // Reported after the record's own events, as a change -- a ModeChanged that
  // repeats the mode the pane already has is harmless, it ignores it.
  //
  // The answer to a Shift+Tab is reported even when the mode equals the last
  // one reported.  The pane moved ahead on the key, which reportedMode_ never
  // saw, so a refusal falls back to exactly that mode -- and with the plain
  // comparison the pane kept the refused one (found checking
  // claude-gui-lkk.46: plan, Shift+Tab to a refused auto, F2 said auto).
  const std::string mode = session_.permissionMode();
  if (!mode.empty() && (mode != reportedMode_ || IsModeAnswer(event.raw))) {
    reportedMode_ = mode;
    batch.push_back(agent::ModeChanged{mode});
  }
  if (!reportedReady_ && ready()) {
    reportedReady_ = true;
    batch.push_back(agent::Ready{});
  }
  if (!batch.empty() && callbacks_.onEvents) {
    callbacks_.onEvents(std::move(batch));
  }
}

PermissionDecision ClaudeBackend::OnPermission(
    const PermissionRequest& request) {
  PermissionDecision decision;

  // AskUserQuestion is not a permission at all, however it travels (invariant
  // 12).  It is the model asking the reader something, and the answer goes
  // back in the field that edits a tool's arguments -- see proto/ask.h.
  //
  // The parse has to succeed as well as the name match: an input that cannot
  // be drawn falls through to the general prompt, which at least shows it.
  std::vector<AskQuestion> asked;
  if (request.toolName == kAskUserQuestionTool &&
      ParseAskUserQuestion(request.input, &asked) && callbacks_.onQuestion) {
    agent::QuestionRequest question;
    question.by = translator_.AgentName(request.agentId);
    for (const AskQuestion& item : asked) {
      agent::Question out;
      // Claude files an answer under the text of its question.
      out.id = item.question;
      out.text = item.question;
      out.header = item.header;
      out.multiSelect = item.multiSelect;
      out.allowsOther = true;
      for (const AskOption& option : item.options) {
        out.options.push_back({option.label, option.description});
      }
      question.questions.push_back(std::move(out));
    }
    const agent::QuestionAnswer answer = callbacks_.onQuestion(question);
    if (answer.declined) {
      // Written for the model.  "Denied" would read as a rule refusing the
      // tool and invite a retry; this says a person declined to answer, which
      // is a thing to stop for.
      decision.denyMessage = kQuestionDeclinedInstruction;
    } else {
      decision.allow = true;
      decision.updatedInput =
          MakeAskAnswers(request.input, asked, answer.chosen);
    }
    return decision;
  }

  if (!callbacks_.onPermission) return decision;
  agent::PermissionRequest out;
  out.call = ToolCallFromInput(request.toolName, request.toolUseId,
                               request.input);
  out.title = request.displayName;
  out.description = request.description;
  out.reason = ReasonSentence(request.decisionReasonType);
  out.by = translator_.AgentName(request.agentId);
  // "For this session" is what the CLI suggests and nothing broader: for a
  // command that is the exact command line, for a file edit acceptEdits.
  const Json kept = SessionPermissions(request.suggestions);
  out.offered = {agent::Verdict::Allow};
  if (!kept.is_null()) out.offered.push_back(agent::Verdict::AllowForSession);
  out.offered.push_back(agent::Verdict::Deny);
  const agent::PermissionAnswer answer = callbacks_.onPermission(out);
  decision.allow = answer.verdict == agent::Verdict::Allow ||
                   answer.verdict == agent::Verdict::AllowForSession;
  if (answer.verdict == agent::Verdict::AllowForSession) {
    decision.updatedPermissions = kept;
  }
  // What the model is told is the adapter's to write, not the pane's: the
  // pane does not know which CLI it is talking to, nor that the words come
  // back in the stream for the Translator to recognise.
  decision.denyMessage =
      answer.message.empty() ? kDeniedInstruction : answer.message;
  return decision;
}

bool ClaudeBackend::SendPrompt(const std::string& utf8Text) {
  return session_.SendPrompt(utf8Text);
}

bool ClaudeBackend::Interrupt() { return session_.Interrupt(); }

bool ClaudeBackend::StopTask(const std::string& id) {
  return session_.StopTask(id);
}

bool ClaudeBackend::StopAllTasks() { return session_.StopAllTasks(); }

bool ClaudeBackend::Background() { return session_.BackgroundTasks(); }

bool ClaudeBackend::SetMode(const std::string& id) {
  return session_.SetPermissionMode(id);
}

void ClaudeBackend::Stop(unsigned turnTimeoutMs) { session_.Stop(turnTimeoutMs); }

std::string ClaudeBackend::conversationId() const {
  return session_.sessionId();
}

std::string ClaudeBackend::mode() const { return session_.permissionMode(); }

std::vector<std::string> ClaudeBackend::refusedModes() const {
  return session_.refusedModes();
}

agent::Account ClaudeBackend::account() const {
  const InitializeInfo handshake = session_.handshake();
  agent::Account account;
  account.email = handshake.accountEmail;
  account.plan = handshake.subscriptionType;
  // Said only when it is NOT the ordinary one.  Through Bedrock or Vertex the
  // billing is somebody else's, so "0 účtované" would mean something different
  // again.
  if (!handshake.apiProvider.empty() && handshake.apiProvider != "firstParty") {
    account.billingNote = handshake.apiProvider;
  }
  return account;
}

std::vector<agent::SlashCommand> ClaudeBackend::commands() const {
  std::vector<agent::SlashCommand> out;
  for (const SlashCommand& command : session_.handshake().commands) {
    out.push_back({command.name, command.description, command.argumentHint,
                   command.aliases});
  }
  return out;
}

bool ClaudeBackend::ready() const {
  return !session_.handshake().requestId.empty();
}

}  // namespace proto
