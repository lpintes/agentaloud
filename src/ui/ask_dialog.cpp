#include "ui/ask_dialog.h"

#include "model/utf.h"
#include "ui/resource.h"
#include "win/dialog.h"

namespace ui {
namespace {

// What "answer something of my own" looks like in the list.  It is an item and
// not just the box below, so that the list holds every answer there is: with a
// box that counts whenever it has text in it, picking an option and then
// typing a remark loses one of the two without saying which.  The CLI's own
// terminal does the same thing and calls the item "Other".
const wchar_t kOtherItem[] = L"Iné — vlastná odpoveď, napíš ju do poľa nižšie";

std::wstring Trim(const std::wstring& text) {
  const size_t first = text.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) return {};
  return text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
}

// A list box item is one line whatever is in it: a lone newline in a
// description comes out as a box glyph, and read aloud it is nothing at all.
// The description is a sentence about a trade-off, so folding it onto one line
// loses nothing -- the whole question is in the transcript either way.
std::wstring OneLine(std::wstring text) {
  for (wchar_t& character : text) {
    if (character == L'\n' || character == L'\r' || character == L'\t') {
      character = L' ';
    }
  }
  return text;
}

// One question, on screen.  The answers come back as labels rather than
// indices because that is what the wire wants -- see proto/ask.h.
class AskDialog : public win::Dialog {
 public:
  AskDialog(const proto::AskQuestion& question, size_t ordinal, size_t total)
      : question_(question), ordinal_(ordinal), total_(total) {}

  const std::vector<std::string>& chosen() const { return chosen_; }

 protected:
  bool OnInit() override;
  bool OnOk() override;

 private:
  // The list actually in use.  The other one is hidden and disabled in OnInit,
  // so nothing else has to remember which is which.
  int ListId() const {
    return question_.multiSelect ? IDC_ASK_OPTIONS_MULTI : IDC_ASK_OPTIONS;
  }
  LRESULT List(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) const {
    return SendDlgItemMessageW(hwnd_, ListId(), message, wParam, lParam);
  }
  std::vector<int> Selected() const;
  // Says why the dialog is staying open, and puts the focus where the fix is.
  // A dialog that refuses to close without a word is the same silence this
  // application spends the rest of its time avoiding.
  bool Refuse(const wchar_t* why, int focusId) const;

  const proto::AskQuestion& question_;
  const size_t ordinal_;  // 1-based, for the title
  const size_t total_;
  std::vector<std::string> chosen_;
};

bool AskDialog::OnInit() {
  // The title carries the header and the counter, because NVDA reads the title
  // when the dialog opens and nothing else it reads says how many of these are
  // still coming.
  std::wstring caption = L"ClaudeLens — ";
  caption += question_.header.empty() ? std::wstring(L"otázka")
                                      : model::Utf16FromUtf8(question_.header);
  if (total_ > 1) {
    caption += L" (otázka " + std::to_wstring(ordinal_) + L" z " +
               std::to_wstring(total_) + L")";
  }
  SetWindowTextW(hwnd_, caption.c_str());

  SetTextLines(IDC_ASK_QUESTION, model::Utf16FromUtf8(question_.question));
  // Whether one answer is wanted or several is in the label, and so in the
  // name NVDA reads out when the list takes the focus.  Without it the two
  // lists are indistinguishable until something is tried.
  SetText(IDC_ASK_OPTIONS_LABEL,
          question_.multiSelect ? L"&Možnosti — dá sa označiť viac (medzerník):"
                                : L"&Možnosti — vyber jednu:");

  const int unused =
      question_.multiSelect ? IDC_ASK_OPTIONS : IDC_ASK_OPTIONS_MULTI;
  ShowWindow(Item(unused), SW_HIDE);
  // Hiding alone is not enough: the dialog manager tabs to a hidden control
  // and the focus then sits somewhere invisible with nothing to read.
  SetEnabled(unused, false);

  for (const proto::AskOption& option : question_.options) {
    std::wstring line = model::Utf16FromUtf8(option.label);
    if (!option.description.empty()) {
      line += L" — " + model::Utf16FromUtf8(option.description);
    }
    line = OneLine(std::move(line));
    List(LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
  }
  List(LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kOtherItem));
  // Nothing preselected on purpose.  Enter is the default button, so a
  // preselected first option would turn one keystroke into an answer the
  // reader never chose -- and it would be the answer the model listed first.
  return false;  // the dialog manager puts the focus on the question box
}

std::vector<int> AskDialog::Selected() const {
  std::vector<int> selected;
  if (!question_.multiSelect) {
    const LRESULT one = List(LB_GETCURSEL);
    if (one != LB_ERR) selected.push_back(static_cast<int>(one));
    return selected;
  }
  const LRESULT count = List(LB_GETSELCOUNT);
  if (count <= 0) return selected;
  selected.resize(static_cast<size_t>(count));
  List(LB_GETSELITEMS, static_cast<WPARAM>(count),
       reinterpret_cast<LPARAM>(selected.data()));
  return selected;
}

bool AskDialog::Refuse(const wchar_t* why, int focusId) const {
  MessageBoxW(hwnd_, why, L"ClaudeLens — otázka", MB_OK | MB_ICONINFORMATION);
  SetFocus(Item(focusId));
  return false;
}

bool AskDialog::OnOk() {
  const int otherIndex = static_cast<int>(question_.options.size());
  const std::vector<int> selected = Selected();
  const std::wstring typed = Trim(GetText(IDC_ASK_OTHER));
  bool wantsOther = false;
  for (int index : selected) wantsOther = wantsOther || index == otherIndex;

  if (selected.empty()) {
    return Refuse(L"Nič nie je označené. Vyber možnosť zo zoznamu, alebo "
                  L"otázku odmietni tlačidlom Zamietnuť.",
                  ListId());
  }
  if (wantsOther && typed.empty()) {
    return Refuse(L"Je označená možnosť Iné, ale pole Vlastná odpoveď je "
                  L"prázdne. Napíš do neho odpoveď.",
                  IDC_ASK_OTHER);
  }
  // The other way round is refused too, and that is the point of the rule:
  // text in the box with nothing marked in the list would otherwise be thrown
  // away without a word, which is exactly the kind of silence this dialog is
  // here to end.
  if (!wantsOther && !typed.empty()) {
    return Refuse(L"Vlastná odpoveď sa použije, len keď je v zozname označená "
                  L"možnosť Iné. Označ ju, alebo pole vyprázdni.",
                  ListId());
  }

  chosen_.clear();
  for (int index : selected) {
    if (index == otherIndex) {
      chosen_.push_back(model::Utf8FromUtf16(typed));
    } else if (index >= 0 && index < otherIndex) {
      chosen_.push_back(question_.options[static_cast<size_t>(index)].label);
    }
  }
  return true;
}

}  // namespace

bool AskQuestions(HWND owner, const std::vector<proto::AskQuestion>& questions,
                  std::vector<std::vector<std::string>>* chosen) {
  chosen->clear();
  for (size_t i = 0; i < questions.size(); ++i) {
    AskDialog dialog(questions[i], i + 1, questions.size());
    if (dialog.ShowModal(owner, IDD_ASK_QUESTION) != IDOK) {
      chosen->clear();
      return false;
    }
    chosen->push_back(dialog.chosen());
  }
  return true;
}

}  // namespace ui
