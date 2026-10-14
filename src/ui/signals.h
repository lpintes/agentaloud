#ifndef UI_SIGNALS_H
#define UI_SIGNALS_H

// The tones the application answers with when there is nobody to say a
// sentence to (claude-gui-lkk.75): the reader is in another window, or the
// turn said nothing.  Each event has a shape of its own -- pitch, direction,
// number of notes -- so that it is told apart by ear and never taken for
// speech.  Why not the system sounds is in tone.h.
//
// All of them return at once; the sound plays on its own.
//
// With a `top`, the window is behind another one, and the tone comes again
// every half a minute until `top` or a window it owns comes to the front.
// Heard once, it is too easy to miss from across the room -- and coming back
// is the only answer it wants.  Without one it plays once: the reader is
// here.  Only the latest event repeats.
//
// UI thread only, except a call without `top`, which touches no timer --
// Speech::SayThen calls TurnEnded from its own thread.

#include <windows.h>

namespace ui::signals {

// From the settings, once at start-up (`sounds`).  Off, every call below is
// silent -- the flash of a waiting window stays, that is not a sound.
void SetEnabled(bool enabled);

// The turn is over.
void TurnEnded(HWND top = nullptr);

// The agent's process is gone; nothing more will come from this session.
void AgentEnded(HWND top = nullptr);

// The agent is waiting for an answer -- a permission, a question, a plan.
void Waiting(HWND top);

// Call when the question is answered.  Coming to the front only stops the
// repeat if the timer happens to look then: answered and left again within
// the half minute, it went on calling about a question no longer there
// (found in use).
void StopRepeating();

}  // namespace ui::signals

#endif  // UI_SIGNALS_H
