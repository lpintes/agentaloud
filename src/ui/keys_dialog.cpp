#include "ui/keys_dialog.h"

#include <string>

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
const wchar_t kKeys[] =
    L"Odosielanie a ťah:\n"
    L"Ctrl+Enter — odošle prompt. Len z poľa Prompt.\n"
    L"Esc — preruší bežiaci ťah. Z oboch polí.\n"
    L"Tab — prepne medzi prepisom a promptom.\n"
    L"\n"
    L"Pohyb po prepise:\n"
    L"T — ďalšie volanie nástroja, Shift+T — predchádzajúce.\n"
    L"R — ďalší výstup nástroja, Shift+R — predchádzajúci.\n"
    L"P — ďalší prompt, Shift+P — predchádzajúci.\n"
    L"A — ďalšia odpoveď Clauda, Shift+A — predchádzajúca.\n"
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
    L"F2 — podrobnosti session: id, model, kontext, cena.\n"
    L"F4 — zoznam slash príkazov. Vybraný vloží do promptu, neodošle.\n"
    L"Ctrl+Shift+C — skopíruje id session do schránky.";

class KeysDialog : public win::Dialog {
 protected:
  bool OnInit() override {
    SetTextLines(IDC_KEYS_TEXT, kKeys);
    // The caret starts at the top of the box, which is where the dialog
    // manager leaves it, so the first arrow key reads the first heading.
    return false;  // the box is the first tab stop
  }
};

}  // namespace

void ShowKeys(HWND owner) {
  KeysDialog dialog;
  dialog.ShowModal(owner, IDD_KEYS);
}

}  // namespace ui
