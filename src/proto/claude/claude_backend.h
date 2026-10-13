#ifndef PROTO_CLAUDE_CLAUDE_BACKEND_H
#define PROTO_CLAUDE_CLAUDE_BACKEND_H

// Claude Code behind the port: a proto::Session for the process and the
// control channel, a proto::Translator for the records, and the knowledge of
// what Claude's permission modes are called and what they do.
//
// Everything the pane used to know about Claude is here now, or below this:
// that AskUserQuestion arrives as a permission request (invariant 12), that a
// resumed history is read off ~/.claude/projects (invariant 18), the names of
// the modes and the order Shift+Tab steps through them (invariant 20).

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "agent/backend.h"
#include "proto/claude/session.h"
#include "proto/claude/translate.h"

namespace proto {

class ClaudeBackend : public agent::Backend {
 public:
  ClaudeBackend();
  ~ClaudeBackend() override = default;

  const agent::Capabilities& capabilities() const override {
    return capabilities_;
  }
  // A conversation named by --resume in extraArgs is read off disk and handed
  // to onHistory before the process starts, on the calling thread.
  bool Start(const agent::StartOptions& options, Callbacks callbacks) override;
  agent::StartFailure startFailure() const override {
    return {L"claude", session_.startError()};
  }
  bool SendPrompt(const std::string& utf8Text) override;
  bool Interrupt() override;
  bool StopTask(const std::string& id) override;
  bool StopAllTasks() override;
  bool Background() override;
  bool SetMode(const std::string& id) override;
  // `claude auth login`, a process of its own beside the session's: unlike
  // Codex, Claude's stream has no request for it (claude-gui-lkk.71.4).
  bool StartSignIn() override;
  bool SubmitSignInCode(const std::string& code) override;
  bool CancelSignIn() override;
  void Stop(unsigned turnTimeoutMs) override;

  std::string conversationId() const override;
  std::string mode() const override;
  std::vector<std::string> refusedModes() const override;
  agent::Account account() const override;
  std::vector<agent::SlashCommand> commands() const override;
  bool ready() const override;

 private:
  // Reader thread.
  void OnRecord(const Event& event);
  PermissionDecision OnPermission(const PermissionRequest& request);
  // The reader thread of the sign-in process.  `generation` is the sign-in
  // the call belongs to; a call from one already cancelled or replaced is
  // dropped.
  void OnSignInOutput(unsigned generation, std::string_view bytes);
  void OnSignInExit(unsigned generation, const win::Process::Exit& exit);

  agent::Capabilities capabilities_;
  Callbacks callbacks_;
  Session session_;
  // Reader thread only, like everything below it: they are about the order
  // records arrive in, and only that thread sees the order.
  Translator translator_;
  // The mode last reported as ModeChanged, so that a change is reported once.
  std::string reportedMode_;
  bool reportedReady_ = false;

  // Where `claude auth login` runs: the session's folder, as for the session.
  std::wstring projectDir_;
  // Guards everything below.  Never held while a Process is destroyed: its
  // destructor joins the reader thread, which may be waiting for this lock.
  std::mutex signInMutex_;
  // Counts sign-ins, so that the last words of a cancelled one are not taken
  // for the next one's.
  unsigned signInGeneration_ = 0;
  // Between StartSignIn and the SignInFinished it ends with.
  bool signingIn_ = false;
  bool signInPrompted_ = false;
  // stdout of the sign-in up to the next line break.
  std::string signInLine_;
  // Last, so that it dies first: its threads call into everything above.
  std::unique_ptr<win::Process> signIn_;
};

}  // namespace proto

#endif
