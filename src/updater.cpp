#include "updater.h"

#include <bcrypt.h>
#include <commctrl.h>
#include <shellapi.h>
#include <winhttp.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app_name.h"
#include "i18n/i18n.h"
#include "update.h"
#include "version.h"

namespace fs = std::filesystem;

namespace updater {

using i18n::Str;

namespace {

constexpr wchar_t kNewFolder[] = L".update-new";
constexpr wchar_t kOldFolder[] = L".update-old";
constexpr wchar_t kTitle[] = L"" APP_NAME;

class Internet {
 public:
  explicit Internet(HINTERNET handle = nullptr) : handle_(handle) {}
  ~Internet() {
    if (handle_) WinHttpCloseHandle(handle_);
  }
  Internet(const Internet&) = delete;
  Internet& operator=(const Internet&) = delete;
  HINTERNET get() const { return handle_; }
  explicit operator bool() const { return handle_ != nullptr; }

 private:
  HINTERNET handle_;
};

// What went wrong, in words.  The common cases are named: the raw system text
// for them is English and says nothing a reader can act on.
std::wstring NetworkError(DWORD code) {
  switch (code) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
      return i18n::Text(Str::kNetNotResolved);
    case ERROR_WINHTTP_TIMEOUT:
      return i18n::Text(Str::kNetTimeout);
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
      return i18n::Text(Str::kNetCannotConnect);
    case ERROR_WINHTTP_SECURE_FAILURE:
      return i18n::Text(Str::kNetSecureFailure);
    default:
      return i18n::Format(Str::kNetError, {std::to_wstring(code)});
  }
}

struct Response {
  DWORD status = 0;
  std::wstring location;
  std::string body;
};

// Shared by a download and whoever watches it: the progress dialog reads the
// counters on its timer, and sets `cancel` when the reader gives up.
struct Progress {
  std::atomic<std::uint64_t> done{0};
  std::atomic<std::uint64_t> total{0};
  std::atomic<bool> cancel{false};
};

// One GET to github.com.  Redirects are followed for downloads -- the asset
// URL redirects to GitHub's storage host -- and not for releases/latest, whose
// redirect *is* the answer.
bool Get(const std::wstring& path, bool followRedirects, DWORD timeoutMs,
         Progress* progress, Response& out, std::wstring& error) {
  const std::wstring agent = L"" APP_NAME L"/" + std::wstring(version::Current());
  Internet session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) {
    error = NetworkError(GetLastError());
    return false;
  }
  if (timeoutMs != 0) {
    const int ms = static_cast<int>(timeoutMs);
    WinHttpSetTimeouts(session.get(), ms, ms, ms, ms);
  }
  Internet connection(WinHttpConnect(session.get(), update::kHost,
                                     INTERNET_DEFAULT_HTTPS_PORT, 0));
  if (!connection) {
    error = NetworkError(GetLastError());
    return false;
  }
  Internet request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(),
                                      nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE));
  if (!request) {
    error = NetworkError(GetLastError());
    return false;
  }
  if (!followRedirects) {
    DWORD option = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &option,
                     sizeof(option));
  }
  if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr)) {
    error = NetworkError(GetLastError());
    return false;
  }

  DWORD size = sizeof(out.status);
  WinHttpQueryHeaders(request.get(),
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &out.status, &size,
                      WINHTTP_NO_HEADER_INDEX);
  size = 0;
  WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_LOCATION,
                      WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                      WINHTTP_NO_HEADER_INDEX);
  if (size > 0) {
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
    if (WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_LOCATION,
                            WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &size,
                            WINHTTP_NO_HEADER_INDEX)) {
      out.location.assign(buffer.data(), size / sizeof(wchar_t));
    }
  }
  if (!followRedirects) return true;

  if (progress) {
    DWORD length = 0;
    size = sizeof(length);
    if (WinHttpQueryHeaders(request.get(),
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &length, &size,
                            WINHTTP_NO_HEADER_INDEX)) {
      progress->total = length;
    }
  }
  for (;;) {
    if (progress && progress->cancel) return false;
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request.get(), &available)) {
      error = NetworkError(GetLastError());
      return false;
    }
    if (available == 0) break;
    const size_t at = out.body.size();
    out.body.resize(at + available);
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), out.body.data() + at, available, &read)) {
      error = NetworkError(GetLastError());
      return false;
    }
    out.body.resize(at + read);
    if (progress) progress->done = out.body.size();
  }
  return true;
}

