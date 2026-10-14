#include "tone.h"

#include <algorithm>
#include <cmath>

namespace tone {

namespace {

// Long enough that the edge does not click, short enough that a 40 ms note
// still has a body between its two ramps.
constexpr int kRampMs = 5;

void PutU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  PutU16(out, static_cast<std::uint16_t>(value & 0xFFFF));
  PutU16(out, static_cast<std::uint16_t>(value >> 16));
}

void PutTag(std::vector<std::uint8_t>& out, const char (&tag)[5]) {
  out.insert(out.end(), tag, tag + 4);
}

}  // namespace

std::vector<std::uint8_t> MakeWave(std::initializer_list<Note> notes,
                                   double amplitude) {
  std::vector<std::int16_t> samples;
  const double peak = std::clamp(amplitude, 0.0, 1.0) * 32767.0;
  const int ramp = kSampleRate * kRampMs / 1000;
  for (const Note& note : notes) {
    const int count = kSampleRate * std::max(note.ms, 0) / 1000;
    for (int i = 0; i < count; ++i) {
      double value = 0.0;
      if (note.hz > 0) {
        // Ramps of at most half the note each, so a very short one is a
        // triangle rather than two ramps that overlap.
        const int edge = std::min(ramp, count / 2);
        double envelope = 1.0;
        if (edge > 0) {
          if (i < edge) envelope = static_cast<double>(i) / edge;
          if (count - 1 - i < edge) {
            envelope = std::min(envelope,
                                static_cast<double>(count - 1 - i) / edge);
          }
        }
        constexpr double kTwoPi = 6.283185307179586;
        value = peak * envelope *
                std::sin(kTwoPi * note.hz * i / kSampleRate);
      }
      samples.push_back(static_cast<std::int16_t>(std::lround(value)));
    }
  }

  const std::uint32_t dataBytes =
      static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
  std::vector<std::uint8_t> out;
  out.reserve(44 + dataBytes);
  PutTag(out, "RIFF");
  PutU32(out, 36 + dataBytes);
  PutTag(out, "WAVE");
  PutTag(out, "fmt ");
  PutU32(out, 16);                 // size of the PCM format block
  PutU16(out, 1);                  // PCM
  PutU16(out, 1);                  // mono
  PutU32(out, kSampleRate);
  PutU32(out, kSampleRate * 2);    // bytes per second
  PutU16(out, 2);                  // bytes per frame
  PutU16(out, 16);                 // bits per sample
  PutTag(out, "data");
  PutU32(out, dataBytes);
  for (std::int16_t sample : samples) {
    PutU16(out, static_cast<std::uint16_t>(sample));
  }
  return out;
}

}  // namespace tone
