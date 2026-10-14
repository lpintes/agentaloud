#include "ui/speech.h"

#include <thread>

namespace ui {

bool Speech::Open() {
  if (library_) return available();

  // Beside the executable first, because that is the copy shipped with the
  // application and known to match this code; then the search path, so a
  // machine that already has one is not made to carry two.
  library_ = LoadLibraryW(L"nvdaControllerClient.dll");
  if (!library_) return false;

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
  if (text.empty() || !speak_ || !Running()) return;
  if (interrupt) Silence();
  speak_(text.c_str());
}

void Speech::SayThen(const std::wstring& text, void (*then)()) {
  if (text.empty() || !speak_ || !Running()) {
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
  if (cancel_ && Running()) cancel_();
}

}  // namespace ui