// Lower-case hex SHA-256, or nothing if BCrypt refused.
std::optional<std::string> Sha256(const std::string& data) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr,
                                  0) < 0) {
    return std::nullopt;
  }
  unsigned char digest[32] = {};
  BCRYPT_HASH_HANDLE hash = nullptr;
  bool ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
  ok = ok && BCryptHashData(hash, reinterpret_cast<PUCHAR>(
                                      const_cast<char*>(data.data())),
                            static_cast<ULONG>(data.size()), 0) >= 0;
  ok = ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (!ok) return std::nullopt;
  static const char kHex[] = "0123456789abcdef";
  std::string text;
  for (unsigned char byte : digest) {
    text += kHex[byte >> 4];
    text += kHex[byte & 15];
  }
  return text;
}

std::wstring ThisExe() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                            static_cast<DWORD>(buffer.size()));
    if (length == 0) return {};
    if (length < buffer.size()) {
      buffer.resize(length);
      return buffer;
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::wstring Bare(const std::wstring& tag) {
  return tag.starts_with(L'v') ? tag.substr(1) : tag;
}

std::wstring AssetPath(const std::wstring& tag, const wchar_t* asset) {
  // kLatestPath is ".../releases/latest"; the download lives beside it.
  std::wstring path = update::kLatestPath;
  path.resize(path.size() - std::wcslen(L"latest"));
  return path + L"download/" + tag + L"/" + asset;
}

void OpenReleasePage() {
  ShellExecuteW(nullptr, L"open", update::kReleasePage, nullptr, nullptr,
                SW_SHOWNORMAL);
}

bool WriteAll(const fs::path& path, const std::string& bytes) {
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                            &written, nullptr) &&
                  written == bytes.size() && FlushFileBuffers(file);
  CloseHandle(file);
  return ok;
}

