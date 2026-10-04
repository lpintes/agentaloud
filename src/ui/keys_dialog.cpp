#include "ui/keys_dialog.h"

#include <string>
#include <vector>

#include "model/utf.h"
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
const wchar_t kSending[] =
    L"Odosielanie a ťah:\n"
    L"Ctrl+Enter — odošle prompt. Len z poľa Prompt.\n"
    L"Esc — preruší bežiaci ťah. Z oboch polí.\n"
    L"Tab — prepne medzi prepisom a promptom.\n";

// The modes are the agent's and come from Capabilities; see ModesText.

const wchar_t kMoving[] =
    L"\n"
    L"Pohyb po prepise:\n"
    L"T — ďalšie volanie nástroja, Shift+T — predchádzajúce.\n"
    L"R — ďalší výstup nástroja, Shift+R — predchádzajúci.\n"
    L"P — ďalší prompt, Shift+P — predchádzajúci.\n"
    L"A — ďalšia odpoveď, Shift+A — predchádzajúca.\n"
    L"K — ďalšie premýšľanie, Shift+K — predchádzajúce.\n"
    L"E — ďalšia chyba alebo zamietnuté volanie, Shift+E — predchádzajúce.\n"
    L"Shift znamená dozadu. Týchto šesť písmen platí len v prepise.\n"
    L"Ctrl+Shift+T, R, P, A, K, E — to isté dopredu, aj z poľa Prompt.\n"
    L"Enter — zbalí alebo rozbalí blok pod kurzorom. Len v prepise.\n"
    L"Ctrl+C — skopíruje označený text. Robí to samotné pole prepisu.\n"
    L"\n"
    L"Záložky, z oboch polí:\n"
    L"Ctrl+Shift+1 až Ctrl+Shift+9 — označí blok, na ktorom stojí kurzor.\n"
    L"Ctrl+1 až Ctrl+9 — vráti kurzor na označený blok.\n"
    L"Ctrl+0 — späť tam, kde kurzor stál, kým naposledy niečo pribudlo.\n"
    L"Nultú záložku píše aplikácia sama, nastaviť sa nedá.\n"
    L"\n"
    L"Dialógy a schránka, z oboch polí:\n"
    L"F1 — tento zoznam.\n"
    L"F2 — podrobnosti session: id, model, kontext, cena.\n";

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
  if (cycle.empty()) {
    return L"Shift+Tab — tento agent režimy za behu meniť nevie.\n";
  }
  text += L"Shift+Tab — mení režim povolení dokola, z oboch polí:\n";
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
    text += L"Len pri štarte, cez --permission-mode: " + rest + L".\n";
  }
  return text;
}

class KeysDialog : public win::Dialog {
 public:
  explicit KeysDialog(std::wstring text) : text_(std::move(text)) {}

 protected:
  bool OnInit() override {
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
  std::wstring text = kSending;
  text += ModesText(capabilities);
  text += kMoving;
  // Listed even when the agent has none, and saying so: a key missing from
  // this list does not exist (invariant 19), and F4 still answers.
  text += capabilities.slashCommands
              ? L"F4 — zoznam slash príkazov. Vybraný vloží do promptu, "
                L"neodošle.\n"
              : L"F4 — zoznam slash príkazov; tento agent žiadny nemá.\n";
  text += L"Ctrl+Shift+C — skopíruje id session do schránky.";
  return text;
}

void ShowKeys(HWND owner, const agent::Capabilities& capabilities) {
  KeysDialog dialog(KeysText(capabilities));
  dialog.ShowModal(owner, IDD_KEYS);
}

}  // namespace ui
