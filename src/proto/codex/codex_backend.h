#ifndef PROTO_CODEX_CODEX_BACKEND_H
#define PROTO_CODEX_CODEX_BACKEND_H

// Codex CLI behind the port: one `codex app-server` process speaking JSON-RPC
// on stdio, one thread in it, and the translator (proto/codex/translate.h)
// for what it says.
//
// What differs from Claude and is carried here rather than in the port:
//
//   * The conversation's id is Codex's to make, and it is known only from the
//     answer to thread/start -- a tenth of a second in, but after Start has
//     returned.  conversationId() is empty until then, and a prompt sent
//     before it is queued, not lost.
//   * The history of a resumed thread comes back over the wire, in the shape
//     of the live stream, so onHistory is called on the reader thread.
//   * A permission request names only the item; the call itself was
//     announced by item/started, and the translator remembers it.
//   * A question has no item of its own, so the adapter puts the call and its
//     answer into the stream itself.
//   * Stdin is the whole connection: closed during a turn, the process exits
//     at once and the turn is thrown away (measured).  Stop interrupts the
//     turn and waits for it to end, as for Claude (invariant 1).

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "agent/backend.h"
#include "proto/codex/translate.h"
#include "proto/jsonl.h"
#include "win/process.h"

namespace proto::codex {

class CodexBackend : public agent::Backend {
 public:
  CodexBackend();
  ~CodexBackend() override;

  const agent::Capabilities& capabilities() const override {
    return capabilities_;
  }
  bool Start(const agent::StartOptions& options, Callbacks callbacks) override;
  bool SendPrompt(const std::string& utf8Text) override;
  bool Interrupt() override;
  bool SetMode(const std::string& id) override;
  void Stop(unsigned turnTimeoutMs) override;

  std::string conversationId() const override;
  std::string mode() const override;
  agent::Account account() const override;
  std::vector<agent::SlashCommand> commands() const override { return {}; }
  bool ready() const override;

 private:
  // What an answer is the answer to.  Ids are ours and the server's requests
  // are numbered separately, so this is looked up only for responses.
  enum class Purpose {
    Initialize,
    ListThreads,
    StartThread,
    ResumeThread,
    StartTurn,
    Interrupt,
    Settings,
  };

  bool Request(Purpose purpose, const char* method, Json params);
  bool Send(const Json& message);
  // The parameters that put a thread into `mode`, for thread/start and
  // thread/resume.  Plan is not among them: it is a collaboration mode,
  // which only thread/settings/update sets.
  Json ThreadParams(const std::string& mode) const;
  bool SendSettings(const std::string& mode);  // under no lock
  void StartTurnLocked(const std::string& text);  // mutex_ held

  // Reader thread.
  void OnLine(std::string_view line);
  void OnResponse(const Json& message);
  void OnServerRequest(const Json& message);
  void OnThreadOpened(const Json& result, bool resumed);
  void Emit(std::vector<agent::Event> batch);
  void TurnOver();

  agent::Capabilities capabilities_;
  Callbacks callbacks_;
  agent::StartOptions options_;
  win::Process process_;
  std::unique_ptr<LineAssembler> assembler_;
  // Reader thread only.
  Translator translator_;
  std::string reportedMode_;
  bool reportedReady_ = false;

  mutable std::mutex mutex_;
  std::condition_variable turnEnded_;
  std::map<long long, Purpose> pending_;
  long long nextId_ = 1;
  std::string threadId_;
  std::string turnId_;
  bool turnInFlight_ = false;
  bool interruptWanted_ = false;
  bool ready_ = false;
  // Prompts sent before the thread existed, in order.
  std::vector<std::string> queued_;
  ModeTracker mode_;
  // A mode asked for before the thread existed, set once it does.
  std::string modeAfterOpen_;
  // The thread's model, which a collaboration mode has to name.
  std::string model_;
  agent::Account account_;

  std::mutex writeMutex_;
};

}  // namespace proto::codex

#endif