// A move within the volume, which is what lets a running EXE or a loaded DLL
// leave its place.  No MOVEFILE_COPY_ALLOWED: a copy would not be atomic, and
// a copy of a running image would not free its name anyway.
bool Move(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// The download runs on its own thread and the dialog only watches it: a
// TaskDialog runs its own message loop, and the network must not stall it.
// The state is shared, so a cancelled download can finish or fail on its own
// time without touching anything that is gone.
struct Download {
  std::wstring tag;
  std::wstring dir;      // the application's folder
  std::wstring exeName;  // "agentaloud.exe", whatever the file is called now
  bool restart = false;
  Progress progress;
  std::atomic<bool> finished{false};
  bool ok = false;  // written before `finished`, read after it
  std::string package;
  std::string sums;
  std::wstring error;
  // Settled on the dialog's thread once the download is in, before the
  // dialog closes; see Finish.
  enum class Outcome { kPending, kInstalled, kRestarted, kNotWritable, kFailed };
  Outcome outcome = Outcome::kPending;
  std::wstring restartError;
};

bool StartNew(const std::wstring& exe, std::wstring& error);

// Checks the download, unpacks it, swaps the files and, when asked, starts the
// new EXE -- all while the progress dialog is still up.  That is the point of
// doing it here and not after the dialog returns: Windows lets a new window
// take the focus only if whoever started it had the focus at that moment, and
// once the dialog is gone this process has no window at all.  Started
// afterwards, the new window comes up behind whatever was there before.
//
// On the dialog's thread, not the download's, so a Zrušiť pressed during the
// download can never race an install already under way.
void Finish(Download& job) {
  if (!job.ok) {
    job.outcome = Download::Outcome::kFailed;
    return;
  }
  // The asset name is ASCII, so narrowing it is a copy.
  const std::wstring_view wide = update::kPackageAsset;
  const std::string name(wide.begin(), wide.end());
  const std::optional<std::string> expected = update::HashFor(job.sums, name);
  const std::optional<std::string> actual = Sha256(job.package);
  if (!expected) {
    job.error = i18n::Text(Str::kNoChecksum);
    job.outcome = Download::Outcome::kFailed;
    return;
  }
  if (!actual || *actual != *expected) {
    job.error = i18n::Text(Str::kChecksumMismatch);
    job.outcome = Download::Outcome::kFailed;
    return;
  }
  job.error = Unpack(job.dir, job.package);
  if (!job.error.empty()) {
    job.outcome = Download::Outcome::kFailed;
    return;
  }
  switch (Install(job.dir, job.exeName, job.error)) {
    case Written::kOk:
      break;
    case Written::kNotWritable:
      job.outcome = Download::Outcome::kNotWritable;
      return;
    case Written::kFailed:
      job.outcome = Download::Outcome::kFailed;
      return;
  }
  job.outcome =
      job.restart && StartNew(job.dir + L"\\" + job.exeName, job.restartError)
          ? Download::Outcome::kRestarted
          : Download::Outcome::kInstalled;
}

void RunDownload(const std::shared_ptr<Download>& job) {
  Response package;
  Response sums;
  std::wstring error;
  bool ok = Get(AssetPath(job->tag, update::kSumsAsset), true, 0, nullptr, sums,
                error) &&
            Get(AssetPath(job->tag, update::kPackageAsset), true, 0,
                &job->progress, package, error);
  if (ok && (sums.status != 200 || package.status != 200)) {
    error = i18n::Format(
        Str::kServerStatus,
        {std::to_wstring(package.status != 200 ? package.status : sums.status)});
    ok = false;
  }
  job->package = std::move(package.body);
  job->sums = std::move(sums.body);
  job->error = std::move(error);
  job->ok = ok;
  job->finished = true;
}

HRESULT CALLBACK ProgressCallback(HWND dialog, UINT notification, WPARAM wParam,
                                  LPARAM, LONG_PTR data) {
  auto* job = reinterpret_cast<Download*>(data);
  switch (notification) {
    case TDN_TIMER: {
      const std::uint64_t total = job->progress.total;
      if (total > 0) {
        const auto percent = static_cast<WPARAM>(job->progress.done * 100 / total);
        SendMessageW(dialog, TDM_SET_PROGRESS_BAR_POS, percent, 0);
      }
      if (job->finished && job->outcome == Download::Outcome::kPending) {
        if (!job->progress.cancel) Finish(*job);
        SendMessageW(dialog, TDM_CLICK_BUTTON, IDCANCEL, 0);
      }
      break;
    }
    case TDN_BUTTON_CLICKED:
      // The same button closes the dialog when the download ends; only a click
      // before that is the reader giving up.
      if (wParam == IDCANCEL && !job->finished) job->progress.cancel = true;
      break;
    default:
      break;
  }
  return S_OK;
}

// Starts exe with this run's command line, so the folder, --backend and the
// rest carry over.  The new process may take the focus: we hand it our right
// to, while we still have it (see Finish).
bool StartNew(const std::wstring& exe, std::wstring& error) {
  std::wstring commandLine = GetCommandLineW();
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                      0, nullptr, nullptr, &startup, &process)) {
    error = i18n::Format(Str::kRestartFailed, {std::to_wstring(GetLastError())});
    return false;
  }
  AllowSetForegroundWindow(process.dwProcessId);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

}  // namespace

