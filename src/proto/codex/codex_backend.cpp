#include "proto/codex/codex_backend.h"

#include <algorithm>
#include <chrono>

#include "app_name.h"

namespace proto::codex {
namespace {

std::string Utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  std::string out(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::string StringField(const Json& object, const char* name) {
  if (!object.is_object()) return {};
  auto found = object.find(name);
  if (found == object.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

const Json& Field(const Json& object, const char* name) {
  static const Json null;
  if (!object.is_object()) return null;
  auto found = object.find(name);
  return found == object.end() ? null : *found;
}

// What Codex's approval wants for a verdict.  "decline" lets the turn go on
// without the call -- the model sees the refusal and carries on -- and
// "cancel" stops the turn.  Both measured; decline works even where
// availableDecisions does not list it.
const char* Decision(agent::Verdict verdict) {
  switch (verdict) {
    case agent::Verdict::Allow: return "accept";
    case agent::Verdict::AllowForSession: return "acceptForSession";
    case agent::Verdict::Deny: return "decline";
    case agent::Verdict::Abort: return "cancel";
  }
  return "decline";
}

}  // namespace

CodexBackend::CodexBackend() : capabilities_(CodexCapabilities()) {}

CodexBackend::~CodexBackend() { Stop(5000); }

bool CodexBackend::Start(const agent::StartOptions& options,
                         Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
  options_ = options;
  translator_.SeedModel(Utf8(options.model));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    model_ = Utf8(options.model);
    // What was asked for, until the server says what it got.  Plan is set
    // after the thread opens, so it is the mode being asked for, too.
    mode_.Seed(options.mode);
    ModeSettings settings;
    if (SettingsForMode(options.mode, &settings) && !settings.permissions) {
      modeAfterOpen_ = options.mode;
    }
  }

  // Found on PATH by CreateProcessW, the same way `claude` is.  That takes
  // the native installer's codex.exe (chatgpt.com/codex/install.ps1); the
  // npm package puts only shims there -- a .cmd, a .ps1 and a shell script
  // that start node -- and CreateProcessW runs none of them.
  std::wstring line = L"codex app-server";
  for (const std::wstring& argument : options.extraArgs) {
    line += L' ';
    line += win::Process::Quote(argument);
  }

  assembler_ = std::make_unique<LineAssembler>(
      [this](std::string_view text) { OnLine(text); });
  if (!process_.Start(
          line, options.projectDir,
          [this](std::string_view bytes) { assembler_->Feed(bytes); },
          [this](const win::Process::Exit& exit) {
            // A turn the process died in will never be completed; over now,
            // or Stop() would wait out its timeout for it.
            TurnOver();
            if (stopping_) return;
            Emit({agent::SessionEnded{exit.codeKnown, exit.code,
                                      exit.errorOutput}});
          })) {
    return false;
  }
  // experimentalApi unlocks thread/settings/update, which is the only way to
  // change the mode between turns and to enter plan mode at all.  A write
  // that fails is a child already dead, and its exit says why -- as with
  // Claude's initialize.
  Request(Purpose::Initialize, "initialize",
          {{"clientInfo",
            {{"name", "agentaloud"}, {"title", APP_NAME}, {"version", "0"}}},
           {"capabilities",
            {{"experimentalApi", true}, {"requestAttestation", false}}}});
  return true;
}

bool CodexBackend::Send(const Json& message) {
  const std::string line = message.dump() + "\n";
  std::lock_guard<std::mutex> lock(writeMutex_);
  return process_.Write(line);
}

bool CodexBackend::Request(Purpose purpose, const char* method, Json params) {
  long long id = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = nextId_++;
    pending_[id] = purpose;
  }
  return Send({{"id", id}, {"method", method}, {"params", std::move(params)}});
}

Json CodexBackend::ThreadParams(const std::string& mode) const {
  Json params = {{"cwd", Utf8(options_.projectDir)}};
  if (!options_.model.empty()) params["model"] = Utf8(options_.model);
  ModeSettings settings;
  if (SettingsForMode(mode, &settings) && settings.permissions) {
    params["approvalPolicy"] = settings.approvalPolicy;
    params["sandbox"] = settings.sandbox;
    params["approvalsReviewer"] = settings.reviewer;
  }
  return params;
}

bool CodexBackend::SendSettings(const std::string& mode) {
  ModeSettings settings;
  if (!SettingsForMode(mode, &settings)) return false;
  std::string thread;
  std::string model;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    thread = threadId_;
    model = model_;
  }
  Json params = {{"threadId", thread}};
  if (settings.permissions) {
    params["approvalPolicy"] = settings.approvalPolicy;
    params["sandboxPolicy"] = settings.sandboxPolicy;
    // Left out, the reviewer stays what it was (measured,
    // never-settings.log), so every preset sends its own.
    params["approvalsReviewer"] = settings.reviewer;
  }
  // Every preset says plan or not: leaving plan is part of moving to any of
  // them.  The collaboration mode has to name a model; the thread's own is
  // the one that is not a change.
  params["collaborationMode"] = {
      {"mode", settings.collaboration},
      {"settings",
       {{"model", model},
        {"reasoning_effort", nullptr},
        {"developer_instructions", nullptr}}}};
  return Request(Purpose::Settings, "thread/settings/update",
                 std::move(params));
}

void CodexBackend::StartTurnLocked(const std::string& text) {
  const long long id = nextId_++;
  pending_[id] = Purpose::StartTurn;
  const Json message = {
      {"id", id},
      {"method", "turn/start"},
      {"params",
       {{"threadId", threadId_},
        {"input", Json::array({{{"type", "text"},
                                {"text", text},
                                {"text_elements", Json::array()}}})}}}};
  // Written under mutex_ so that two prompts keep their order; the write
  // lock is always taken after this one, never before, so it cannot deadlock.
  Send(message);
}

bool CodexBackend::SendPrompt(const std::string& utf8Text) {
  std::lock_guard<std::mutex> lock(mutex_);
  turnInFlight_ = true;
  if (threadId_.empty()) {
    queued_.push_back(utf8Text);
    return true;
  }
  StartTurnLocked(utf8Text);
  return true;
}

bool CodexBackend::Interrupt() {
  std::string thread;
  std::string turn;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!turnInFlight_) return false;
    if (turnId_.empty()) {
      // The turn has been asked for and not yet numbered.  It is stopped as
      // soon as it has an id to stop it by.
      interruptWanted_ = true;
      return true;
    }
    thread = threadId_;
    turn = turnId_;
  }
  // turnInFlight_ stays: the turn is over when turn/completed says so
  // (invariant 1).
  return Request(Purpose::Interrupt, "turn/interrupt",
                 {{"threadId", thread}, {"turnId", turn}});
}

