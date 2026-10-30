#include "proto/claude/claude_backend.h"

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

}  // namespace

ClaudeBackend::ClaudeBackend(agent::Program program)
    : program_(std::move(program)), capabilities_(ClaudeCapabilities()) {
  capabilities_.configHome = agent::ResolveConfigHome(
      program_, L"CLAUDE_CONFIG_DIR", L".claude", /*emptyIsUnset=*/false);
}

std::wstring ClaudeBackend::ConfigDir() const {
  const std::wstring* configured = program_.Find(L"CLAUDE_CONFIG_DIR");
  return configured ? *configured : std::wstring();
}

bool ClaudeBackend::ListConversations(const std::wstring& projectDir,
                                      std::vector<agent::Conversation>* out) {
  return ProjectConversations(projectDir, out, ConfigDir());
}

bool ClaudeBackend::Start(const agent::StartOptions& options,
                          Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
  projectDir_ = options.projectDir;

  Session::Options session;
  session.workingDir = options.projectDir;
  session.model = options.model;
  // "manual" is the CLI's other name for "default", and the CLI answers with
  // "default" whichever was asked for.  Said as "default" from the start, so
  // the handshake does not read as a change of mode that nobody made.
  session.permissionMode =
      Ascii(options.mode == "manual" ? std::string("default") : options.mode);
  session.extraArgs = options.extraArgs;
  session.program = program_.path;
  session.environment = program_.environment;
  if (options.resume == agent::StartOptions::Resume::ById) {
    // An empty id no longer comes from the command line -- a bare --resume
    // opens the list in Nová session there (claude-gui-lkk.7.11.4).  Were it
    // to come, it goes on bare: under --print the CLI refuses it with a
    // message of its own rather than opening a picker (invariant 14).
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
    if (LatestSession(options.projectDir, &latest, ConfigDir())) {
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
                          ResumedConversation(session.extraArgs),
                          ConfigDir()),
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
  // Session has already decided, from the prompts it has seen echoed, whether
  // this result ends everything or only the turn before a queued prompt's.
  if (event.kind == EventKind::Result && session_.turnInFlight()) {
    for (agent::Event& item : batch) {
      if (auto* ended = std::get_if<agent::TurnEnded>(&item)) ended->more = true;
    }
  }

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
  out.reason = ReasonSentence(request.decisionReasonType, request.blockedPath);
  out.by = translator_.AgentName(request.agentId);
  // The call asked about is the call that runs, and the transcript has the
  // one from the assistant record, which can be older: ExitPlanMode is asked
  // with the plan file's text after the model rewrote it (claude-gui-lkk.67).
  // Through the event queue, behind the ToolCallStarted it corrects; the
  // pane does not drain that queue before the dialog goes up.
  if (callbacks_.onEvents) {
    callbacks_.onEvents({agent::ToolCallChanged{out.call}});
  }
  // "For this session" is what the CLI suggests and nothing broader: for a
  // command that is the exact command line, for a file edit acceptEdits.
  const Json kept = SessionPermissions(request.suggestions);
  out.offered = {agent::Verdict::Allow};
  if (!kept.is_null()) {
    out.offered.push_back(agent::Verdict::AllowForSession);
    out.sessionScope = SessionScope(request.suggestions);
  }
  // "In this project" only where the CLI itself would write the project's
  // local settings; the writing is the CLI's, not ours.
  const Json project = ProjectPermissions(request.suggestions);
  if (!project.is_null()) {
    out.offered.push_back(agent::Verdict::AllowInProject);
    out.projectScope = ProjectScope(request.suggestions);
  }
  out.offered.push_back(agent::Verdict::Deny);
  // Approving a plan also picks the mode to go on in.  The CLI keeps a mode
  // sent with the approval over the one planning was entered from (measured,
  // tools/probe_plan_exit.py, claude-gui-lkk.43) -- the same setMode the
  // terminal's own plan dialog sends.
  const bool plan = request.toolName == kExitPlanModeTool;
  if (plan) {
    out.modesAfter = PlanModesAfter(session_.refusedModes(),
                                    session_.modeBeforePlan(), &out.modeAfter);
  }
  const agent::PermissionAnswer answer = callbacks_.onPermission(out);
  decision.allow = answer.verdict == agent::Verdict::Allow ||
                   answer.verdict == agent::Verdict::AllowForSession ||
                   answer.verdict == agent::Verdict::AllowInProject;
  if (answer.verdict == agent::Verdict::AllowForSession) {
    decision.updatedPermissions = kept;
  }
  if (answer.verdict == agent::Verdict::AllowInProject) {
    decision.updatedPermissions = project;
  }
  if (plan && decision.allow && !answer.mode.empty()) {
    decision.updatedPermissions = Json::array(
        {Json{{"type", "setMode"}, {"destination", "session"},
              {"mode", answer.mode}}});
  }
  // The plan sent back ends the turn, and what to change is the reader's next
  // prompt, as in the terminal (claude-gui-lkk.67).  Without interrupt the
  // model went on: with no words it asked what to change by AskUserQuestion,
  // one more dialog; with words, typed into the plan dialog, Enter sent and
  // Ctrl+Enter broke the line -- the prompt's keys the other way round.
  if (plan && !decision.allow) {
    decision.denyMessage = kKeepPlanningInstruction;
    decision.interrupt = true;
    return decision;
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

bool ClaudeBackend::SetModel(const std::string& id) {
  return session_.SetModel(id);
}

bool ClaudeBackend::SetEffort(const std::string& level) {
  return session_.SetEffort(level);
}

bool ClaudeBackend::StartSignIn() {
  std::unique_ptr<win::Process> previous;
  std::unique_ptr<win::Process> process = std::make_unique<win::Process>();
  unsigned generation = 0;
  {
    std::lock_guard<std::mutex> lock(signInMutex_);
    if (signingIn_) return false;
    signingIn_ = true;
    signInPrompted_ = false;
    signInLine_.clear();
    generation = ++signInGeneration_;
    previous = std::move(signIn_);
  }
  // The last one has ended, but its threads are joined here, unlocked.
  previous.reset();

  // With pipes and no console the CLI asks nothing: it prints the address at
  // once and reads the code the page shows off stdin.  It also opens the
  // browser itself, and nothing on its command line stops that (measured,
  // tools/probe_noauth.py login).
  process->SetEnvironment(program_.environment);
  const bool started = process->Start(
      win::Process::Quote(ProgramName()) + L" auth login", projectDir_,
      [this, generation](std::string_view bytes) {
        OnSignInOutput(generation, bytes);
      },
      [this, generation](const win::Process::Exit& exit) {
        OnSignInExit(generation, exit);
      });
  {
    std::lock_guard<std::mutex> lock(signInMutex_);
    if (started) {
      signIn_ = std::move(process);
      return true;
    }
    signingIn_ = false;
  }
  return false;
}

void ClaudeBackend::OnSignInOutput(unsigned generation,
                                   std::string_view bytes) {
  std::vector<agent::SignInPrompt> prompts;
  {
    std::lock_guard<std::mutex> lock(signInMutex_);
    if (generation != signInGeneration_ || !signingIn_) return;
    signInLine_.append(bytes);
    std::size_t end;
    while ((end = signInLine_.find('\n')) != std::string::npos) {
      agent::SignInPrompt prompt;
      if (!signInPrompted_ &&
          ParseSignInUrl(std::string_view(signInLine_).substr(0, end),
                         &prompt.url)) {
        signInPrompted_ = true;
        prompt.wantsCode = true;
        prompts.push_back(std::move(prompt));
      }
      signInLine_.erase(0, end + 1);
    }
  }
  if (!callbacks_.onEvents) return;
  for (agent::SignInPrompt& prompt : prompts) {
    callbacks_.onEvents({std::move(prompt)});
  }
}

void ClaudeBackend::OnSignInExit(unsigned generation,
                                 const win::Process::Exit& exit) {
  {
    std::lock_guard<std::mutex> lock(signInMutex_);
    if (generation != signInGeneration_ || !signingIn_) return;
    signingIn_ = false;
  }
  // A failure is "Login failed: <reason>" on stderr and exit code 1; the
  // CLI's words go to the reader as they are, without the line break.
  agent::SignInFinished done;
  done.signedIn = exit.codeKnown && exit.code == 0;
  if (!done.signedIn) {
    done.error = exit.errorOutput;
    while (!done.error.empty() &&
           static_cast<unsigned char>(done.error.back()) <= ' ') {
      done.error.pop_back();
    }
  }
  if (callbacks_.onEvents) callbacks_.onEvents({std::move(done)});
}

bool ClaudeBackend::SubmitSignInCode(const std::string& code) {
  std::lock_guard<std::mutex> lock(signInMutex_);
  if (!signingIn_ || !signIn_) return false;
  return signIn_->Write(code + "\n");
}

bool ClaudeBackend::CancelSignIn() {
  {
    std::lock_guard<std::mutex> lock(signInMutex_);
    if (!signingIn_) return false;
    signingIn_ = false;
    // Its exit, which follows, finds signingIn_ false and says nothing; the
    // Finished below is the one this sign-in ends with.
    if (signIn_) signIn_->Terminate();
  }
  if (callbacks_.onEvents) callbacks_.onEvents({agent::SignInFinished{}});
  return true;
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

std::vector<agent::ModelChoice> ClaudeBackend::modelCatalog() const {
  std::vector<agent::ModelChoice> out;
  for (const ModelEntry& model : session_.handshake().models) {
    out.push_back({model.value,
                   model.displayName.empty() ? model.value : model.displayName,
                   model.description, model.resolvedModel,
                   model.supportedEffortLevels});
  }
  return out;
}

std::string ClaudeBackend::effort() const { return session_.effort(); }

bool ClaudeBackend::ready() const {
  return !session_.handshake().requestId.empty();
}

}  // namespace proto
