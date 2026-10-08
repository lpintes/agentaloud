#include "proto/claude/session.h"

#include <objbase.h>

#include <chrono>

namespace proto {

std::wstring NewSessionId() {
  GUID guid = {};
  if (CoCreateGuid(&guid) != S_OK) return std::wstring();
  wchar_t braced[40] = {};
  if (StringFromGUID2(guid, braced, 40) == 0) return std::wstring();
  // StringFromGUID2 writes {XXXXXXXX-XXXX-...} in upper case.  The CLI asks
  // for a UUID and the canonical spelling of one has neither braces nor
  // capitals; whether it would take them anyway is a question not worth
  // having, since the id also ends up as a file name on disk.
  std::wstring id(braced + 1);
  if (!id.empty() && id.back() == L'}') id.pop_back();
  for (wchar_t& character : id) {
    if (character >= L'A' && character <= L'F') character += L'a' - L'A';
  }
  return id;
}

std::wstring BuildCommandLine(const Session::Options& options) {
  std::wstring line = L"claude";
  auto add = [&line](const wchar_t* argument) {
    line += L' ';
    line += argument;
  };
  add(L"-p");
  // stream-json output is refused without it, and it is what puts the typed
  // records on the stream in the first place.
  add(L"--verbose");
  add(L"--input-format");
  add(L"stream-json");
  add(L"--output-format");
  add(L"stream-json");
  // The one switch the whole design rests on.  See control.h.
  add(L"--permission-prompt-tool");
  add(L"stdio");
  if (!options.permissionMode.empty()) {
    add(L"--permission-mode");
    line += L' ';
    line += win::Process::Quote(options.permissionMode);
  }
  if (!options.model.empty()) {
    add(L"--model");
    line += L' ';
    line += win::Process::Quote(options.model);
  }
  if (!options.sessionId.empty()) {
    add(L"--session-id");
    line += L' ';
    line += win::Process::Quote(options.sessionId);
  }
  for (const std::wstring& argument : options.extraArgs) {
    line += L' ';
    line += win::Process::Quote(argument);
  }
  return line;
}

bool ResumesConversation(const std::vector<std::wstring>& extraArgs) {
  for (const std::wstring& argument : extraArgs) {
    if (argument == L"--resume" || argument == L"-r" ||
        argument == L"--continue" || argument == L"-c") {
      return true;
    }
  }
  return false;
}

std::wstring ResumedConversation(const std::vector<std::wstring>& extraArgs) {
  // The last one wins, for the same reason the CLI's own parser takes it: two
  // --resume on one command line is somebody correcting themselves.
  std::wstring named;
  for (size_t i = 0; i + 1 < extraArgs.size(); ++i) {
    if (extraArgs[i] == L"--resume" || extraArgs[i] == L"-r") {
      named = extraArgs[i + 1];
    }
  }
  // A bare --resume at the end of the line has nothing after it, and neither
  // has --continue; both leave this empty, which is the honest answer.
  return named;
}

namespace {

// Does the caller already say something about which conversation this is?
// Asked before Start makes an id of its own, because the CLI refuses the two
// together: "--session-id can only be used with --continue or --resume if
// --fork-session is also specified" -- exit code 1, nothing on stdout, and in
// a windowed process nothing anywhere the user can see (measured 2026-09-06,
// tools/probe_session_id.py).  Resuming keeps the id it resumes, so a session
// started this way has one either way.
bool SaysWhichConversation(const std::vector<std::wstring>& extraArgs) {
  if (ResumesConversation(extraArgs)) return true;
  for (const std::wstring& argument : extraArgs) {
    if (argument == L"--session-id") return true;
  }
  return false;
}

}  // namespace

Session::~Session() { Stop(5000); }

bool Session::Start(const Options& options, EventCallback onEvent,
                    PermissionCallback onPermission, ExitCallback onExit) {
  onEvent_ = std::move(onEvent);
  onPermission_ = std::move(onPermission);
  onExit_ = std::move(onExit);
  assembler_ = std::make_unique<LineAssembler>(
      [this](std::string_view line) { OnLine(line); });
  // The id is ours to decide, and deciding it here is the point: told to the
  // CLI it is true from the moment the process exists, whereas read off the
  // stream it would arrive whenever the first record that carries one does --
  // in a project without SessionStart hooks that is after the first turn.
  // Written before the reader thread exists, like initRequestId_.
  Options effective = options;
  if (effective.sessionId.empty() && !SaysWhichConversation(options.extraArgs)) {
    effective.sessionId = NewSessionId();
  }
  // A UUID is ASCII, so this is the whole of the conversion.  Empty when the
  // conversation is someone else's to name -- a resumed one keeps its own id,
  // and that one does arrive on the stream.
  sessionId_.assign(effective.sessionId.begin(), effective.sessionId.end());
  // The mode asked for on the command line is the mode the CLI starts in; see
  // PermissionModeTracker for why it is worth knowing before the handshake.
  // The handshake overwrites it with the CLI's own word, so a mode the CLI
  // read differently is corrected, not kept.  Mode names are ASCII.
  mode_.Seed(std::string(options.permissionMode.begin(),
                         options.permissionMode.end()));
  if (!process_.Start(
          BuildCommandLine(effective), options.workingDir,
          [this](std::string_view bytes) { OnBytes(bytes); },
          [this](const win::Process::Exit& exit) { OnExit(exit); })) {
    return false;
  }
  // Opening the channel before the first turn, so that the first thing to
  // travel on it is not a permission request under time pressure.
  initRequestId_ = "init-" + std::to_string(nextRequestId_++);
  // A write that fails is a child that died before reading its first line,
  // and its exit says why, with its own words off stderr.  Failing Start on
  // it instead would trade them for a GetLastError of zero.
  SendJson(MakeInitialize(initRequestId_));
  return true;
}

void Session::OnBytes(std::string_view bytes) {
  assembler_->Feed(bytes);
}

void Session::OnLine(std::string_view line) {
  Json record;
  std::string error;
  if (!ParseLine(line, &record, &error)) {
    // Not fatal.  A crashing child can put a stack trace on stdout and that
    // must not take the reader down with it.
    return;
  }

  Event event = Classify(std::move(record));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Normally a no-op: Start already knows the id, because it chose it.
    // This is what is left for the run where CoCreateGuid failed and the
    // session was launched without --session-id after all.
    if (sessionId_.empty()) sessionId_ = event.sessionId;
  }

