#ifndef UI_ASK_DIALOG_H
#define UI_ASK_DIALOG_H

// The dialog the model's own questions are put in.
//
// It exists because without it a question with three options was shown as a
// permission prompt with Yes and No, and the user answered "I cannot sensibly
// confirm this, I only have yes and no" -- the question was there on screen and
// there was no way to answer it.  What arrives is not a permission at all: see
// proto/ask.h for the shape and for how the answer travels back.
//
// One question per dialog, in turn.  A question is at most four options and a
// call is at most four questions, so the alternative was a template with
// sixteen controls in it, most of them hidden -- and a screen reader walks
// what is there, not what is drawn.  A dialog per question also means the
// title can say which one it is ("otázka 2 z 3"), and NVDA reads the title.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

#include "agent/events.h"

namespace ui {

// Puts the questions, in order.  True when every one of them was answered;
// `chosen` then runs parallel to `questions`, holding the labels picked (or
// what the user typed instead).
//
// False means the user dismissed one of them, and that denies the whole tool
// call rather than sending a half-answer.  Half-answers are legal on the wire
// -- the CLI tells the model to ask again about what is missing -- but Esc
// here means "I am not answering this", and asking again is not that.
bool AskQuestions(HWND owner, const std::vector<agent::Question>& questions,
                  std::vector<std::vector<std::string>>* chosen);

}  // namespace ui

#endif
