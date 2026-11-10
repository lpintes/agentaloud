#include "ui/speech.h"

#include <prism.h>
#include <tlhelp32.h>

#include <string>
#include <thread>

namespace ui {

namespace {

// prism's screen readers on Windows, as of 0.18.3.  Listed by hand because
// its own flag for that (IS_SCREEN_READER) came in a later release, and an
// unknown backend left out is a reader that stays quiet, while one let in
// could be SAPI talking over a reader prism does not know.  NVDA is not here:
// it has a path of its own, and prism would be asked only when that path has
// already found NVDA is not running.
constexpr PrismBackendId kReaders[] = {
    PRISM_BACKEND_JAWS,          PRISM_BACKEND_UIA,
    PRISM_BACKEND_ZDSR,          PRISM_BACKEND_ZOOM_TEXT,
    PRISM_BACKEND_PC_TALKER,     PRISM_BACKEND_BOY_PC_READER,
    PRISM_BACKEND_SENSE_READER,  PRISM_BACKEND_SYSTEM_ACCESS,
    PRISM_BACKEND_WINDOW_EYES,
};

bool IsReader(PrismBackendId id) {
  for (const PrismBackendId reader : kReaders) {
    if (reader == id) return true;
  }
  return false;
}

// Whether Narrator is running.  prism's UIA backend asks the system's screen
// reader flag (SPI_GETSCREENREADER) for that, and Narrator does not set it --
// measured 2026-11-10 on Windows 11: the flag stayed 0 the whole time
// Narrator was speaking, so prism never offered UIA and every Announce beeped.
// Its speak and initialize do not look at the flag, so asking for the process
// here is enough.  A snapshot of every process is not cheap and available()
// is asked before nearly every sentence, so the answer is kept for a second.
bool NarratorRunning() {
  static bool running = false;
  static ULONGLONG checked = 0;
  static bool once = false;
  const ULONGLONG now = GetTickCount64();
  if (once && now - checked < 1000) return running;
  once = true;
  checked = now;
  running = false;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return running;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL more = Process32FirstW(snapshot, &entry); more;
       more = Process32NextW(snapshot, &entry)) {
    if (_wcsicmp(entry.szExeFile, L"Narrator.exe") == 0) {
      running = true;
      break;
    }
  }
  CloseHandle(snapshot);
  return running;
}

// One for the process, not one per Speech: every SessionPane has its own
// Speech, and the UIA backend (Narrator) keeps a hidden window and a thread
// per instance.  Only ever touched from the window thread -- prism's backends
// are not safe to share between threads, and nothing here needs them to be.
class Prism {
 public:
  void Load() {
    if (tried_) return;
    tried_ = true;
    library_ = LoadLibraryW(L"prism.dll");
    if (!library_) return;
    const auto configInit = Get<decltype(&prism_config_init)>("prism_config_init");
    const auto init = Get<decltype(&prism_init)>("prism_init");
    count_ = Get<decltype(&prism_registry_count)>("prism_registry_count");
    idAt_ = Get<decltype(&prism_registry_id_at)>("prism_registry_id_at");
    acquire_ = Get<decltype(&prism_registry_acquire)>("prism_registry_acquire");
    features_ = Get<decltype(&prism_backend_get_features)>("prism_backend_get_features");
    initialize_ = Get<decltype(&prism_backend_initialize)>("prism_backend_initialize");
    speak_ = Get<decltype(&prism_backend_speak)>("prism_backend_speak");
    stop_ = Get<decltype(&prism_backend_stop)>("prism_backend_stop");
    if (!configInit || !init || !count_ || !idAt_ || !acquire_ || !features_ ||
        !initialize_ || !speak_ || !stop_) {
      return;
    }
    // No availability callback, so prism starts no thread of its own to
    // watch for readers; Reader() asks when it needs to know.
    PrismConfig config = configInit();
    context_ = init(&config);
  }

  // The reader to speak through right now, or nullptr.  The one in use is
  // asked every time whether it is still running, like NVDA is; looking for
  // another goes through every backend, so it is done at most every couple of
  // seconds -- on a machine with no reader at all every keypress would ask.
  PrismBackend* Reader() {
    if (!context_) return nullptr;
    if (current_ && Running(current_, currentId_)) return current_;
    current_ = nullptr;
    const ULONGLONG now = GetTickCount64();
    if (scanned_ && now - lastScan_ < kRescanMs) return nullptr;
    scanned_ = true;
    lastScan_ = now;
    // The registry is in prism's order of preference, best first.
    const size_t count = count_(context_);
    for (size_t i = 0; i < count; ++i) {
      const PrismBackendId id = idAt_(context_, i);
      if (!IsReader(id)) continue;
      PrismBackend* backend = acquire_(context_, id);
      if (!backend || !Running(backend, id)) continue;
      // Here and not in Load: UIA attaches to a visible top-level window of
      // this process, which does not exist yet when the first Speech opens.
      // A failure is not remembered -- the next scan tries again.
      const PrismError result = initialize_(backend);
      if (result != PRISM_OK && result != PRISM_ERROR_ALREADY_INITIALIZED) continue;
      current_ = backend;
      currentId_ = id;
      return current_;
    }
    return nullptr;
  }

  // A reader that refuses is forgotten, so the next call looks again rather
  // than talking to it for the rest of the session -- UIA's hidden window, for
  // one, can be gone while the backend still reports Narrator running.
  void Speak(PrismBackend* backend, const std::wstring& text, bool interrupt) {
    const std::string utf8 = ToUtf8(text);
    if (utf8.empty()) return;
    if (speak_(backend, utf8.c_str(), interrupt) != PRISM_OK) current_ = nullptr;
  }

