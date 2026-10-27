#include "ui/form_dialog.h"

#include <string>
#include <vector>

#include "app_name.h"
#include "i18n/i18n.h"
#include "model/form.h"
#include "model/utf.h"
#include "ui/dialog_texts.h"
#include "ui/links_dialog.h"
#include "ui/resource.h"
#include "win/dialog.h"

namespace ui {

using i18n::Str;

namespace {

std::wstring Trim(const std::wstring& text) {
  const size_t first = text.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) return {};
  return text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
}

std::string Title(const agent::FormField& field) {
  return field.title.empty() ? field.name : field.title;
}

// A refusal, said, with the focus put where the fix is: a dialog that stays
// open without a word is the silence this application exists to end.
void Refuse(HWND dialog, const std::wstring& why, HWND focus) {
  const std::wstring title =
      std::wstring(L"" APP_NAME L" — ") + i18n::Text(Str::kDlgFormCaption);
  MessageBoxW(dialog, why.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
  SetFocus(focus);
}

class FieldDialog : public win::Dialog {
 public:
  FieldDialog(const agent::FormRequest& request, size_t index,
              std::vector<std::string>* value)
      : request_(request), field_(request.fields[index]), index_(index),
        value_(value) {}

 protected:
  bool OnInit() override;
  bool OnOk() override;
  bool OnCommand(int id, int notification) override;

 private:
  int ValueId() const;
  // What the value control holds now, as agent::FormAnswer carries it.
  std::vector<std::string> Read() const;
  void Fill();

  const agent::FormRequest& request_;
  const agent::FormField& field_;
  const size_t index_;
  std::vector<std::string>* value_;
  // For the lists: what each item stands for.  An empty entry is "leave
  // empty", offered only for a field that is not required -- a single-choice
  // list cannot otherwise be emptied once something is chosen in it.
  std::vector<std::string> itemValues_;
};

int FieldDialog::ValueId() const {
  switch (field_.type) {
    case agent::FieldType::Boolean:
    case agent::FieldType::Choice:
      return IDC_FORM_CHOICE;
    case agent::FieldType::Choices:
      return IDC_FORM_CHOICES;
    case agent::FieldType::Text:
    case agent::FieldType::Number:
    case agent::FieldType::Integer:
      break;
  }
  return IDC_FORM_TEXT;
}

bool FieldDialog::OnInit() {
  LocalizeDialog(hwnd_, IDD_FORM_FIELD);
  const std::wstring title = model::Utf16FromUtf8(Title(field_));
  const std::wstring caption =
      RequestCaption(request_.by) +
      i18n::Format(Str::kFormFieldCaption,
                   {model::Utf16FromUtf8(request_.server), title,
                    std::to_wstring(index_ + 1),
                    std::to_wstring(request_.fields.size())});
  SetWindowTextW(hwnd_, caption.c_str());

  // The server's words first: on the first field they are what the reader
  // has not heard yet.  Then what this field is and what it takes.
  std::string text = request_.message;
  text += "\n\n" + Title(field_);
  if (!field_.description.empty()) text += ": " + field_.description;
  for (const std::string& rule : model::FormFieldRules(field_)) {
    text += "\n" + rule;
  }
  SetTextLines(IDC_FORM_MESSAGE, model::Utf16FromUtf8(text));

  // The label is the accessible name of the value control, so the field's
  // title goes there and NVDA says it on the way in.
  SetText(IDC_FORM_VALUE_LABEL,
          field_.type == agent::FieldType::Choices
              ? i18n::Format(Str::kFormSeveral, {title})
              : title + L":");

  for (int id : {IDC_FORM_TEXT, IDC_FORM_CHOICE, IDC_FORM_CHOICES}) {
    if (id == ValueId()) continue;
    ShowWindow(Item(id), SW_HIDE);
    // Hidden alone, it would still be tabbed to (see ui/ask_dialog.cpp).
    SetEnabled(id, false);
  }
  SetEnabled(IDC_FORM_BACK, index_ > 0);
  if (index_ + 1 == request_.fields.size()) {
    SetText(IDOK, i18n::Text(Str::kFormSend));
  }
  Fill();

  // The first field opens on the request, which says what the form is for;
  // the next ones on the value, as the request is the same.
  SetFocus(Item(index_ == 0 ? IDC_FORM_MESSAGE : ValueId()));
  return true;
}

void FieldDialog::Fill() {
  if (ValueId() == IDC_FORM_TEXT) {
    if (!value_->empty()) {
      SetText(IDC_FORM_TEXT, model::Utf16FromUtf8(value_->front()));
    }
    return;
  }
  std::vector<std::wstring> labels;
  // "No value" goes first: Home reaches it, and it is not the item a reader
  // has to arrow back up from after passing every option.
  if (field_.type != agent::FieldType::Choices && !field_.required) {
    labels.push_back(i18n::Text(Str::kFormNoValue));
    itemValues_.push_back(std::string());
  }
  if (field_.type == agent::FieldType::Boolean) {
    labels = {i18n::Text(Str::kFormYes), i18n::Text(Str::kFormNo)};
    itemValues_ = {"true", "false"};
  } else {
    for (const agent::FormOption& option : field_.options) {
      labels.push_back(model::Utf16FromUtf8(option.label));
      itemValues_.push_back(option.value);
    }
  }
  const HWND list = Item(ValueId());
  for (const std::wstring& label : labels) {
    SendMessageW(list, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(label.c_str()));
  }
  for (size_t i = 0; i < itemValues_.size(); ++i) {
    for (const std::string& value : *value_) {
      if (itemValues_[i].empty() || itemValues_[i] != value) continue;
      if (field_.type == agent::FieldType::Choices) {
        SendMessageW(list, LB_SETSEL, TRUE, static_cast<LPARAM>(i));
      } else {
        SendMessageW(list, LB_SETCURSEL, i, 0);
      }
    }
  }
  // With nothing chosen, a multiple list still needs its caret on an item,
  // or the first arrow key is spent putting it there in silence.
  if (field_.type == agent::FieldType::Choices) {
    SendMessageW(list, LB_SETCARETINDEX, 0, FALSE);
  } else if (!field_.required &&
             SendMessageW(list, LB_GETCURSEL, 0, 0) == LB_ERR) {
    // Nothing chosen is what the "no value" item says, so the reader
    // lands on it instead of on a list without a selection.
    SendMessageW(list, LB_SETCURSEL, 0, 0);
  }
}

std::vector<std::string> FieldDialog::Read() const {
  std::vector<std::string> values;
  const HWND list = Item(ValueId());
  if (ValueId() == IDC_FORM_TEXT) {
    const std::wstring typed = Trim(GetText(IDC_FORM_TEXT));
    if (!typed.empty()) values.push_back(model::Utf8FromUtf16(typed));
  } else if (ValueId() == IDC_FORM_CHOICE) {
    const LRESULT one = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (one >= 0 && static_cast<size_t>(one) < itemValues_.size() &&
        !itemValues_[static_cast<size_t>(one)].empty()) {
      values.push_back(itemValues_[static_cast<size_t>(one)]);
    }
  } else {
    for (size_t i = 0; i < itemValues_.size(); ++i) {
      if (SendMessageW(list, LB_GETSEL, i, 0) > 0) {
        values.push_back(itemValues_[i]);
      }
    }
  }
  return values;
}

bool FieldDialog::OnOk() {
  std::vector<std::string> values = Read();
  const std::string problem = model::CheckFormField(field_, values);
  if (!problem.empty()) {
    Refuse(hwnd_, model::Utf16FromUtf8(problem), Item(ValueId()));
    return false;
  }
  *value_ = std::move(values);
  return true;
}

bool FieldDialog::OnCommand(int id, int notification) {
  if (notification != BN_CLICKED) return false;
  if (id == IDC_FORM_BACK) {
    *value_ = Read();
    EndDialog(hwnd_, IDC_FORM_BACK);
    return true;
  }
  if (id == IDC_FORM_DECLINE) {
    EndDialog(hwnd_, IDC_FORM_DECLINE);
    return true;
  }
  return false;
}

// The link stays shown whole, in a read-only field: the reader decides by
// the address where it goes, and can copy it when it will not open here.
class UrlDialog : public win::Dialog {
 public:
  explicit UrlDialog(const agent::FormRequest& request) : request_(request) {}

 protected:
  bool OnInit() override {
    LocalizeDialog(hwnd_, IDD_FORM_URL);
    const std::wstring caption =
        RequestCaption(request_.by) +
        i18n::Format(Str::kFormUrlCaption,
                     {model::Utf16FromUtf8(request_.server)});
    SetWindowTextW(hwnd_, caption.c_str());
    SetTextLines(IDC_FORM_MESSAGE, model::Utf16FromUtf8(request_.message));
    SetText(IDC_FORM_LINK, model::Utf16FromUtf8(request_.url));
    return false;
  }
  bool OnOk() override {
    if (OpenLink(model::Utf16FromUtf8(request_.url))) return true;
    Refuse(hwnd_, i18n::Text(Str::kFormLinkNotOpened), Item(IDC_FORM_LINK));
    return false;
  }
  bool OnCommand(int id, int notification) override {
    if (id != IDC_FORM_DECLINE || notification != BN_CLICKED) return false;
    EndDialog(hwnd_, IDC_FORM_DECLINE);
    return true;
  }

 private:
  const agent::FormRequest& request_;
};

agent::FormAnswer::Action ActionFor(INT_PTR result) {
  if (result == IDOK) return agent::FormAnswer::Action::Accept;
  if (result == IDC_FORM_DECLINE) return agent::FormAnswer::Action::Decline;
  return agent::FormAnswer::Action::Cancel;
}

}  // namespace

agent::FormAnswer FillForm(HWND owner, const agent::FormRequest& request) {
  agent::FormAnswer answer;
  if (!request.url.empty() || request.fields.empty()) {
    UrlDialog dialog(request);
    answer.action = ActionFor(dialog.ShowModal(owner, IDD_FORM_URL));
    return answer;
  }
  // Each field starts from its default and keeps what was typed in it, so
  // Back and then forward again finds it as it was left.
  std::vector<std::vector<std::string>> values;
  for (const agent::FormField& field : request.fields) {
    values.push_back(field.defaults);
  }
  size_t index = 0;
  while (index < request.fields.size()) {
    FieldDialog dialog(request, index, &values[index]);
    const INT_PTR result = dialog.ShowModal(owner, IDD_FORM_FIELD);
    if (result == IDC_FORM_BACK) {
      if (index > 0) --index;
      continue;
    }
    answer.action = ActionFor(result);
    if (answer.action != agent::FormAnswer::Action::Accept) return answer;
    ++index;
  }
  answer.action = agent::FormAnswer::Action::Accept;
  answer.values = std::move(values);
  return answer;
}

}  // namespace ui