bool CodexBackend::StopTask(const std::string& id) {
  std::string thread;
  std::string process;
  std::string turn;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    thread = threadId_;
    if (auto found = processes_.find(id); found != processes_.end()) {
      process = found->second;
    } else if (auto agent = subagentTurns_.find(id);
               agent != subagentTurns_.end()) {
      turn = agent->second;
    }
    if (process.empty() && turn.empty()) {
      // A subagent whose turn has not started yet, or one between turns, or
      // a task that ended a moment ago.  Only the first can still be
      // stopped, and it is, once its turn has an id.
      stopWanted_.push_back(id);
    }
  }
  if (!process.empty()) {
    return Request(Purpose::StopTask, "thread/backgroundTerminals/terminate",
                   {{"threadId", thread}, {"processId", process}});
  }
  bool sent = true;
  if (!turn.empty()) {
    sent = Request(Purpose::StopTask, "turn/interrupt",
                   {{"threadId", id}, {"turnId", turn}});
  }
  // The interrupted turn leaves the subagent's command running -- a ping ran
  // on to its end (spawn-kill, and live 2026-10-09) -- and that command is in
  // no list of ours to stop by itself.
  return Request(Purpose::StopTask, "thread/backgroundTerminals/clean",
                 {{"threadId", id}}) &&
         sent;
}

bool CodexBackend::StopAllTasks() {
  std::vector<std::string> threads;
  std::vector<std::pair<std::string, std::string>> turns;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!threadId_.empty()) threads.push_back(threadId_);
    for (const auto& [thread, turn] : subagentTurns_) {
      threads.push_back(thread);
      if (!turn.empty()) turns.emplace_back(thread, turn);
    }
  }
  Interrupt();
  bool sent = true;
  for (const auto& [thread, turn] : turns) {
    sent = Request(Purpose::StopTask, "turn/interrupt",
                   {{"threadId", thread}, {"turnId", turn}}) &&
           sent;
  }
  for (const std::string& thread : threads) {
    sent = Request(Purpose::StopTask, "thread/backgroundTerminals/clean",
                   {{"threadId", thread}}) &&
           sent;
  }
  return sent;
}

