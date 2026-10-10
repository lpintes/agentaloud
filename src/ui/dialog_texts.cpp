#include "ui/dialog_texts.h"

#include <string>
#include <vector>

#include "app_name.h"
#include "i18n/i18n.h"
#include "model/utf.h"
#include "ui/resource.h"

namespace ui {
namespace {

using i18n::Str;

struct Control {
  int id;
  Str text;
};

struct Template {
  int id;
  Str caption;
  std::vector<Control> controls;
};

// A control left out here keeps the template's Slovak word, silently -- so a
// control with text in app.rc belongs here as well.
const std::vector<Template>& Templates() {
  static const std::vector<Template> templates = {
      {IDD_SESSION_DETAILS,
       Str::kDlgDetailsCaption,
       {{IDC_DETAILS_ID_LABEL, Str::kDlgDetailsId},
        {IDC_DETAILS_MODEL_LABEL, Str::kDlgDetailsModel},
        {IDC_DETAILS_MODE_LABEL, Str::kDlgDetailsMode},
        {IDC_DETAILS_PROJECT_LABEL, Str::kDlgDetailsProject},
        {IDC_DETAILS_CONTEXT_LABEL, Str::kDlgDetailsContext},
        {IDC_DETAILS_ACCOUNT_LABEL, Str::kDlgDetailsAccount},
        {IDC_DETAILS_COST_LABEL, Str::kDlgDetailsCost},
        {IDC_DETAILS_TOKENS_LABEL, Str::kDlgDetailsTokens},
        {IDC_DETAILS_COPY, Str::kDlgDetailsCopy},
        {IDCANCEL, Str::kDlgClose}}},
      {IDD_ASK_QUESTION,
       Str::kDlgAskCaption,
       {{IDC_ASK_QUESTION_LABEL, Str::kDlgAskQuestion},
        {IDC_ASK_OPTIONS_LABEL, Str::kDlgAskOptions},
        {IDC_ASK_OTHER_LABEL, Str::kDlgAskOther},
        {IDOK, Str::kDlgAskAnswer},
        {IDCANCEL, Str::kDlgAskDecline}}},
      {IDD_PERMISSION,
       Str::kDlgPermCaption,
       {{IDC_PERM_DESCRIPTION_LABEL, Str::kDlgPermDescription},
        {IDC_PERM_REASON_LABEL, Str::kDlgPermReason},
        {IDC_PERM_INPUT_LABEL, Str::kDlgPermArguments},
        {IDC_PERM_SCOPE_LABEL, Str::kDlgPermScope},
        {IDOK, Str::kDlgPermAllow},
        {IDC_PERM_SESSION, Str::kDlgPermSession},
        {IDCANCEL, Str::kDlgPermDeny}}},
      {IDD_COMMANDS,
       Str::kDlgCmdCaption,
       {{IDC_CMD_FILTER_LABEL, Str::kDlgCmdFilter},
        {IDC_CMD_LIST_LABEL, Str::kDlgCmdList},
        {IDC_CMD_DETAIL_LABEL, Str::kDlgCmdDetail},
        {IDOK, Str::kDlgCmdInsert},
        {IDCANCEL, Str::kDlgCmdCancel}}},
      {IDD_KEYS,
       Str::kDlgKeysCaption,
       {{IDC_KEYS_TEXT_LABEL, Str::kDlgKeysText}, {IDCANCEL, Str::kDlgClose}}},
      {IDD_NEW_SESSION,
       Str::kDlgNewCaption,
       {{IDC_NEW_BACKEND_LABEL, Str::kDlgNewBackend},
        {IDC_NEW_MODEL_LABEL, Str::kDlgNewModel},
        {IDC_NEW_PROJECT_LABEL, Str::kDlgNewProject},
        {IDC_NEW_BROWSE, Str::kDlgNewBrowse},
        {IDOK, Str::kDlgNewStart},
        {IDCANCEL, Str::kDlgNewCancel}}},
      {IDD_FIND,
       Str::kDlgFindCaption,
       {{IDC_FIND_TEXT_LABEL, Str::kDlgFindText},
        {IDOK, Str::kDlgFindOk},
        {IDCANCEL, Str::kDlgClose}}},
      {IDD_TASKS,
       Str::kDlgTasksCaption,
       {{IDC_TASKS_LIST_LABEL, Str::kDlgTasksList},
        {IDOK, Str::kDlgTasksStop},
        {IDCANCEL, Str::kDlgClose}}},
      {IDD_ABOUT,
       Str::kDlgAboutCaption,
       {{IDC_ABOUT_TEXT_LABEL, Str::kDlgAboutText},
        {IDC_ABOUT_PAGE, Str::kDlgAboutPage},
        {IDCANCEL, Str::kDlgClose}}},
  };
  return templates;
}

}  // namespace

void LocalizeDialog(HWND dialog, int templateId) {
  for (const Template& entry : Templates()) {
    if (entry.id != templateId) continue;
    const std::wstring caption =
        std::wstring(L"" APP_NAME L" — ") + i18n::Text(entry.caption);
    SetWindowTextW(dialog, caption.c_str());
    for (const Control& control : entry.controls) {
      SetDlgItemTextW(dialog, control.id, i18n::Text(control.text));
    }
    return;
  }
}

std::wstring RequestCaption(const std::string& by) {
  std::wstring caption = L"" APP_NAME L" — ";
  if (!by.empty()) caption += model::Utf16FromUtf8(by) + L": ";
  return caption;
}

}  // namespace ui
