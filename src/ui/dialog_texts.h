#ifndef UI_DIALOG_TEXTS_H
#define UI_DIALOG_TEXTS_H

// The words of a dialog template, in the reader's language.
//
// One template per dialog in app.rc, not one per language: the layout is the
// same in every language, and a copy per language is a copy that drifts.  The
// template keeps its Slovak words as the layout's placeholder; this puts the
// catalog's over them -- the caption and every label and button -- by control
// id.  Called first thing in a dialog's OnInit, so whatever the dialog then
// sets for itself (a counter in the title, a label that says how many) wins.

#include <windows.h>

#include <string>

namespace ui {

void LocalizeDialog(HWND dialog, int templateId);

// The front of a request's caption: the application, and the subagent asking
// when it is one -- "AgentAloud — Explore 2: " -- in the transcript's own
// speaker form.  NVDA reads the caption first, so whose request it is comes
// before what it is (claude-gui-b8n.10).  `by` is the port's, UTF-8.
std::wstring RequestCaption(const std::string& by);

}  // namespace ui

#endif
