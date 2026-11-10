#ifndef UI_SPEECH_H
#define UI_SPEECH_H

// Saying things out loud.
//
// Through NVDA's own controller client, which was the decision in the design:
// it goes through NVDA, so it uses the reader's voice and speed, and it is
// reliable in a way that live regions in a Win32 window are not.  What it does
// bypass is NVDA's own rules about interrupting, which is what Silence() is
// for.
//
// The library is loaded at run time and never linked against.  Two reasons,
// and both matter: the application has to start on a machine with no NVDA, and
// nvdaControllerClient.dll is LGPL while this is MIT -- loading it dynamically
// and shipping it unmodified beside its licence keeps those apart.
//
// When NVDA is not running, the same calls go to whichever other screen reader
// is running -- JAWS, Narrator, ZDSR and the rest -- through prism.dll
// (claude-gui-lkk.54), loaded the same way and for the same reasons (MPL-2.0).
// NVDA keeps its own path because prism cannot do two things this code relies
// on: SSML with a report of when speech ended (SayThen), and noticing that
// NVDA was restarted.  Only screen readers, never a speech engine: a voice of
// our own talking over a reader that is not one prism knows would be worse
// than silence, and a machine with no reader at all wants no voice.
//
// When no reader is running every call here does nothing.  That is deliberate:
// a caller should never have to ask first.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace ui {

class Speech {
 public:
  // Safe to call more than once.  Returns false when no reader is there, which
  // is a fact about the machine and not an error.
  bool Open();
  bool available() const;
  // Whether NVDA's client library was found at all.  Not the same question as
  // available(): NVDA not running is a fact about the moment and nothing is
  // wrong, but a missing DLL is a broken installation that will never speak,
  // and one the application cannot report in its own voice.  prism.dll is not
  // asked about: without it only the other readers go quiet, and those users
  // hear the dialog this is for no better than its absence.
  bool loaded() const { return library_ != nullptr; }

  // interrupt: stop what is being said first.  Use it for something the
  // reader asked for by pressing a key -- they want the answer to that press,
  // not the tail of the previous one.  Leave it off for something that
  // arrived on its own, which should wait its turn.
  //
  // It is not a parameter of the NVDA API: this is cancelSpeech() followed by
  // speakText(), and cancelSpeech empties the WHOLE of NVDA's queue -- our
  // own earlier messages that have not been said yet, and the speech NVDA
  // generates itself, the line under the caret, the focus, the letter just
  // typed.  So there is no such thing as interrupting only our own last
  // sentence, and anything designed around that granularity cannot be built.
  void Say(const std::wstring& text, bool interrupt);
  void Silence();

  // Say(text, false), and call `then` once NVDA has finished saying it --
  // and with it everything queued before it -- or the reader cut it short.
  // For a tone that has to come after the speech and not in the middle of it
  // (claude-gui-lkk.75).  `then` runs on a thread of its own.  Without the
  // SSML half of the API (NVDA before 2024.1), and with any reader other than
  // NVDA, it runs at once.
  void SayThen(const std::wstring& text, void (*then)());

 private:
  // NVDA itself, not any reader.
  bool Running() const;

  using SpeakText = long(__stdcall*)(const wchar_t*);
  using CancelSpeech = long(__stdcall*)();
  using TestIfRunning = long(__stdcall*)();
  // symbol level, priority, asynchronous -- see vendor/nvda/nvdaController.h.
  using SpeakSsml = long(__stdcall*)(const wchar_t*, int, int, unsigned char);

  HMODULE library_ = nullptr;
  SpeakText speak_ = nullptr;
  SpeakSsml speakSsml_ = nullptr;
  CancelSpeech cancel_ = nullptr;
  TestIfRunning test_ = nullptr;
};

}  // namespace ui

#endif