// The system's tar.exe, by full path: whatever "tar" is first on PATH -- msys,
// Git for Windows -- is a different program with different ideas about paths.
//
// It only sees the ANSI code page, so no path goes on its command line.  The
// folder it unpacks into is handed over as the process's current directory,
// which CreateProcessW takes as Unicode, and the package is saved there under
// an ASCII name.  A profile path with letters outside the code page -- Cyrillic
// on a Central European system -- would otherwise turn into "??" and fail.
//
// Only the exit code counts.  A truncated package was measured to leave a file
// of the right size and the wrong contents behind with exit code 1, so the
// folder is fresh every time and nothing in it is trusted unless tar said 0.
std::wstring Unpack(const std::wstring& dir, const std::string& zip) {
  const fs::path target = fs::path(dir) / kNewFolder;
  std::error_code ec;
  fs::remove_all(target, ec);
  if (!fs::create_directories(target, ec)) {
    return i18n::Text(Str::kUnpackNoFolder);
  }
  const fs::path package = target / L"package.zip";
  if (!WriteAll(package, zip)) return i18n::Text(Str::kUnpackCannotWrite);

  wchar_t system[MAX_PATH] = {};
  GetSystemDirectoryW(system, MAX_PATH);
  const std::wstring tar = std::wstring(system) + L"\\tar.exe";
  std::wstring commandLine = L"tar.exe -xf package.zip";
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  if (!CreateProcessW(tar.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, target.c_str(), &startup,
                      &process)) {
    return i18n::Format(Str::kUnpackTarFailed,
                        {tar, std::to_wstring(GetLastError())});
  }
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  fs::remove(package, ec);
  if (code != 0) {
    return i18n::Format(Str::kUnpackTarExit, {std::to_wstring(code)});
  }
  return {};
}

Written Install(const std::wstring& dir, const std::wstring& exeName,
                std::wstring& error) {
  const fs::path root(dir);
  const fs::path fresh = root / kNewFolder;
  const fs::path old = root / kOldFolder;

  std::vector<std::wstring> entries;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(fresh, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_regular_file(ec)) {
      entries.push_back(fs::relative(it->path(), fresh, ec).wstring());
    }
  }
  const update::Plan plan = update::PlanReplace(entries, exeName);
  if (!plan.error.empty()) {
    error = plan.error + L".";
    return Written::kFailed;
  }

  fs::remove_all(old, ec);
  // What has been done, in order, so a failure can undo it: whether the file
  // had a predecessor that went to .update-old, and whether the new one is in
  // place.
  struct Step {
    fs::path target;
    fs::path parked;
    fs::path source;  // where it came from in .update-new
    bool movedOld = false;
    bool placed = false;
  };
  std::vector<Step> steps;
  DWORD failure = 0;
  for (const update::Plan::File& file : plan.files) {
    Step step;
    step.target = root / file.to;
    step.parked = old / file.to;
    step.source = fresh / file.from;
    if (fs::exists(step.target, ec)) {
      if (!Move(step.target, step.parked)) {
        failure = GetLastError();
        steps.push_back(step);
        break;
      }
      step.movedOld = true;
    }
    if (!Move(step.source, step.target)) {
      failure = GetLastError();
      steps.push_back(step);
      break;
    }
    step.placed = true;
    steps.push_back(step);
  }
  if (failure == 0) return Written::kOk;

  // Undone newest first: the folder ends as it was, with every old file back
  // under its own name.  The new files go back where they came from rather
  // than being deleted, which keeps an undo that itself fails from losing
  // anything.
  for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
    if (step->placed) {
      Move(step->target, step->source);
    }
    if (step->movedOld) Move(step->parked, step->target);
  }
  if (failure == ERROR_ACCESS_DENIED) return Written::kNotWritable;
  // Measured: a file another program holds open without FILE_SHARE_DELETE --
  // an editor, an antivirus scan -- gives this, and the number alone would say
  // nothing.  Trying again later is the whole remedy, and nothing has changed.
  if (failure == ERROR_SHARING_VIOLATION) {
    error = i18n::Format(Str::kFileInUse, {L"" APP_NAME});
    return Written::kFailed;
  }
  error = i18n::Format(Str::kFileCannotReplace, {std::to_wstring(failure)});
  return Written::kFailed;
}

