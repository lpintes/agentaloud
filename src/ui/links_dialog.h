#ifndef UI_LINKS_DIALOG_H
#define UI_LINKS_DIALOG_H

// Alt+Enter in the transcript (claude-gui-lkk.74): a link under the caret
// opens straight away; otherwise this lists every link in the block.
//
// The link is opened from inside the dialog, which closes only once the
// browser has it.  A failure is a sentence, and behind a closing dialog
// speech dies (invariant 6) -- so the dialog stays, says it, and the reader
// can try another row or leave with Esc.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "model/links.h"

namespace ui {

// The browser for one address, nothing said on success: the browser takes
// the foreground and NVDA reads it.  False when ShellExecute refused, or when
// the text is not an http or https address at all -- ShellExecute runs
// whatever it is given, and this text was written by a model.
bool OpenLink(const std::wstring& url);

// One row per link, "title: address" when the link has a title.  `announce`
// says the failure.  Returns once a link was opened or the dialog was
// dismissed.
void ShowLinks(HWND owner, std::vector<model::Link> links,
               const std::function<void(const std::wstring&)>& announce);

}  // namespace ui

#endif
