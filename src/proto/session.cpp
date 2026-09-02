#include "proto/session.h"

#include <chrono>

namespace proto {

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
  if (!options.model.empty()) {
    add(L"--model");
    line += L' ';
    line += win::Process::Quote(options.model);
  }
  for (const std::wstring& argument : options.extraArgs) {
    line += L' ';
    line += win::Process::Quote(argument);
  }
  return line;
}

Session::~Session() { Stop(5000); }

bool Session::Start(const Options& options, EventCallback onEvent,
                    PermissionCallback onPermission) {
  onEvent_ = std::move(onEvent);
  onPermission_ = std::move(onPermission);
  assembler_ = std::make_unique<LineAssembler>(
      [this](std::string_view line) { OnLine(line); });
  if (!process_.Start(BuildCommandLine(options), options.workingDir,
                      [this](std::string_view bytes) { OnBytes(bytes); })) {
    return false;
  }
  // Opening the channel before the first turn, so that the first thing to
  // travel on it is not a permission request under time pressure.
  return SendJson(MakeInitialize("init-" + std::to_string(nextRequestId_++)));
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
  // The first one wins; every later record carries the same id.
  if (sessionId_.empty()) sessionId_ = event.sessionId;

  if (event.kind == EventKind::ControlRequest) {
    PermissionRequest request;
    if (ParsePermissionRequest(event.raw, &request)) {
      PermissionDecision decision;
      if (onPermission_) decision = onPermission_(request);
      SendJson(decision.allow
                   ? MakeAllow(request, decision.updatedInput)
                   : MakeDeny(request, decision.denyMessage.empty()
                                           ? "The user declined."
                                           : decision.denyMessage));
    } else {
      // request_user_dialog and elicitation land here.  Answering with an
      // error is deliberate: it is worse to leave the CLI waiting for a
      // dialog that no one will ever show than to say we cannot.
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

bool Session::WaitForTurn(unsigned milliseconds) {
  std::unique_lock<std::mutex> lock(mutex_);
  return turnEnded_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                             [this] { return !turnInFlight_; });
}

void Session::Stop(unsigned turnTimeoutMs) {
  WaitForTurn(turnTimeoutMs);
  // Only now.  Closing earlier makes the CLI report a broken channel as a
  // permission rule -- see control.h.
  process_.CloseInput();
  process_.Wait(10000);
}

}  // namespace proto
