#include "win/process.h"

#include <vector>

namespace win {
namespace {

// Both ends of both pipes are created inheritable, because CreateProcess can
// only pass a handle to the child if it is.  Our own ends are then marked back
// as non-inheritable: a handle we forgot to unmark would be held open by every
// grandchild the child spawns, and the read would never see EOF.
bool MakePipe(HANDLE* readEnd, HANDLE* writeEnd, bool keepReadEnd) {
  SECURITY_ATTRIBUTES attributes = {};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  if (!CreatePipe(readEnd, writeEnd, &attributes, 0)) return false;
  HANDLE ours = keepReadEnd ? *readEnd : *writeEnd;
  return SetHandleInformation(ours, HANDLE_FLAG_INHERIT, 0) != 0;
}

void Close(HANDLE* handle) {
  if (*handle) {
    CloseHandle(*handle);
    *handle = nullptr;
  }
}

// A job the child cannot outlive.
//
// KILL_ON_JOB_CLOSE means: when the last handle to this job goes, the kernel
// kills everything in it.  Our process holds that handle, so it goes when we
// do -- on a clean exit, on a crash, and on TerminateProcess, because the
// kernel closes a dead process's handles no matter how it died.  That last
// case is the whole point: no destructor, no atexit and no signal handler runs
// when somebody types "taskkill /F", and the child is a headless CLI holding
// half a gigabyte with nobody left to talk to.
//
// The handle must NOT be inheritable, and that is why the attributes are left
// null.  An inheritable one would be passed to the child by the CreateProcess
// below (it inherits handles, for the pipes), the child would then hold a
// handle to the job it is in, the last handle would never go, and the whole
// mechanism would do nothing at all -- silently, and only in the case it
// exists for.
HANDLE MakeKillOnCloseJob() {
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job == nullptr) return nullptr;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                               sizeof(limits))) {
    CloseHandle(job);
    return nullptr;
  }
  return job;
}

// How much of stderr is kept.  The head is where a CLI that refuses to start
// says why; the tail is where a crash says what it was doing.  What is between
// them in a long session is warnings nobody will read in a dialog anyway.
constexpr size_t kErrorHead = 4096;
constexpr size_t kErrorTail = 12288;

}  // namespace

Process::~Process() {
  stopping_ = true;
  CloseInput();
  if (running()) Terminate();
  // Closing the read end unblocks a ReadFile that is still waiting.
  Close(&outputRead_);
  if (reader_.joinable()) reader_.join();
  Close(&process_);
  // Last of the three, and it does more than tidy up: Terminate() above kills
  // the child we started, this kills whatever the child started.  On the
  // orderly path there is nothing left to kill, and that is what it should
  // look like.
  Close(&job_);
  // After the job, because everything that could still hold the write end of
  // stderr is dead by now and the read sees EOF.  Without a job a grandchild
  // may outlive us with it, and the read is cancelled instead.
  if (errorThread_) {
    CancelSynchronousIo(errorThread_);
    WaitForSingleObject(errorThread_, INFINITE);
  }
  Close(&errorRead_);
  Close(&errorThread_);
  Close(&errorDone_);
}

std::wstring Process::Quote(const std::wstring& argument) {
  if (!argument.empty() &&
      argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    return argument;
  }
  std::wstring quoted = L"\"";
  for (auto it = argument.begin();; ++it) {
    size_t backslashes = 0;
    while (it != argument.end() && *it == L'\\') {
      ++it;
      ++backslashes;
    }
    if (it == argument.end()) {
      // Backslashes before the closing quote are doubled, or the parser reads
      // the quote as escaped and swallows the end of the argument.
      quoted.append(backslashes * 2, L'\\');
      break;
    }
    if (*it == L'"') {
      quoted.append(backslashes * 2 + 1, L'\\');
    } else {
      quoted.append(backslashes, L'\\');
    }
    quoted.push_back(*it);
  }
  quoted.push_back(L'"');
  return quoted;
}

