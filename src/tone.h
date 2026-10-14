#ifndef TONE_H
#define TONE_H

// Signals of our own, synthesised (claude-gui-lkk.75).
//
// Why not the system sounds: they depend on the sound scheme, are often
// switched off or all the same, and a reader who stepped away could neither
// hear nor tell apart "the turn is over" and "the agent is waiting for you".
// A tone of a pitch and shape chosen here is the same on every machine.
//
// Why not Beep(): it is synchronous and would stall the UI thread for the
// length of the tone.  A WAV built in memory goes to PlaySoundW with
// SND_MEMORY | SND_ASYNC instead.  This file only builds the bytes -- no
// windows.h -- so the tests can look at them.

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace tone {

inline constexpr int kSampleRate = 22050;

// One note.  A frequency of 0 is a rest of that length.
struct Note {
  int hz = 0;
  int ms = 0;
};

// A complete RIFF/WAVE file, 16-bit mono PCM at kSampleRate.  Each note fades
// in and out over a few milliseconds: a sine cut off mid-swing clicks, and the
// click is louder than the tone.  amplitude is 0..1 of full scale.
std::vector<std::uint8_t> MakeWave(std::initializer_list<Note> notes,
                                   double amplitude);

}  // namespace tone

#endif  // TONE_H