bool CodexBackend::SetMode(const std::string& id) {
  ModeSettings settings;
  if (!SettingsForMode(id, &settings)) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (threadId_.empty()) {
      // Before the thread exists there is nothing to update; the mode is
      // set the moment it does.
      mode_.Seed(id);
      modeAfterOpen_ = id;
      return true;
    }
    mode_.Requested(id);
  }
  return SendSettings(id);
}

void CodexBackend::Stop(unsigned turnTimeoutMs) {
  // Asked to stop rather than waited out, as for Claude: the wait has a limit
  // and stdin closes after it either way.
  stopping_ = true;
  Interrupt();
  {
    std::unique_lock<std::mutex> lock(mutex_);
    turnEnded_.wait_for(lock, std::chrono::milliseconds(turnTimeoutMs),
                        [this] { return !turnInFlight_; });
  }
  process_.CloseInput();
  process_.Wait(10000);
}

std::string CodexBackend::conversationId() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return threadId_;
}

std::string CodexBackend::mode() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mode_.current();
}

agent::Account CodexBackend::account() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return account_;
}

bool CodexBackend::ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ready_;
}

// ---- Reader thread --------------------------------------------------------

void CodexBackend::OnLine(std::string_view line) {
  Json message;
  std::string error;
  if (!ParseLine(line, &message, &error)) return;

  switch (KindOf(message)) {
    case MessageKind::Response:
      OnResponse(message);
      return;
    case MessageKind::ServerRequest:
      OnServerRequest(message);
      return;
    case MessageKind::Malformed:
      return;
    case MessageKind::Notification:
      break;
  }

  const std::string method = StringField(message, "method");
  const Json& params = Field(message, "params");
  // A subagent's turns come on the same wire and are not ours to interrupt
  // or wait for.
  bool own = true;
  if (const std::string thread = StringField(params, "threadId");
      !thread.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    own = threadId_.empty() || thread == threadId_;
  }
  if (!own) {
    // Of a subagent, only what stopping it needs: the turn to interrupt.
    const std::string thread = StringField(params, "threadId");
    if (method == "turn/started") {
      const std::string turn = StringField(Field(params, "turn"), "id");
      bool interrupt = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        subagentTurns_[thread] = turn;
        auto wanted =
            std::find(stopWanted_.begin(), stopWanted_.end(), thread);
        if (wanted != stopWanted_.end()) {
          stopWanted_.erase(wanted);
          interrupt = true;
        }
      }
      if (interrupt) {
        Request(Purpose::StopTask, "turn/interrupt",
                {{"threadId", thread}, {"turnId", turn}});
      }
    } else if (method == "turn/completed") {
      // Kept with no turn: its terminals are still cleaned by StopAllTasks.
      std::lock_guard<std::mutex> lock(mutex_);
      subagentTurns_[thread].clear();
    }
  } else if (method == "item/started" || method == "item/completed") {
    const Json& item = Field(params, "item");
    if (StringField(item, "type") == "commandExecution" &&
        !Field(item, "processId").is_null()) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (method == "item/started") {
        processes_[StringField(item, "id")] = StringField(item, "processId");
      } else {
        processes_.erase(StringField(item, "id"));
      }
    }
  } else if (method == "thread/settings/updated") {
    const Json& settings = Field(params, "threadSettings");
    std::lock_guard<std::mutex> lock(mutex_);
    mode_.Reported(ModeFromThreadSettings(settings));
    if (const std::string model = StringField(settings, "model");
        !model.empty()) {
      model_ = model;
    }
  } else if (method == "account/updated") {
    std::lock_guard<std::mutex> lock(mutex_);
    account_.plan = StringField(params, "planType");
  } else if (method == "turn/started") {
    std::string turn = StringField(Field(params, "turn"), "id");
    bool interrupt = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // Also for a turn the agent began without a prompt from us, so that
      // Interrupt and Stop treat it as the turn it is (invariant 1).
      turnInFlight_ = true;
      turnId_ = turn;
      interrupt = interruptWanted_;
      interruptWanted_ = false;
    }
    if (interrupt) {
      Request(Purpose::Interrupt, "turn/interrupt",
              {{"threadId", conversationId()}, {"turnId", turn}});
    }
  }

  std::vector<agent::Event> batch = translator_.Translate(message);
  if (own && method == "turn/completed") TurnOver();
  Emit(std::move(batch));
}

