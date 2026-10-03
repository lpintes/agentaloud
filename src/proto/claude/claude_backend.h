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

#include <string>
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
  bool SendPrompt(const std::string& utf8Text) override;
  bool Interrupt() override;
  bool SetMode(const std::string& id) override;
  void Stop(unsigned turnTimeoutMs) override;

  std::string conversationId() const override;
  std::string mode() const override;
  agent::Account account() const override;
  std::vector<agent::SlashCommand> commands() const override;
  bool ready() const override;

 private:
  // Reader thread.
  void OnRecord(const Event& event);
  PermissionDecision OnPermission(const PermissionRequest& request);

  agent::Capabilities capabilities_;
  Callbacks callbacks_;
  Session session_;
  // Reader thread only, like everything below it: they are about the order
  // records arrive in, and only that thread sees the order.
  Translator translator_;
  // The mode last reported as ModeChanged, so that a change is reported once.
  std::string reportedMode_;
  bool reportedReady_ = false;
};

}  // namespace proto

#endif
