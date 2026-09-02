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
// When NVDA is not running every call here does nothing.  That is deliberate:
// a caller should never have to ask first.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace ui {

class Speech {
 public:
  // Safe to call more than once.  Returns false when NVDA is not there, which
  // is a fact about the machine and not an error.
  bool Open();
  bool available() const { return speak_ != nullptr && Running(); }

  // interrupt: stop what is being said first.  Use it for something the
  // reader asked for by pressing a key -- they want the answer to that press,
  // not the tail of the previous one.  Leave it off for something that
  // arrived on its own, which should wait its turn.
  void Say(const std::wstring& text, bool interrupt);
  void Silence();

 private:
  bool Running() const;

  using SpeakText = long(__stdcall*)(const wchar_t*);
  using CancelSpeech = long(__stdcall*)();
  using TestIfRunning = long(__stdcall*)();

  HMODULE library_ = nullptr;
  SpeakText speak_ = nullptr;
  CancelSpeech cancel_ = nullptr;
  TestIfRunning test_ = nullptr;
};

}  // namespace ui

#endif
