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

namespace ui {

void LocalizeDialog(HWND dialog, int templateId);

}  // namespace ui

#endif
