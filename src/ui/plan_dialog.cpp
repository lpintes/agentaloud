#include "ui/plan_dialog.h"

#include <string>

#include "i18n/i18n.h"
#include "model/transcript.h"
#include "model/utf.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"
#include "win/dialog.h"

namespace ui {
namespace {

// The label and the gloss on one line, so that arrowing through the list says
// what each mode lets the agent do -- the label alone ("automatické úpravy")
// is a name the reader has to have learnt.  A colon, not a dash: NVDA passes
// over a dash without a word.
std::wstring ModeLine(const std::vector<agent::Mode>& modes,
                      const std::string& id) {
  for (const agent::Mode& mode : modes) {
    if (mode.id != id) continue;
    std::wstring line = model::Utf16FromUtf8(mode.label);
    if (!mode.gloss.empty()) line += L": " + model::Utf16FromUtf8(mode.gloss);
    return line;
  }
  return model::Utf16FromUtf8(id);
}

class PlanDialog : public win::Dialog {
 public:
  PlanDialog(const agent::PermissionRequest& request,
             const std::vector<agent::Mode>& modes,
             agent::PermissionAnswer* answer)
      : request_(request), modes_(modes), answer_(answer) {}

 protected:
  bool OnInit() override;
  bool OnOk() override;
  bool OnCommand(int id, int notification) override;

 private:
  const agent::PermissionRequest& request_;
  const std::vector<agent::Mode>& modes_;
  agent::PermissionAnswer* answer_;
};

bool PlanDialog::OnInit() {
  LocalizeDialog(hwnd_, IDD_PLAN_APPROVAL);
  const std::wstring caption =
      RequestCaption(request_.by) + i18n::Text(i18n::Str::kDlgPlanCaption);
  SetWindowTextW(hwnd_, caption.c_str());

  // The transcript's rendering of the call, as in the permission prompt
  // (invariant 13): the plan approved is the plan read back afterwards.
  std::wstring plan = model::RenderToolCall(request_.call);
  if (plan.empty()) plan = i18n::Text(i18n::Str::kPermNoArguments);
  SetTextLines(IDC_PLAN_TEXT, plan);

  const HWND list = Item(IDC_PLAN_MODE);
  int selected = 0;
  for (size_t i = 0; i < request_.modesAfter.size(); ++i) {
    const std::wstring line = ModeLine(modes_, request_.modesAfter[i]);
    SendMessageW(list, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    if (request_.modesAfter[i] == request_.modeAfter) {
      selected = static_cast<int>(i);
    }
  }
  SendMessageW(list, CB_SETCURSEL, selected, 0);

  // Into the plan, as the permission prompt goes into the arguments: it is
  // what the answer is about.
  SetFocus(Item(IDC_PLAN_TEXT));
  return true;
}

bool PlanDialog::OnOk() {
  answer_->verdict = agent::Verdict::Allow;
  const LRESULT index = SendMessageW(Item(IDC_PLAN_MODE), CB_GETCURSEL, 0, 0);
  if (index >= 0 &&
      static_cast<size_t>(index) < request_.modesAfter.size()) {
    answer_->mode = request_.modesAfter[static_cast<size_t>(index)];
  }
  return true;
}

bool PlanDialog::OnCommand(int id, int notification) {
  if (id != IDC_PLAN_REVISE || notification != BN_CLICKED) return false;
  answer_->verdict = agent::Verdict::Deny;
  answer_->feedback = model::Utf8FromUtf16(GetText(IDC_PLAN_FEEDBACK));
  EndDialog(hwnd_, IDC_PLAN_REVISE);
  return true;
}

}  // namespace

void AskPlanApproval(HWND owner, const agent::PermissionRequest& request,
                     const std::vector<agent::Mode>& modes,
                     agent::PermissionAnswer* answer) {
  answer->verdict = agent::Verdict::Deny;
  PlanDialog dialog(request, modes, answer);
  dialog.ShowModal(owner, IDD_PLAN_APPROVAL);
}

}  // namespace ui