Latest FetchLatest(DWORD timeoutMs) {
  Latest result;
  Response response;
  if (!Get(update::kLatestPath, false, timeoutMs, nullptr, response,
           result.error)) {
    return result;
  }
  if (response.status < 300 || response.status >= 400) {
    // 404 is what a repository with no release -- or a private one -- gives;
    // neither is an error worth a word.
    if (response.status == 404) {
      result.kind = Latest::Kind::kNoRelease;
    } else {
      result.error =
          i18n::Format(Str::kServerStatus, {std::to_wstring(response.status)});
    }
    return result;
  }
  if (const auto tag = update::TagFromLocation(response.location)) {
    result.kind = Latest::Kind::kFound;
    result.tag = *tag;
  } else {
    result.kind = Latest::Kind::kNoRelease;
  }
  return result;
}

Latest FetchLatestWithin(DWORD milliseconds) {
  // A promise and not std::async: the future std::async hands back blocks in
  // its destructor until the work is done, which is the wait this avoids.
  auto answer = std::make_shared<std::promise<Latest>>();
  std::future<Latest> future = answer->get_future();
  std::thread([answer, milliseconds] {
    answer->set_value(FetchLatest(milliseconds));
  }).detach();
  if (future.wait_for(std::chrono::milliseconds(milliseconds)) !=
      std::future_status::ready) {
    Latest late;
    late.error = i18n::Text(Str::kNetTimeout);
    return late;
  }
  return future.get();
}

Latest FetchLatestAsked(HWND owner) {
  // Shared with the worker, which may outlive the dialog if the reader cancels.
  struct Asked {
    std::atomic<bool> finished{false};
    Latest latest;  // written before `finished`
  };
  auto job = std::make_shared<Asked>();
  std::thread([job] {
    job->latest = FetchLatest(0);
    job->finished = true;
  }).detach();

  TASKDIALOGCONFIG config = {};
  config.cbSize = sizeof(config);
  config.hwndParent = owner;
  config.dwFlags = TDF_SHOW_MARQUEE_PROGRESS_BAR | TDF_CALLBACK_TIMER |
                   TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
  config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
  config.pszWindowTitle = kTitle;
  config.pszMainInstruction = i18n::Text(Str::kCheckingForUpdate);
  config.lpCallbackData = reinterpret_cast<LONG_PTR>(job.get());
  config.pfCallback = [](HWND dialog, UINT notification, WPARAM, LPARAM,
                         LONG_PTR data) -> HRESULT {
    auto* asked = reinterpret_cast<Asked*>(data);
    if (notification == TDN_CREATED) {
      SendMessageW(dialog, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 0);
    }
    if (notification == TDN_TIMER && asked->finished) {
      SendMessageW(dialog, TDM_CLICK_BUTTON, IDCANCEL, 0);
    }
    return S_OK;
  };
  TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
  // The button that closes it is the same either way; what tells the two
  // apart is whether the answer was in.
  if (!job->finished) {
    Latest cancelled;
    cancelled.kind = Latest::Kind::kCancelled;
    return cancelled;
  }
  return job->latest;
}

