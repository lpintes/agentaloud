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

#include <atomic>
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
  agent::StartFailure startFailure() const override {
    return {L"codex", process_.startError()};
  }
  bool SendPrompt(const std::string& utf8Text) override;
  bool Interrupt() override;
  // A subagent is stopped by interrupting the turn of its thread and cleaning
  // the thread's terminals, which the interrupt leaves running; a command by
  // terminating its unified exec process.  Neither says anything on our
  // thread, the translator hears it on theirs
  // (tools/probe_codex_subagents.notes.md, spawn-kill and bgterm).
  bool StopTask(const std::string& id) override;
  // Interrupting our turn stops neither subagents nor commands (spawn-int,
  // bgterm-int), so each goes on its own: every subagent's turn, and every
  // thread's terminals at once by clean.
  bool StopAllTasks() override;
  // Codex has no foreground to move from: a subagent always runs beside the
  // turn, and a command is left running by the model, not by us.
  bool Background() override { return false; }
  bool SetMode(const std::string& id) override;
  void Stop(unsigned turnTimeoutMs) override;

  std::string conversationId() const override;
  std::string mode() const override;
  // A refused settings update has been seen in no measurement, so nothing
  // is kept: ModeTracker falls back and the next press tries again.
  std::vector<std::string> refusedModes() const override { return {}; }
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
    // Interrupting a subagent, terminate, clean: what they did arrives as
    // notifications, and a refusal means the task was gone already.
    StopTask,
    // thread/read of a subagent just started, for its nickname.
    ReadSubagent,
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
  // Set by Stop(), so that the end it causes is not reported as the CLI's.
  std::atomic<bool> stopping_{false};

  mutable std::mutex mutex_;
  std::condition_variable turnEnded_;
  std::map<long long, Purpose> pending_;
  long long nextId_ = 1;
  std::string threadId_;
  std::string turnId_;
  bool turnInFlight_ = false;
  bool interruptWanted_ = false;
  bool ready_ = false;
  // What StopTask needs and BackgroundTask does not carry.  A subagent's
  // thread by its id, with the turn it is in, empty between turns; a
  // command's unified exec processId by its item id, for our thread only --
  // the translator lists no other thread's commands.
  std::map<std::string, std::string> subagentTurns_;
  std::map<std::string, std::string> processes_;
  // Subagents asked to stop before their turn had an id.
  std::vector<std::string> stopWanted_;
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
