#include "ui/speech.h"

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

void Speech::Silence() {
  if (cancel_ && Running()) cancel_();
}

}  // namespace ui
