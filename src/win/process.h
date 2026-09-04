#ifndef WIN_PROCESS_H
#define WIN_PROCESS_H

// A child process with its standard input and output on pipes, read by a
// thread of its own.
//
// Not overlapped I/O, although that is what the design first called for:
// CreatePipe hands back synchronous handles and there is no flag that changes
// it.  Overlapped reads would mean CreateNamedPipe with a generated unique
// name -- more moving parts, a name to keep unique, and a race to accept the
// connection -- to arrive at what one blocking ReadFile on its own thread
// already does.  The thread is also the shape the caller wants later: a GUI
// wants the bytes handed to it, not a completion it has to poll for.
//
// Nothing in win/ knows what is on the other end of the pipes, so this can be
// lifted into another project as it stands.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

namespace win {

class Process {
 public:
  // Raw bytes as they arrive, in whatever chunks the pipe gives them -- not
  // lines, and not necessarily whole characters.  Reassembly is the caller's
  // job because only the caller knows what a record is.
  //
  // CALLED ON THE READER THREAD.  Anything that touches a window has to be
  // posted across; anything that touches the caller's state needs a lock.
  using OutputCallback = std::function<void(std::string_view)>;

  Process() = default;
  ~Process();
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  // commandLine is the whole line including argv[0]; quoting is the caller's
  // to get right, and Quote() below is here for that.  An empty workingDir
  // means "inherit ours", which is nearly never what is wanted here.
  bool Start(const std::wstring& commandLine, const std::wstring& workingDir,
             OutputCallback onOutput);

  // UTF-8 bytes, written whole or not at all.  The caller adds its own record
  // separator; this does not guess at one.
  bool Write(std::string_view bytes);

  // EOF on the child's standard input.  Deliberately separate from Wait() and
  // from the destructor, because for a child that treats its input as a live
  // channel -- Claude's control protocol does -- closing it early is not a
  // tidy-up but a fault, and one that arrives disguised as something else.
  void CloseInput();

  // Returns false on timeout.  INFINITE is allowed.
  bool Wait(DWORD milliseconds);
  void Terminate();

  bool running() const;
  DWORD exitCode() const { return exitCode_; }

  // Wraps one argument for the Windows command line parser: quotes when the
  // argument holds a space, and escapes embedded quotes and the backslashes
  // in front of them, which is the one rule everybody gets wrong.
  static std::wstring Quote(const std::wstring& argument);

 private:
  void ReadLoop();
  void CloseHandles();

  HANDLE process_ = nullptr;
  // The child and everything it starts, in a job that dies with us.  Null when
  // the job could not be made, which costs the guarantee and nothing else.
  HANDLE job_ = nullptr;
  HANDLE inputWrite_ = nullptr;   // our end of the child's stdin
  HANDLE outputRead_ = nullptr;   // our end of the child's stdout
  std::thread reader_;
  OutputCallback onOutput_;
  std::atomic<bool> stopping_{false};
  DWORD exitCode_ = 0;
};

}  // namespace win

#endif