  void Stop(PrismBackend* backend) {
    if (stop_(backend) != PRISM_OK) current_ = nullptr;
  }

 private:
  static constexpr ULONGLONG kRescanMs = 2000;

  template <typename F>
  F Get(const char* name) {
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(library_, name)));
  }

  // Computed live by prism: for JAWS a window of its, for UIA the system's
  // screen reader flag and UI Automation clients listening -- which leaves
  // out Narrator itself, hence the second question.
  bool Running(PrismBackend* backend, PrismBackendId id) const {
    if ((features_(backend) & PRISM_BACKEND_IS_SUPPORTED_AT_RUNTIME) != 0) return true;
    return id == PRISM_BACKEND_UIA && NarratorRunning();
  }

  static std::string ToUtf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
  }

  bool tried_ = false;
  HMODULE library_ = nullptr;
  PrismContext* context_ = nullptr;
  PrismBackend* current_ = nullptr;
  PrismBackendId currentId_ = PRISM_BACKEND_INVALID;
  bool scanned_ = false;
  ULONGLONG lastScan_ = 0;
  decltype(&prism_registry_count) count_ = nullptr;
  decltype(&prism_registry_id_at) idAt_ = nullptr;
  decltype(&prism_registry_acquire) acquire_ = nullptr;
  decltype(&prism_backend_get_features) features_ = nullptr;
  decltype(&prism_backend_initialize) initialize_ = nullptr;
  decltype(&prism_backend_speak) speak_ = nullptr;
  decltype(&prism_backend_stop) stop_ = nullptr;
};

Prism& OtherReaders() {
  static Prism prism;
  return prism;
}

}  // namespace

bool Speech::available() const {
  return (speak_ && Running()) || OtherReaders().Reader() != nullptr;
}

bool Speech::Open() {
  OtherReaders().Load();
  if (library_) return available();

  // Beside the executable first, because that is the copy shipped with the
  // application and known to match this code; then the search path, so a
  // machine that already has one is not made to carry two.
  library_ = LoadLibraryW(L"nvdaControllerClient.dll");
  if (!library_) return available();

  speak_ = reinterpret_cast<SpeakText>(
      reinterpret_cast<void*>(GetProcAddress(library_, "nvdaController_speakText")));
  cancel_ = reinterpret_cast<CancelSpeech>(
      reinterpret_cast<void*>(GetProcAddress(library_, "nvdaController_cancelSpeech")));
  test_ = reinterpret_cast<TestIfRunning>(
      reinterpret_cast<void*>(GetProcAddress(library_, "nvdaController_testIfRunning")));
  speakSsml_ = reinterpret_cast<SpeakSsml>(
      reinterpret_cast<void*>(GetProcAddress(library_, "nvdaController_speakSsml")));
  return available();
}

bool Speech::Running() const {
  // Asked every time rather than once at startup: NVDA gets restarted, and an
  // application that decided at launch that there is no reader would stay
  // silent for the rest of the day.
  return test_ && test_() == 0;
}

void Speech::Say(const std::wstring& text, bool interrupt) {
  if (text.empty()) return;
  if (!speak_ || !Running()) {
    // prism's interrupt is the reader's own: JAWS and Narrator are told to
    // stop, as NVDA is by cancelSpeech.
    if (PrismBackend* reader = OtherReaders().Reader()) {
      OtherReaders().Speak(reader, text, interrupt);
    }
    return;
  }
  if (interrupt) Silence();
  speak_(text.c_str());
}

void Speech::SayThen(const std::wstring& text, void (*then)()) {
  if (text.empty() || !speak_ || !Running()) {
    // No reader prism knows reports when it has finished speaking, so the
    // tone comes at once, as with an NVDA too old for SSML.
    Say(text, false);
    then();
    return;
  }
  if (!speakSsml_) {
    speak_(text.c_str());
    then();
    return;
  }
  std::wstring ssml = L"<speak>";
  for (const wchar_t c : text) {
    if (c == L'&') {
      ssml += L"&amp;";
    } else if (c == L'<') {
      ssml += L"&lt;";
    } else if (c == L'>') {
      ssml += L"&gt;";
    } else {
      ssml += c;
    }
  }
  ssml += L"</speak>";
  // Synchronous, on a thread of its own: only then does NVDA say when it is
  // done -- read in NVDA's source (NVDAHelper/__init__.py), where the
  // asynchronous call returns as soon as the text is queued and the marks
  // are never reported.  The call returns after the text is spoken, or with
  // ERROR_CANCELLED when the reader's key emptied the queue.  Either way the
  // turn is over and the tone belongs now.  Any other answer -- 1717 from an
  // NVDA that does not know the call -- means nothing was said, so the text
  // goes the old way.  The library is never unloaded, so the pointers hold.
  const SpeakSsml speakSsml = speakSsml_;
  const SpeakText speak = speak_;
  std::thread([ssml = std::move(ssml), text, speakSsml, speak, then] {
    constexpr int kSymbolLevelUnchanged = -1;
    constexpr int kPriorityNormal = 0;
    const long result =
        speakSsml(ssml.c_str(), kSymbolLevelUnchanged, kPriorityNormal, 0);
    if (result != 0 && result != ERROR_CANCELLED) speak(text.c_str());
    then();
  }).detach();
}

void Speech::Silence() {
  if (cancel_ && Running()) {
    cancel_();
  } else if (PrismBackend* reader = OtherReaders().Reader()) {
    OtherReaders().Stop(reader);
  }
}

}  // namespace ui
