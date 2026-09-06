#include "ui/permission_dialog.h"

#include <string>

#include "model/transcript.h"
#include "model/utf.h"
#include "ui/resource.h"
#include "win/dialog.h"

namespace ui {
namespace {

// What an empty field says.  Not an em dash and not a blank: a screen reader
// passes over punctuation without a word at the usual settings, and an empty
// edit box is announced as "blank", which reads as a bug rather than as an
// answer.  "Neuvedený" says the CLI sent nothing, which is what happened.
const wchar_t kMissing[] = L"neuvedený";

// `decision_reason_type` as a sentence.  The wire's words are for a program:
// "rule" alone in a dialog says nothing about which rule or whose.
//
// Both values below were measured, not read -- "rule" in tools/spike_control.py
// (a git commit against an ask rule) and "subcommandResults" during the probes
// on claude-gui-lkk.25.  Anything else is passed through as it came: an unknown
// word is still more than no word, and inventing a translation for it would be
// the one failure this dialog cannot afford.
std::wstring ReasonSentence(const std::string& type) {
  if (type.empty()) return kMissing;
  if (type == "rule") return L"pravidlo v nastaveniach alebo hook (rule)";
  if (type == "subcommandResults") {
    return L"vyhodnotenie podpríkazov (subcommandResults)";
  }
  return model::Utf16FromUtf8(type);
}

class PermissionDialog : public win::Dialog {
 public:
  explicit PermissionDialog(const proto::PermissionRequest& request)
      : request_(request) {}

 protected:
  bool OnInit() override;

 private:
  const proto::PermissionRequest& request_;
};

bool PermissionDialog::OnInit() {
  // The tool's name goes in the caption because NVDA reads the caption when
  // the dialog opens and then the focused control -- so "povolenie: Bash" and
  // the command itself arrive as one announcement, in that order.  In a field
  // of its own the name would be a Tab away, and a reader answering quickly
  // would be answering about a tool nobody named.
  const std::string& name =
      request_.displayName.empty() ? request_.toolName : request_.displayName;
  SetWindowTextW(hwnd_, (L"ClaudeLens — povolenie: " +
                         model::Utf16FromUtf8(name)).c_str());

  SetText(IDC_PERM_DESCRIPTION,
          request_.description.empty()
              ? std::wstring(kMissing)
              : model::Utf16FromUtf8(request_.description));
  SetText(IDC_PERM_REASON, ReasonSentence(request_.decisionReasonType));

  // The transcript's own rendering of the call, not a JSON dump: one field per
  // line, and the long ones as text.  Whatever is allowed here is what will be
  // read back in the transcript afterwards, character for character.
  std::wstring arguments =
      model::RenderToolCall(request_.toolName, request_.input);
  if (arguments.empty()) arguments = L"bez argumentov";
  SetTextLines(IDC_PERM_INPUT, arguments);

  // Focus into the arguments rather than onto a button.  This is the text the
  // answer is about, and it is the one control here worth walking by line.
  SetFocus(Item(IDC_PERM_INPUT));
  return true;
}

}  // namespace

bool AskPermission(HWND owner, const proto::PermissionRequest& request) {
  PermissionDialog dialog(request);
  return dialog.ShowModal(owner, IDD_PERMISSION) == IDOK;
}

}  // namespace ui