Choice AskToUpdate(HWND owner, const std::wstring& tag, bool restartsItself) {
  constexpr int kUpdate = 100;
  constexpr int kLater = 101;
  constexpr int kSkip = 102;
  const TASKDIALOG_BUTTON buttons[] = {
      {kUpdate, i18n::Text(Str::kUpdateButton)},
      {kLater, i18n::Text(Str::kLaterButton)},
      {kSkip, i18n::Text(Str::kSkipButton)},
  };
  const std::wstring instruction =
      i18n::Format(Str::kVersionAvailable, {Bare(tag)});
  std::wstring content =
      i18n::Format(Str::kYouHaveVersion, {version::Current()});
  if (restartsItself) {
    content += L" " + i18n::Format(Str::kRestartsAfterUpdate, {L"" APP_NAME});
  }
  std::wstring page = update::kReleasePage;
  page.resize(page.size() - std::wcslen(L"latest"));
  const std::wstring footer = L"<a href=\"" + page + L"tag/" + tag + L"\">" +
                              i18n::Format(Str::kWhatsNew, {Bare(tag)}) +
                              L"</a>";

  TASKDIALOGCONFIG config = {};
  config.cbSize = sizeof(config);
  config.hwndParent = owner;
  config.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION |
                   TDF_POSITION_RELATIVE_TO_WINDOW;
  config.pszWindowTitle = kTitle;
  config.pszMainInstruction = instruction.c_str();
  config.pszContent = content.c_str();
  config.pszFooter = footer.c_str();
  config.cButtons = ARRAYSIZE(buttons);
  config.pButtons = buttons;
  config.nDefaultButton = kUpdate;
  config.pfCallback = [](HWND, UINT notification, WPARAM, LPARAM lParam,
                         LONG_PTR) -> HRESULT {
    if (notification == TDN_HYPERLINK_CLICKED) {
      ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(lParam), nullptr,
                    nullptr, SW_SHOWNORMAL);
    }
    return S_OK;
  };
  int pressed = kLater;
  if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr))) {
    return Choice::kLater;
  }
  if (pressed == kUpdate) return Choice::kUpdate;
  if (pressed == kSkip) return Choice::kSkip;
  return Choice::kLater;
}

Installed DownloadAndInstall(HWND owner, const std::wstring& tag, bool restart) {
  auto job = std::make_shared<Download>();
  job->tag = tag;
  const fs::path exe(ThisExe());
  job->dir = exe.parent_path().wstring();
  job->exeName = exe.filename().wstring();
  job->restart = restart;
  std::thread([job] { RunDownload(job); }).detach();

  const std::wstring instruction = i18n::Format(Str::kDownloading, {Bare(tag)});
  TASKDIALOGCONFIG config = {};
  config.cbSize = sizeof(config);
  config.hwndParent = owner;
  config.dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER |
                   TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
  config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
  config.pszWindowTitle = kTitle;
  config.pszMainInstruction = instruction.c_str();
  config.pfCallback = ProgressCallback;
  config.lpCallbackData = reinterpret_cast<LONG_PTR>(job.get());
  TaskDialogIndirect(&config, nullptr, nullptr, nullptr);

  // Still pending means the dialog closed before the download did: cancelled.
  switch (job->outcome) {
    case Download::Outcome::kPending:
      return Installed::kCancelled;
    case Download::Outcome::kRestarted:
      return Installed::kRestarted;
    case Download::Outcome::kInstalled:
      if (restart) {
        MessageBoxW(owner,
                    i18n::Format(Str::kInstalledButNotStarted,
                                 {job->restartError})
                        .c_str(),
                    kTitle, MB_OK | MB_ICONWARNING);
      }
      return Installed::kInstalled;
    case Download::Outcome::kNotWritable: {
      const std::wstring question =
          i18n::Format(Str::kFolderNotWritable, {job->dir, L"" APP_NAME});
      if (MessageBoxW(owner, question.c_str(), kTitle,
                      MB_YESNO | MB_ICONWARNING) == IDYES) {
        OpenReleasePage();
      }
      return Installed::kFailed;
    }
    case Download::Outcome::kFailed:
      break;
  }
  MessageBoxW(owner,
              i18n::Format(Str::kUpdateFailed, {job->error}).c_str(), kTitle,
              MB_OK | MB_ICONWARNING);
  return Installed::kFailed;
}

void RemoveLeftover() {
  const fs::path dir = fs::path(ThisExe()).parent_path();
  if (dir.empty()) return;
  std::error_code ec;
  fs::remove_all(dir / kOldFolder, ec);
  fs::remove_all(dir / kNewFolder, ec);
}

std::wstring Today() {
  SYSTEMTIME now = {};
  GetLocalTime(&now);
  wchar_t text[16] = {};
  std::swprintf(text, 16, L"%04u-%02u-%02u", now.wYear, now.wMonth, now.wDay);
  return text;
}

}  // namespace updater