void CodexBackend::TurnOver() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    turnInFlight_ = false;
    turnId_.clear();
    interruptWanted_ = false;
  }
  turnEnded_.notify_all();
}

void CodexBackend::Emit(std::vector<agent::Event> batch) {
  // The mode and readiness off the backend's own state rather than off the
  // message: they move on answers as well as on notifications.  Reported
  // after the message's own events, as changes; a ModeChanged that repeats
  // the mode the pane has is harmless.
  std::string mode;
  bool ready = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    mode = mode_.current();
    ready = ready_;
  }
  if (!mode.empty() && mode != reportedMode_) {
    reportedMode_ = mode;
    batch.push_back(agent::ModeChanged{mode});
  }
  if (!reportedReady_ && ready) {
    reportedReady_ = true;
    batch.push_back(agent::Ready{});
  }
  if (!batch.empty() && callbacks_.onEvents) {
    callbacks_.onEvents(std::move(batch));
  }
}

void CodexBackend::OnResponse(const Json& message) {
  const Json& id = Field(message, "id");
  if (!id.is_number_integer()) return;
  Purpose purpose;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = pending_.find(id.get<long long>());
    if (found == pending_.end()) return;
    purpose = found->second;
    pending_.erase(found);
  }
  const bool failed = message.contains("error");
  const Json& result = Field(message, "result");

  switch (purpose) {
    case Purpose::Initialize: {
      Send({{"method", "initialized"}});
      using Resume = agent::StartOptions::Resume;
      if (options_.resume == Resume::ById && !options_.resumeId.empty()) {
        Json params = ThreadParams(options_.mode);
        params.erase("cwd");
        params["threadId"] = Utf8(options_.resumeId);
        Request(Purpose::ResumeThread, "thread/resume", std::move(params));
      } else if (options_.resume != Resume::None) {
        // -c, and a bare --resume with it: which thread was last is asked of
        // Codex itself, limited to this folder.  Unlike Claude's -c this
        // finds the terminal's threads too, and rightly -- they live in the
        // same store and resume the same way, so carrying one on is
        // carrying it on, not pretending.
        Request(Purpose::ListThreads, "thread/list",
                {{"cwd", Utf8(options_.projectDir)},
                 {"limit", 1},
                 {"sortKey", "updated_at"}});
      } else {
        Request(Purpose::StartThread, "thread/start",
                ThreadParams(options_.mode));
      }
      break;
    }
    case Purpose::ListThreads: {
      const Json& data = Field(result, "data");
      const std::string latest =
          data.is_array() && !data.empty() ? StringField(data[0], "id") : "";
      if (latest.empty()) {
        // Nothing to carry on is a new thread, without a word (invariant 15).
        Request(Purpose::StartThread, "thread/start",
                ThreadParams(options_.mode));
      } else {
        Json params = ThreadParams(options_.mode);
        params.erase("cwd");
        params["threadId"] = latest;
        Request(Purpose::ResumeThread, "thread/resume", std::move(params));
      }
      break;
    }
    case Purpose::StartThread:
    case Purpose::ResumeThread:
      if (!failed) OnThreadOpened(result, purpose == Purpose::ResumeThread);
      break;
    case Purpose::StartTurn: {
      if (failed) {
        // The turn never started, so nothing else will end it.
        TurnOver();
        Emit({agent::TurnEnded{agent::TurnOutcome::Failed}});
        break;
      }
      const std::string turn = StringField(Field(result, "turn"), "id");
      bool interrupt = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (turnInFlight_ && turnId_.empty()) turnId_ = turn;
        interrupt = interruptWanted_ && !turn.empty();
        if (interrupt) interruptWanted_ = false;
      }
      if (interrupt) {
        Request(Purpose::Interrupt, "turn/interrupt",
                {{"threadId", conversationId()}, {"turnId", turn}});
      }
      break;
    }
    case Purpose::Settings: {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        mode_.Answered(!failed);
      }
      Emit({});
      break;
    }
    case Purpose::Interrupt:
    case Purpose::StopTask:
      break;
  }
}

