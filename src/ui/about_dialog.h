#ifndef UI_ABOUT_DIALOG_H
#define UI_ABOUT_DIALOG_H

// Pomocník > O programe: name, version, copyright, licence and the address
// of the project, in one read-only box.
//
// A box and not static texts, like every value in this application's
// dialogs: a static text cannot take the focus, so it can be neither read
// by character nor selected, and the version and the address are exactly
// what someone copies into a bug report.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace ui {

// Modal; the button opens the project page in the default browser and
// leaves the dialog open.
void ShowAbout(HWND owner);

}  // namespace ui

#endif
