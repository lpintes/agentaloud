#include "ui/new_session_dialog.h"

#include "app_name.h"
#include "i18n/i18n.h"
#include "model/utf.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"

namespace ui {
namespace {

std::wstring Trimmed(const std::wstring& text) {
  const size_t first = text.find_first_not_of(L" \t");
  if (first == std::wstring::npos) return {};
  const size_t last = text.find_last_not_of(L" \t");
  return text.substr(first, last - first + 1);
}

}  // namespace

NewSessionDialog::NewSessionDialog(NewSession* session) : session_(session) {}

bool NewSessionDialog::OnInit() {
  LocalizeDialog(hwnd_, IDD_NEW_SESSION);
  const HWND backends = Item(IDC_NEW_BACKEND);
  int selected = 0;
  for (size_t i = 0; i < session_->backends.size(); ++i) {
    const std::wstring name = model::Utf16FromUtf8(session_->backends[i].name);
    SendMessageW(backends, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(name.c_str()));
    if (session_->backends[i].name == session_->backend) {
      selected = static_cast<int>(i);
    }
  }
  SendMessageW(backends, CB_SETCURSEL, selected, 0);
  ShowModels(selected);
  SetText(IDC_NEW_PROJECT, session_->project);
  return false;  // the dialog manager puts the focus on the backend list
}

// The model field takes the new backend's default whatever was typed in it:
// model names belong to one CLI, and "opus" carried over to Codex would be a
// session refused on a stderr nobody sees (invariant 16).
void NewSessionDialog::ShowModels(int backend) {
  const HWND models = Item(IDC_NEW_MODEL);
  SendMessageW(models, CB_RESETCONTENT, 0, 0);
  if (backend < 0 || backend >= static_cast<int>(session_->backends.size())) {
    return;
  }
  const NewSessionBackend& entry = session_->backends[backend];
  for (const std::string& name : entry.models) {
    const std::wstring wide = model::Utf16FromUtf8(name);
    SendMessageW(models, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(wide.c_str()));
  }
  SetText(IDC_NEW_MODEL, entry.model);
}

// The focus goes back into the field, not onto the button: the picker
// closing is a focus change NVDA announces anyway, and landing in the field
// makes that announcement the path that was just chosen (invariant 6).
void NewSessionDialog::Browse() {
  const std::wstring chosen =
      win::PickFolder(hwnd_, i18n::Text(i18n::Str::kNewPickFolder),
                      Trimmed(GetText(IDC_NEW_PROJECT)));
  if (!chosen.empty()) SetText(IDC_NEW_PROJECT, chosen);
  SendMessageW(hwnd_, WM_NEXTDLGCTL,
               reinterpret_cast<WPARAM>(Item(IDC_NEW_PROJECT)), TRUE);
}

void NewSessionDialog::Refuse(int focus, const std::wstring& why) {
  MessageBoxW(hwnd_, why.c_str(), L"" APP_NAME, MB_OK | MB_ICONWARNING);
  SendMessageW(hwnd_, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(Item(focus)),
               TRUE);
}

bool NewSessionDialog::OnCommand(int id, int notification) {
  if (id == IDC_NEW_BACKEND && notification == CBN_SELCHANGE) {
    ShowModels(static_cast<int>(
        SendMessageW(Item(IDC_NEW_BACKEND), CB_GETCURSEL, 0, 0)));
    return true;
  }
  if (id == IDC_NEW_BROWSE && notification == BN_CLICKED) {
    Browse();
    return true;
  }
  return false;
}

// Checked here and not after the dialog closes, so that a typo costs an
// edit and not the whole dialog again.  Only that the folder exists: what
// is in it is the CLI's business, and an empty folder is a fine place to
// start a project.
bool NewSessionDialog::OnOk() {
  const std::wstring project = Trimmed(GetText(IDC_NEW_PROJECT));
  if (project.empty()) {
    Refuse(IDC_NEW_PROJECT, i18n::Text(i18n::Str::kNewEnterProject));
    return false;
  }
  const DWORD attributes = GetFileAttributesW(project.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    Refuse(IDC_NEW_PROJECT,
           i18n::Format(i18n::Str::kNewFolderMissing, {project}));
    return false;
  }
  const int backend = static_cast<int>(
      SendMessageW(Item(IDC_NEW_BACKEND), CB_GETCURSEL, 0, 0));
  if (backend < 0 || backend >= static_cast<int>(session_->backends.size())) {
    Refuse(IDC_NEW_BACKEND, i18n::Text(i18n::Str::kNewChooseBackend));
    return false;
  }
  session_->backend = session_->backends[backend].name;
  session_->model = Trimmed(GetText(IDC_NEW_MODEL));
  session_->project = project;
  return true;
}

}  // namespace ui