void CodexBackend::OnThreadOpened(const Json& result, bool resumed) {
  const Json& thread = Field(result, "thread");
  const std::string model = StringField(result, "model");
  if (resumed) {
    translator_.SeedModel(model);
    if (callbacks_.onHistory) callbacks_.onHistory(TranslateHistory(result));
  }

  translator_.SetThread(StringField(thread, "id"));
  std::vector<std::string> queued;
  std::string modeAfterOpen;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    threadId_ = StringField(thread, "id");
    if (!model.empty()) model_ = model;
    mode_.Reported(ModeFromSettings(
        Field(result, "approvalPolicy"), Field(result, "sandbox"),
        StringField(result, "approvalsReviewer"),
        StringField(Field(result, "collaborationMode"), "mode")));
    ready_ = true;
    modeAfterOpen.swap(modeAfterOpen_);
    if (!modeAfterOpen.empty()) mode_.Requested(modeAfterOpen);
    queued.swap(queued_);
  }
  // The mode first: a prompt queued behind it is meant for the mode that was
  // asked for.
  if (!modeAfterOpen.empty()) SendSettings(modeAfterOpen);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const std::string& text : queued) StartTurnLocked(text);
  }
  Emit({});
}

void CodexBackend::OnServerRequest(const Json& message) {
  const Json& id = Field(message, "id");
  const std::string method = StringField(message, "method");
  const Json& params = Field(message, "params");

  if (method == "item/commandExecution/requestApproval" ||
      method == "item/fileChange/requestApproval") {
    agent::PermissionRequest request;
    const std::string itemId = StringField(params, "itemId");
    if (!translator_.FindCall(itemId, &request.call)) {
      // Not announced: built from the request itself, which for a command
      // carries the command and for a file change carries nothing.
      Json item = params;
      item["id"] = itemId;
      item["type"] = method == "item/fileChange/requestApproval"
                         ? "fileChange"
                         : "commandExecution";
      request.call = ToolCallFromItem(item);
    }
    // Codex's own sentence about why it asks, when the model asked to step
    // outside the sandbox.  Not a code to translate: it is already words.
    request.reason = StringField(params, "reason");
    request.by = translator_.Author(params);
    // A command lists what it takes, and "acceptForSession" is not always
    // there (measured: accept, acceptWithExecpolicyAmendment, cancel -- the
    // amendment is a rule written to disk, not offered here).  A file change
    // lists nothing and its schema takes it.
    bool session = true;
    const Json& available = Field(params, "availableDecisions");
    if (available.is_array()) {
      session = false;
      for (const Json& each : available) {
        if (each.is_string() && each.get<std::string>() == "acceptForSession") {
          session = true;
        }
      }
    }
    request.offered = {agent::Verdict::Allow};
    if (session) request.offered.push_back(agent::Verdict::AllowForSession);
    request.offered.push_back(agent::Verdict::Deny);
    request.offered.push_back(agent::Verdict::Abort);
    agent::PermissionAnswer answer;
    if (callbacks_.onPermission) answer = callbacks_.onPermission(request);
    Send({{"id", id}, {"result", {{"decision", Decision(answer.verdict)}}}});
    return;
  }

  if (method == "item/tool/requestUserInput") {
    agent::QuestionRequest request;
    request.questions = ReadQuestions(params);
    request.by = translator_.Author(params);
    const agent::ToolCall call = QuestionCall(params);
    // No item comes for it, so the transcript gets the call from here, before
    // the dialog -- as Claude's AskUserQuestion is in the transcript before
    // its dialog.
    Emit({agent::ToolCallStarted{call, request.by}});
    agent::QuestionAnswer answer;
    answer.declined = true;
    if (callbacks_.onQuestion) answer = callbacks_.onQuestion(request);
    Send({{"id", id},
          {"result", MakeQuestionAnswer(request.questions, answer)}});
    Emit({agent::ToolCallFinished{
        QuestionResult(call.id, request.questions, answer)}});
    return;
  }

  if (method == "mcpServer/elicitation/request") {
    agent::PermissionRequest request;
    if (ElicitationPermission(params, &request)) {
      request.by = translator_.Author(params);
      agent::PermissionAnswer answer;
      if (callbacks_.onPermission) answer = callbacks_.onPermission(request);
      Send({{"id", id}, {"result", MakeElicitationAnswer(answer.verdict)}});
      return;
    }
  }

  // Anything else -- a form with fields, a dynamic tool, a token refresh --
  // is refused rather than left waiting: the server waits without a timeout
  // (measured), and a turn stuck on a request nobody shows is worse than one
  // told no.
  Send({{"id", id},
        {"error",
         {{"code", -32601},
          {"message", std::string(APP_NAME) + " does not handle " + method}}}});
  Emit({agent::Unrecognised{method}});
}

}  // namespace proto::codex