bool Process::Start(const std::wstring& commandLine,
                    const std::wstring& workingDir, OutputCallback onOutput,
                    ExitCallback onExit) {
  startError_ = 0;
  HANDLE inputRead = nullptr;
  HANDLE outputWrite = nullptr;
  HANDLE errorWrite = nullptr;
  auto fail = [&] {
    startError_ = GetLastError();
    Close(&inputRead);
    Close(&outputWrite);
    Close(&errorWrite);
    Close(&inputWrite_);
    Close(&outputRead_);
    Close(&errorRead_);
    Close(&errorDone_);
    Close(&job_);
    return false;
  };
  if (!MakePipe(&inputRead, &inputWrite_, false)) return fail();
  if (!MakePipe(&outputRead_, &outputWrite, true)) return fail();
  // The child's stderr is ours to read, not passed through to our own.  Ours
  // does not exist -- this is a -mwindows program -- and the CLI says there
  // exactly the things a user needs when a session will not start: an option
  // it refuses, a session id already in use (invariant 16).
  if (!MakePipe(&errorRead_, &errorWrite, true)) return fail();
  errorDone_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (errorDone_ == nullptr) return fail();

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = inputRead;
  startup.hStdOutput = outputWrite;
  startup.hStdError = errorWrite;

  // CreateProcessW may write into the command line, so it cannot be a literal.
  std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
  mutableLine.push_back(L'\0');

  // Suspended, so that the child cannot spawn anything before it is in the
  // job.  Not a theoretical race: claude.exe is a launcher and the process
  // that does the work is the node it starts, so a grandchild made in that
  // window would be outside the job and would survive us -- which is the exact
  // failure this is here to stop.  A job created without one is no job at all.
  job_ = MakeKillOnCloseJob();

  PROCESS_INFORMATION info = {};
  const BOOL started = CreateProcessW(
      nullptr, mutableLine.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW | (job_ ? CREATE_SUSPENDED : 0u), nullptr,
      workingDir.empty() ? nullptr : workingDir.c_str(), &startup, &info);

  if (!started) return fail();
  // The child owns its ends now; ours have to go or the pipes never break.
  Close(&inputRead);
  Close(&outputWrite);
  Close(&errorWrite);
  if (job_) {
    // Failure here is not fatal and is not treated as one: the session works,
    // it just loses the guarantee that the child dies with us.  Since Windows
    // 8 a process may be in several jobs at once, so being started from inside
    // somebody else's job -- a terminal, a debugger -- no longer makes this
    // fail the way it did on 7.
    if (!AssignProcessToJobObject(job_, info.hProcess)) Close(&job_);
    ResumeThread(info.hThread);
  }
  CloseHandle(info.hThread);
  process_ = info.hProcess;
  onOutput_ = std::move(onOutput);
  onExit_ = std::move(onExit);
  // Before the stdout reader, which waits for this one at the end.  Without
  // it the session still works, it only says nothing about why it ended --
  // and the event is set so that nobody waits for a reader that is not there.
  errorThread_ = CreateThread(nullptr, 0, &Process::ErrorThread, this, 0,
                              nullptr);
  if (errorThread_ == nullptr) SetEvent(errorDone_);
  reader_ = std::thread(&Process::ReadLoop, this);
  return true;
}

void Process::ReadLoop() {
  char buffer[8192];
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(outputRead_, buffer, sizeof(buffer), &read, nullptr)) break;
    if (read == 0) break;  // EOF: the child closed its end or exited
    if (onOutput_ && !stopping_) onOutput_(std::string_view(buffer, read));
  }
  if (!onExit_ || stopping_) return;

  // stdout ends when the child does, nearly always.  A child that closed it
  // and carried on is given a few seconds and then reported without a code,
  // rather than holding this thread -- and with it the destructor -- forever.
  Exit exit;
  if (WaitForSingleObject(process_, 5000) == WAIT_OBJECT_0 &&
      GetExitCodeProcess(process_, &exit.code)) {
    exit.codeKnown = true;
  }
  // The last words of a crash are written just before it, on the other pipe,
  // and may still be on their way.  Bounded for the same grandchild that may
  // hold that pipe open after the child is gone.
  WaitForSingleObject(errorDone_, 1000);
  exit.errorOutput = ErrorOutput();
  if (!stopping_) onExit_(exit);
}

DWORD WINAPI Process::ErrorThread(void* self) {
  static_cast<Process*>(self)->ErrorLoop();
  return 0;
}

void Process::ErrorLoop() {
  char buffer[4096];
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(errorRead_, buffer, sizeof(buffer), &read, nullptr)) break;
    if (read == 0) break;
    std::string_view bytes(buffer, read);
    std::lock_guard<std::mutex> lock(errorMutex_);
    const size_t room = kErrorHead - errorHead_.size();
    errorHead_.append(bytes.substr(0, room));
    if (bytes.size() > room) errorTail_.append(bytes.substr(room));
    if (errorTail_.size() > kErrorTail) {
      errorTail_.erase(0, errorTail_.size() - kErrorTail);
      errorDropped_ = true;
    }
  }
  SetEvent(errorDone_);
}

std::string Process::ErrorOutput() const {
  std::lock_guard<std::mutex> lock(errorMutex_);
  if (!errorDropped_) return errorHead_ + errorTail_;
  // The cut may have landed inside a UTF-8 sequence; its continuation bytes
  // would decode as a replacement character the reader hears for nothing.
  size_t start = 0;
  while (start < errorTail_.size() &&
         (static_cast<unsigned char>(errorTail_[start]) & 0xC0) == 0x80) {
    ++start;
  }
  return errorHead_ + "\n\xE2\x80\xA6\n" + errorTail_.substr(start);
}

bool Process::Write(std::string_view bytes) {
  if (!inputWrite_) return false;
  size_t written = 0;
  while (written < bytes.size()) {
    DWORD chunk = 0;
    if (!WriteFile(inputWrite_, bytes.data() + written,
                   static_cast<DWORD>(bytes.size() - written), &chunk,
                   nullptr) ||
        chunk == 0) {
      return false;
    }
    written += chunk;
  }
  return true;
}

void Process::CloseInput() { Close(&inputWrite_); }

bool Process::Wait(DWORD milliseconds) {
  if (!process_) return true;
  if (WaitForSingleObject(process_, milliseconds) != WAIT_OBJECT_0) {
    return false;
  }
  GetExitCodeProcess(process_, &exitCode_);
  return true;
}

void Process::Terminate() {
  if (process_) TerminateProcess(process_, 1);
}

bool Process::running() const {
  if (!process_) return false;
  return WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

}  // namespace win