  if (event.kind == EventKind::ControlResponse) {
    // The answer to our own opening request, and the only thing known about
    // the session before the first turn happens.
    InitializeInfo info;
    if (ParseInitializeResponse(event.raw, &info) &&
        info.requestId == initRequestId_) {
      std::lock_guard<std::mutex> lock(mutex_);
      handshake_ = info;
    }
  }

  // Every record, not only the ones classified as control: the changes the CLI
  // makes on its own arrive as system/status -- see ParsePermissionModeReport.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_.Observe(event.raw);
  }

  if (event.kind == EventKind::ControlRequest) {
    PermissionRequest request;
    if (ParsePermissionRequest(event.raw, &request)) {
      PermissionDecision decision;
      if (onPermission_) decision = onPermission_(request);
      SendJson(decision.allow
                   ? MakeAllow(request, decision.updatedInput,
                               decision.updatedPermissions)
                   : MakeDeny(request, decision.denyMessage.empty()
                                           ? "The user declined."
                                           : decision.denyMessage));
    } else {
      // elicitation lands here, and so would request_user_dialog -- but that
      // second one cannot actually arrive.  The CLI fails closed on it: a
      // dialog kind is only ever sent to a client that named it in
      // `initialize.supportedDialogKinds`, and we name none.  See invariant 12
      // in CLAUDE.md, and proto/ask.h for the request that DOES bring the
      // model's questions here -- it is an ordinary can_use_tool.
      //
      // Answering with an error is still right for what is left: it is worse
      // to leave the CLI waiting for a dialog no one will show than to say we
      // cannot.  For a parked dialog it would not even be heard -- the CLI
      // discards error-shaped answers to one, on the grounds that an error is
      // not a human's choice.
      const std::string requestId =
          event.raw.value("request_id", std::string());
      SendJson(MakeError(requestId, "not handled by this client yet"));
    }
  }

  if (event.kind == EventKind::Result) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      turnInFlight_ = false;
    }
    turnEnded_.notify_all();
  }

  if (onEvent_) onEvent_(event);
}

void Session::OnExit(const win::Process::Exit& exit) {
  // A turn the process died in will never have its result.  Over now, or
  // Stop() would wait out its whole timeout for it when the window closes.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    turnInFlight_ = false;
  }
  turnEnded_.notify_all();
  if (onExit_ && !stopping_) onExit_(exit);
}

std::string Session::sessionId() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessionId_;
}

InitializeInfo Session::handshake() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return handshake_;
}

std::string Session::permissionMode() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mode_.current();
}

std::vector<std::string> Session::refusedModes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mode_.refused();
}

bool Session::SendJson(const Json& value) {
  const std::string line = value.dump() + "\n";
  std::lock_guard<std::mutex> lock(writeMutex_);
  return process_.Write(line);
}

bool Session::SendPrompt(const std::string& utf8Text) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    turnInFlight_ = true;
  }
  const Json message = {
      {"type", "user"},
      {"message",
       {{"role", "user"},
        {"content", Json::array({{{"type", "text"}, {"text", utf8Text}}})}}}};
  return SendJson(message);
}

bool Session::Interrupt() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!turnInFlight_) return false;
  }
  // turnInFlight_ is deliberately left alone: the turn is not over when the
  // request is written, it is over when the CLI says so.  Anything that ends
  // it here would let Stop() close stdin while the CLI is still winding the
  // turn down -- the one thing control.h says never to do.
  return SendJson(MakeInterrupt("stop-" + std::to_string(nextRequestId_++)));
}

bool Session::SetPermissionMode(const std::string& mode) {
  const std::string requestId =
      kModeRequestPrefix + std::to_string(nextRequestId_++);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_.Requested(requestId, mode);
  }
  return SendJson(MakeSetPermissionMode(requestId, mode));
}

bool Session::WaitForTurn(unsigned milliseconds) {
  std::unique_lock<std::mutex> lock(mutex_);
  return turnEnded_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                             [this] { return !turnInFlight_; });
}

void Session::Stop(unsigned turnTimeoutMs) {
  // A turn still running is asked to stop, not waited out: closing a window
  // mid-turn would otherwise sit through the timeout and then close stdin under
  // the turn anyway.  Interrupted, it ends with its own result, usually within
  // a second, and the record on disk says it was interrupted.
  stopping_ = true;
  Interrupt();
  WaitForTurn(turnTimeoutMs);
  // Only now.  Closing earlier makes the CLI report a broken channel as a
  // permission rule -- see control.h.
  process_.CloseInput();
  process_.Wait(10000);
}

}  // namespace proto
