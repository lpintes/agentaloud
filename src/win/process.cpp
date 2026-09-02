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

}  // namespace

Process::~Process() {
  stopping_ = true;
  CloseInput();
  if (running()) Terminate();
  // Closing the read end unblocks a ReadFile that is still waiting.
  Close(&outputRead_);
  if (reader_.joinable()) reader_.join();
  Close(&process_);
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
                    const std::wstring& workingDir, OutputCallback onOutput) {
  HANDLE inputRead = nullptr;
  HANDLE outputWrite = nullptr;
  if (!MakePipe(&inputRead, &inputWrite_, false)) return false;
  if (!MakePipe(&outputRead_, &outputWrite, true)) {
    Close(&inputRead);
    Close(&inputWrite_);
    return false;
  }

  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = inputRead;
  startup.hStdOutput = outputWrite;
  // The child's stderr goes to ours.  Claude puts diagnostics there and
  // nothing that belongs to the protocol, so letting it through to the console
  // is more useful than swallowing it.
  startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  // CreateProcessW may write into the command line, so it cannot be a literal.
  std::vector<wchar_t> mutableLine(commandLine.begin(), commandLine.end());
  mutableLine.push_back(L'\0');

  PROCESS_INFORMATION info = {};
  const BOOL started = CreateProcessW(
      nullptr, mutableLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
      nullptr, workingDir.empty() ? nullptr : workingDir.c_str(), &startup,
      &info);

  // The child owns its ends now; ours have to go or the pipes never break.
  Close(&inputRead);
  Close(&outputWrite);

  if (!started) {
    Close(&inputWrite_);
    Close(&outputRead_);
    return false;
  }
  CloseHandle(info.hThread);
  process_ = info.hProcess;
  onOutput_ = std::move(onOutput);
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
