#include "ui/keys_dialog.h"

#include <string>
#include <vector>

#include "i18n/i18n.h"
#include "model/utf.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"
#include "win/dialog.h"

namespace ui {
namespace {

// Written with '\n', like everything else in this application; the widget's
// own CR LF spelling is put on by win::Dialog::SetTextLines on the way in.
// That is invariant 4, and a lone '\n' left in a plain EDIT would be drawn as
// a box glyph and read aloud as nothing at all -- which for a list whose whole
// job is to be read line by line would be the entire content lost.
//
// Every line stands on its own, because a reader arrives at any one of them
// with a single arrow key and nothing before it is in earshot.  So each says
// which box the key works in when that is not both of them, and no line
// finishes a sentence the line above started.
//
// Headings end in a colon and are separated by a blank line.  Read aloud a
// blank line says "blank", which is what a heading needs before it.
//
// Every key is written the way a keyboard shortcut is written -- "T" and
// "Shift+T", never "t" and "T".  A screen reader says both cases of a letter
// the same, so the pair of them read aloud is one word twice and the line
// stops meaning anything: the whole distinction this list exists to record is
// the one that vanishes.  What Navigate actually reads is the case of the
// character and not the shift state -- that is what makes it work on any
// layout -- so with Caps Lock on the two swap over; the name written here is
// the one the key has the rest of the time.
//
// The text itself is in the catalog, in every language (i18n/<code>.def); the
// rules above hold for each of them.  A key added to the application is added
// to every language's list at once, or it does not exist (invariant 19).

// Shift+Tab's lines, out of the agent's own list of modes in its own order.
// A mode off the cycle is named too, once per label: a session can be
// started in it, and a reader who meets it in the status bar should find it
// here.  Claude's "manual" is "default" under another name and shares its
// label, which is why it is per label and not per id.
std::wstring ModesText(const agent::Capabilities& capabilities) {
  std::wstring text;
  std::vector<std::wstring> cycle;
  std::vector<std::wstring> startOnly;
  for (const agent::Mode& mode : capabilities.modes) {
    const std::wstring label = model::Utf16FromUtf8(mode.label);
    if (mode.inCycle) {
      cycle.push_back(L"  " + label + L" — " +
                      model::Utf16FromUtf8(mode.gloss));
    } else {
      startOnly.push_back(label);
    }
  }
  if (cycle.empty()) return i18n::Text(i18n::Str::kKeysModesNone);
  text += i18n::Text(i18n::Str::kKeysModesHeading);
  for (size_t i = 0; i < cycle.size(); ++i) {
    text += cycle[i] + (i + 1 < cycle.size() ? L";\n" : L".\n");
  }
  std::wstring rest;
  for (const std::wstring& label : startOnly) {
    bool known = false;
    for (const agent::Mode& mode : capabilities.modes) {
      if (mode.inCycle && model::Utf16FromUtf8(mode.label) == label) {
        known = true;
      }
    }
    if (known || rest.find(label) != std::wstring::npos) continue;
    if (!rest.empty()) rest += L", ";
    rest += label;
  }
  if (!rest.empty()) {
    text += i18n::Format(i18n::Str::kKeysModesStartOnly, {rest});
  }
  return text;
}

class KeysDialog : public win::Dialog {
 public:
  explicit KeysDialog(std::wstring text) : text_(std::move(text)) {}

 protected:
  bool OnInit() override {
    LocalizeDialog(hwnd_, IDD_KEYS);
    SetTextLines(IDC_KEYS_TEXT, text_.c_str());
    // The caret starts at the top of the box, which is where the dialog
    // manager leaves it, so the first arrow key reads the first heading.
    return false;  // the box is the first tab stop
  }

 private:
  std::wstring text_;
};

}  // namespace

std::wstring KeysText(const agent::Capabilities& capabilities) {
  using i18n::Str;
  std::wstring text = i18n::Text(Str::kKeysSending);
  // The modes are the agent's and come from Capabilities.
  text += ModesText(capabilities);
  text += i18n::Text(Str::kKeysMoving);
  // Listed even when the agent has none, and saying so: a key missing from
  // this list does not exist (invariant 19), and F4 still answers.
  text += i18n::Text(capabilities.slashCommands ? Str::kKeysCommands
                                                : Str::kKeysNoCommands);
  text += i18n::Text(Str::kKeysCopyId);
  return text;
}

void ShowKeys(HWND owner, const agent::Capabilities& capabilities) {
  KeysDialog dialog(KeysText(capabilities));
  dialog.ShowModal(owner, IDD_KEYS);
}

}  // namespace ui
