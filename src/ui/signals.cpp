#include "ui/signals.h"

#include <mmsystem.h>

#include <cstdint>
#include <vector>

#include "tone.h"

namespace ui::signals {

namespace {

constexpr double kAmplitude = 0.4;
constexpr UINT kRepeatMs = 30 * 1000;

bool gEnabled = true;
HWND gWaitingFor = nullptr;
const std::vector<std::uint8_t>* gRepeated = nullptr;
UINT_PTR gTimer = 0;

// Built on first use and never freed: SND_ASYNC plays straight from this
// memory after PlaySoundW has returned, so it must outlive any playback.
//
// Falling, two notes: the end of something, the way a sentence ends.
const std::vector<std::uint8_t>& TurnEndedWave() {
  static const std::vector<std::uint8_t> wave =
      tone::MakeWave({{784, 90}, {523, 140}}, kAmplitude);
  return wave;
}

// Two knocks and a rise: a question, and the only shape with a rest in it,
// so it is not taken for the end of a turn heard from the next room.
const std::vector<std::uint8_t>& WaitingWave() {
  static const std::vector<std::uint8_t> wave = tone::MakeWave(
      {{660, 70}, {0, 50}, {660, 70}, {0, 50}, {990, 160}}, kAmplitude);
  return wave;
}

// Three notes down, the last one long and low: more final than a turn.
const std::vector<std::uint8_t>& AgentEndedWave() {
  static const std::vector<std::uint8_t> wave =
      tone::MakeWave({{587, 110}, {440, 110}, {294, 220}}, kAmplitude);
  return wave;
}

void Play(const std::vector<std::uint8_t>& wave) {
  if (!gEnabled) return;
  // SND_NODEFAULT: a sound we cannot play is not to be replaced by the
  // system's default one, which already means "the key was refused".
  PlaySoundW(reinterpret_cast<LPCWSTR>(wave.data()), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

bool InFront(HWND top) {
  const HWND foreground = GetForegroundWindow();
  // The dialog that asks is owned by the frame, so its root owner is the
  // frame: answering it counts as having come back.
  return foreground != nullptr &&
         GetAncestor(foreground, GA_ROOTOWNER) == top;
}

// The window is looked at far more often than the tone is played.  Looked at
// only when the tone was due, a reader who came back and left again in
// between -- to copy something from another window -- was never seen to have
// come back, and was called on and on (found in use).
constexpr UINT kPollMs = 250;
ULONGLONG gPlayedAt = 0;

void CALLBACK OnPoll(HWND, UINT, UINT_PTR, DWORD) {
  if (gRepeated == nullptr || gWaitingFor == nullptr ||
      !IsWindow(gWaitingFor) || InFront(gWaitingFor)) {
    StopRepeating();
    return;
  }
  if (GetTickCount64() - gPlayedAt < kRepeatMs) return;
  gPlayedAt = GetTickCount64();
  Play(*gRepeated);
}

void Signal(const std::vector<std::uint8_t>& wave, HWND top) {
  Play(wave);
  if (!gEnabled || top == nullptr) return;
  // One repeat at a time, and the latest event is the one repeated: a turn
  // that ends after its question was answered is no longer waiting.
  gRepeated = &wave;
  gWaitingFor = top;
  gPlayedAt = GetTickCount64();
  // A thread timer, not one on a window: it keeps firing while a modal
  // dialog runs its own message loop, and nobody has to forward WM_TIMER.
  if (gTimer == 0) gTimer = SetTimer(nullptr, 0, kPollMs, OnPoll);
}

}  // namespace

void SetEnabled(bool enabled) { gEnabled = enabled; }

void TurnEnded(HWND top) { Signal(TurnEndedWave(), top); }

void AgentEnded(HWND top) { Signal(AgentEndedWave(), top); }

void Waiting(HWND top) { Signal(WaitingWave(), top); }

void StopRepeating() {
  if (gTimer != 0) KillTimer(nullptr, gTimer);
  gTimer = 0;
  gWaitingFor = nullptr;
  gRepeated = nullptr;
}

}  // namespace ui::signals
