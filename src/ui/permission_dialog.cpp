#include "ui/permission_dialog.h"

#include <string>
#include <vector>

#include "i18n/i18n.h"
#include "model/transcript.h"
#include "model/utf.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"
#include "win/dialog.h"

namespace ui {
namespace {

// What an empty field says.  Not an em dash and not a blank: a screen reader
// passes over punctuation without a word at the usual settings, and an empty
// edit box is announced as "blank", which reads as a bug rather than as an
// answer.  "Neuvedený" says the CLI sent nothing, which is what happened.
std::wstring OrMissing(const std::string& text) {
  const std::wstring kMissing = i18n::Text(i18n::Str::kPermMissing);
  return text.empty() ? std::wstring(kMissing) : model::Utf16FromUtf8(text);
}

class PermissionDialog : public win::Dialog {
 public:
  explicit PermissionDialog(const agent::PermissionRequest& request)
      : request_(request) {}

 protected:
  bool OnInit() override;
  bool OnCommand(int id, int notification) override;

 private:
  // One "allow and stop asking" button with the field saying what it lets
  // through, shown only when the request offers that verdict.
  void ShowChoice(agent::Verdict verdict,
                  const std::vector<std::string>& lines, int button,
                  int label, int field);

  const agent::PermissionRequest& request_;
};

bool PermissionDialog::OnInit() {
  // The tool's name goes in the caption because NVDA reads the caption when
  // the dialog opens and then the focused control -- so "povolenie: Bash" and
  // the command itself arrive as one announcement, in that order.  In a field
  // of its own the name would be a Tab away, and a reader answering quickly
  // would be answering about a tool nobody named.
  LocalizeDialog(hwnd_, IDD_PERMISSION);
  const std::string& name =
      request_.title.empty() ? request_.call.name : request_.title;
  const std::wstring caption =
      RequestCaption(request_.by) +
      i18n::Format(i18n::Str::kPermTitle, {model::Utf16FromUtf8(name)});
  SetWindowTextW(hwnd_, caption.c_str());

  SetText(IDC_PERM_DESCRIPTION, OrMissing(request_.description));
  // Already a sentence: what the CLI's word for it means is the adapter's to
  // know (proto::ClaudeBackend).
  SetText(IDC_PERM_REASON, OrMissing(request_.reason));

  // The transcript's own rendering of the call, not a JSON dump: one field per
  // line, and the long ones as text.  Whatever is allowed here is what will be
  // read back in the transcript afterwards, character for character.
  std::wstring arguments = model::RenderToolCall(request_.call);
  if (arguments.empty()) arguments = i18n::Text(i18n::Str::kPermNoArguments);
  SetTextLines(IDC_PERM_INPUT, arguments);

  ShowChoice(agent::Verdict::AllowForSession, request_.sessionScope,
             IDC_PERM_SESSION, IDC_PERM_SCOPE_LABEL, IDC_PERM_SCOPE);
  ShowChoice(agent::Verdict::AllowInProject, request_.projectScope,
             IDC_PERM_PROJECT, IDC_PERM_PROJECT_SCOPE_LABEL,
             IDC_PERM_PROJECT_SCOPE);

  // Focus into the arguments rather than onto a button.  This is the text the
  // answer is about, and it is the one control here worth walking by line.
  SetFocus(Item(IDC_PERM_INPUT));
  return true;
}

void PermissionDialog::ShowChoice(agent::Verdict verdict,
                                  const std::vector<std::string>& lines,
                                  int button, int label, int field) {
  // Hidden, not just disabled, where the backend cannot keep the answer: a
  // disabled button is still read out, and a choice that is there but never
  // works is worse than one that is not there.  Without it every step of
  // computer use asked again (claude-gui-lkk.61).
  bool offered = false;
  for (agent::Verdict each : request_.offered) {
    if (each == verdict) offered = true;
  }
  // The scope goes with the button.  Where the adapter cannot say it, the
  // field is hidden too, rather than shown empty and read as "nothing".
  std::wstring scope;
  for (const std::string& line : lines) {
    if (!scope.empty()) scope += L'\n';
    scope += model::Utf16FromUtf8(line);
  }
  if (offered && !scope.empty()) {
    SetTextLines(field, scope);
  } else {
    ShowWindow(Item(label), SW_HIDE);
    ShowWindow(Item(field), SW_HIDE);
  }
  if (!offered) ShowWindow(Item(button), SW_HIDE);
}

bool PermissionDialog::OnCommand(int id, int notification) {
  if (id != IDC_PERM_SESSION && id != IDC_PERM_PROJECT) return false;
  if (notification != BN_CLICKED) return false;
  EndDialog(hwnd_, id);
  return true;
}

}  // namespace

agent::Verdict AskPermission(HWND owner,
                             const agent::PermissionRequest& request) {
  PermissionDialog dialog(request);
  switch (dialog.ShowModal(owner, IDD_PERMISSION)) {
    case IDOK: return agent::Verdict::Allow;
    case IDC_PERM_SESSION: return agent::Verdict::AllowForSession;
    case IDC_PERM_PROJECT: return agent::Verdict::AllowInProject;
    default: return agent::Verdict::Deny;
  }
}

}  // namespace ui
