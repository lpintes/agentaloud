#include "ui/new_session_dialog.h"

#include <mutex>
#include <thread>

#include "app_name.h"
#include "i18n/i18n.h"
#include "model/utf.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"
#include "win/paths.h"

namespace ui {
namespace {

// A list of conversations has arrived from the worker thread; lParam is a
// Listed the dialog takes over.
constexpr UINT kMsgListed = WM_APP + 1;
// The recent folders have arrived; lParam is a FoldersListed, likewise.
constexpr UINT kMsgFolders = WM_APP + 2;
// One backend's models have arrived; lParam is a ModelsListed, likewise.
constexpr UINT kMsgModels = WM_APP + 3;

// A first prompt can be a page long.  The row is for telling conversations
// apart, and the screen reader reads all of it before the next arrow key
// does anything useful.
constexpr size_t kPromptChars = 120;

std::wstring Trimmed(const std::wstring& text) {
  const size_t first = text.find_first_not_of(L" \t");
  if (first == std::wstring::npos) return {};
  const size_t last = text.find_last_not_of(L" \t");
  return text.substr(first, last - first + 1);
}

// The list is keyed by the folder as the CLI will see it, and "." is not a
// key (main.cpp expands it the same way before the session starts).
std::wstring Expanded(const std::wstring& path) {
  if (path.empty()) return path;
  const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
  if (needed == 0) return path;
  std::wstring full(needed, L'\0');
  const DWORD written =
      GetFullPathNameW(path.c_str(), needed, full.data(), nullptr);
  if (written == 0 || written >= needed) return path;
  full.resize(written);
  return full;
}

bool IsFolder(const std::wstring& path) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// In the reader's own format for dates, which is Windows', not the
// catalog's: a Slovak interface on an English Windows still writes the date
// the way the rest of that machine does.  Not localtime, which is not safe
// on more than one thread.
std::wstring LocalTimeText(long long unixSeconds) {
  ULARGE_INTEGER ticks;
  ticks.QuadPart =
      static_cast<unsigned long long>(unixSeconds + 11644473600LL) * 10000000ULL;
  const FILETIME utc = {ticks.LowPart, ticks.HighPart};
  SYSTEMTIME universal = {};
  SYSTEMTIME local = {};
  if (!FileTimeToSystemTime(&utc, &universal) ||
      !SystemTimeToTzSpecificLocalTime(nullptr, &universal, &local)) {
    return {};
  }
  wchar_t date[80] = {};
  wchar_t time[40] = {};
  if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local,
                      nullptr, date, ARRAYSIZE(date), nullptr) == 0 ||
      GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local,
                      nullptr, time, ARRAYSIZE(time)) == 0) {
    return {};
  }
  return std::wstring(date) + L" " + time;
}

void AddRow(HWND list, const std::wstring& text) {
  SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
}

}  // namespace

struct NewSessionDialog::Shared {
  std::mutex lock;
  HWND dialog = nullptr;
};

struct NewSessionDialog::Listed {
  unsigned generation = 0;
  bool ok = false;
  std::vector<agent::Conversation> conversations;
};

struct NewSessionDialog::FoldersListed {
  std::vector<app::RecentFolder> folders;
};

struct NewSessionDialog::ModelsListed {
  int backend = -1;
  std::vector<agent::ModelChoice> models;
};

NewSessionDialog::NewSessionDialog(NewSession* session)
    : session_(session),
      shared_(std::make_shared<Shared>()),
      backendChosen_(session->backendTyped) {}

NewSessionDialog::~NewSessionDialog() {
  std::lock_guard<std::mutex> guard(shared_->lock);
  shared_->dialog = nullptr;
}

bool NewSessionDialog::OnInit() {
  LocalizeDialog(hwnd_, IDD_NEW_SESSION);
  {
    std::lock_guard<std::mutex> guard(shared_->lock);
    shared_->dialog = hwnd_;
  }
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
  // The folder first: its section may say the model.
  SetText(IDC_NEW_PROJECT, session_->project);
  ShowModels(selected);
  ListFolders();
  ListConversations();
  ListModels();
  if (session_->focusConversations) {
    SendMessageW(hwnd_, WM_NEXTDLGCTL,
                 reinterpret_cast<WPARAM>(Item(IDC_NEW_CONVERSATION)), TRUE);
    return true;
  }
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
  FillModels(backend);
  shownModel_ = ModelFor(backend);
  SetText(IDC_NEW_MODEL, shownModel_);
}

std::wstring NewSessionDialog::ModelFor(int backend) const {
  const auto& modelFor = session_->backends[backend].modelFor;
  return modelFor ? modelFor(Expanded(Trimmed(GetText(IDC_NEW_PROJECT))))
                  : std::wstring();
}

// The folder's section (claude-gui-lkk.56) is the reason, and only a field
// nobody touched follows it: a model typed and then a folder chosen is still
// the model typed.
void NewSessionDialog::FollowFolderModel() {
  const int backend = SelectedBackend();
  if (backend < 0 || Trimmed(GetText(IDC_NEW_MODEL)) != Trimmed(shownModel_)) {
    return;
  }
  const std::wstring model = ModelFor(backend);
  if (model == shownModel_) return;
  shownModel_ = model;
  SetText(IDC_NEW_MODEL, model);
}

// The drop-down only.  CB_RESETCONTENT empties the edit part as well, and the
// list may arrive while the reader is typing in it, so the text and the caret
// are put back as they were.
void NewSessionDialog::FillModels(int backend) {
  const HWND models = Item(IDC_NEW_MODEL);
  const std::wstring typed = GetText(IDC_NEW_MODEL);
  DWORD start = 0;
  DWORD end = 0;
  SendMessageW(models, CB_GETEDITSEL, reinterpret_cast<WPARAM>(&start),
               reinterpret_cast<LPARAM>(&end));
  SendMessageW(models, CB_RESETCONTENT, 0, 0);
  for (const std::string& name : session_->backends[backend].models) {
    const std::wstring wide = model::Utf16FromUtf8(name);
    SendMessageW(models, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(wide.c_str()));
  }
  SetText(IDC_NEW_MODEL, typed);
  SendMessageW(models, CB_SETEDITSEL, 0, MAKELPARAM(start, end));
}

// Codex has no names to suggest until a server says (claude-gui-lkk.58), and
// its server takes a second to start: the field is there to type in at once,
// and the list fills in when the answer comes.
void NewSessionDialog::ListModels() {
  for (size_t i = 0; i < session_->backends.size(); ++i) {
    const ModelLister& lister = session_->backends[i].listModels;
    if (!lister) continue;
    std::thread([shared = shared_, lister, backend = static_cast<int>(i)] {
      auto listed = std::make_unique<ModelsListed>();
      listed->backend = backend;
      if (!lister(&listed->models) || listed->models.empty()) return;
      std::lock_guard<std::mutex> guard(shared->lock);
      if (shared->dialog &&
          PostMessageW(shared->dialog, kMsgModels, 0,
                       reinterpret_cast<LPARAM>(listed.get()))) {
        (void)listed.release();  // the dialog owns it now
      }
    }).detach();
  }
}

// The ids, because the field's text goes to --model as it is.
void NewSessionDialog::TakeModels(const ModelsListed& listed) {
  if (listed.backend < 0 ||
      listed.backend >= static_cast<int>(session_->backends.size())) {
    return;
  }
  std::vector<std::string>& models = session_->backends[listed.backend].models;
  models.clear();
  for (const agent::ModelChoice& choice : listed.models) {
    models.push_back(choice.id);
  }
  if (listed.backend == SelectedBackend()) FillModels(listed.backend);
}

// Once per dialog: unlike the conversations, the list does not depend on
// any field.  Claude's half is a few hundred files' tails, Codex's a process
// that takes seconds on a cold start.
void NewSessionDialog::ListFolders() {
  const HWND list = Item(IDC_NEW_RECENT);
  if (!session_->folders) return;
  AddRow(list, i18n::Text(i18n::Str::kNewRecentListing));
  SendMessageW(list, LB_SETCURSEL, 0, 0);
  std::thread([shared = shared_, lister = session_->folders] {
    auto listed = std::make_unique<FoldersListed>();
    listed->folders = lister();
    std::lock_guard<std::mutex> guard(shared->lock);
    if (shared->dialog &&
        PostMessageW(shared->dialog, kMsgFolders, 0,
                     reinterpret_cast<LPARAM>(listed.get()))) {
      (void)listed.release();  // the dialog owns it now
    }
  }).detach();
}

// The folder in the field is selected if the list has it, so the reader
// arriving in the list starts where the dialog was preset, not at the top;
// LB_SETCURSEL notifies nobody, so a filled-in field stays as it was.
void NewSessionDialog::ShowFolders(std::vector<app::RecentFolder> folders) {
  folders_.clear();
  for (app::RecentFolder& folder : folders) {
    if (folder.backend >= 0 &&
        folder.backend < static_cast<int>(session_->backends.size())) {
      folders_.push_back(std::move(folder));
    }
  }
  const HWND list = Item(IDC_NEW_RECENT);
  const std::wstring current =
      win::FolderKey(Expanded(Trimmed(GetText(IDC_NEW_PROJECT))));
  int selected = -1;
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (size_t i = 0; i < folders_.size(); ++i) {
    const app::RecentFolder& folder = folders_[i];
    AddRow(list, i18n::Format(i18n::Str::kNewRecentRow,
                              {win::FolderName(folder.path),
                               session_->backends[folder.backend].agentName,
                               folder.path}));
    if (selected < 0 && win::FolderKey(folder.path) == current) {
      selected = static_cast<int>(i);
    }
  }
  if (folders_.empty()) AddRow(list, i18n::Text(i18n::Str::kNewRecentNone));
  // Never no selection: NVDA says "not selected" only after the row, and the
  // reader, already on the next arrow key, hears the first row twice.
  // An empty field takes the first row, so what is selected is what starts --
  // with an agent the reader typed on the line or already chose, the first
  // row of that agent, and none when it has no row: the list arrives seconds
  // after the dialog, and changing the agent then would also throw away the
  // model typed for it.
  const bool none = selected < 0;
  bool take = none && Trimmed(GetText(IDC_NEW_PROJECT)).empty();
  if (none) selected = 0;
  if (take && backendChosen_) {
    const int backend = SelectedBackend();
    take = false;
    for (size_t i = 0; i < folders_.size(); ++i) {
      if (folders_[i].backend == backend) {
        selected = static_cast<int>(i);
        take = true;
        break;
      }
    }
  }
  SendMessageW(list, LB_SETCURSEL, selected, 0);
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
  if (take) {
    TakeRecentFolder();
    ListConversations();
  }
}

// Only the fields: the conversations are looked for when the focus leaves
// the list, or at OK.  The model follows the agent only when the agent
// changes, so arrowing among one agent's folders keeps what was typed.
void NewSessionDialog::TakeRecentFolder() {
  const int row = static_cast<int>(
      SendMessageW(Item(IDC_NEW_RECENT), LB_GETCURSEL, 0, 0));
  if (row < 0 || row >= static_cast<int>(folders_.size())) return;
  const app::RecentFolder& folder = folders_[row];
  SetText(IDC_NEW_PROJECT, folder.path);
  if (folder.backend != SelectedBackend()) {
    SendMessageW(Item(IDC_NEW_BACKEND), CB_SETCURSEL, folder.backend, 0);
    ShowModels(folder.backend);
  }
}

// The focus goes back into the field, not onto the button: the picker
// closing is a focus change NVDA announces anyway, and landing in the field
// makes that announcement the path that was just chosen (invariant 6).
void NewSessionDialog::Browse() {
  const std::wstring chosen =
      win::PickFolder(hwnd_, i18n::Text(i18n::Str::kNewPickFolder),
                      Trimmed(GetText(IDC_NEW_PROJECT)));
  if (!chosen.empty()) SetText(IDC_NEW_PROJECT, chosen);
  ListConversations();
  SendMessageW(hwnd_, WM_NEXTDLGCTL,
               reinterpret_cast<WPARAM>(Item(IDC_NEW_PROJECT)), TRUE);
}

void NewSessionDialog::Refuse(int focus, const std::wstring& why) {
  MessageBoxW(hwnd_, why.c_str(), L"" APP_NAME, MB_OK | MB_ICONWARNING);
  SendMessageW(hwnd_, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(Item(focus)),
               TRUE);
}

int NewSessionDialog::SelectedBackend() const {
  const int backend = static_cast<int>(
      SendMessageW(Item(IDC_NEW_BACKEND), CB_GETCURSEL, 0, 0));
  if (backend < 0 || backend >= static_cast<int>(session_->backends.size())) {
    return -1;
  }
  return backend;
}

// Off this thread, because Claude's list is its files read on a cold disk --
// seconds for a project with a hundred conversations -- and Codex's is a
// process of its own (claude-gui-lkk.7.11.2).  Meanwhile the list says it
// is looking, and "Nový rozhovor" is there to be chosen already: the common
// Enter does not wait for anything.
void NewSessionDialog::ListConversations() {
  const int backend = SelectedBackend();
  const std::string name =
      backend < 0 ? std::string() : session_->backends[backend].name;
  const std::wstring project = Expanded(Trimmed(GetText(IDC_NEW_PROJECT)));
  if (name == listedBackend_ && project == listedProject_) return;
  listedBackend_ = name;
  listedProject_ = project;
  FollowFolderModel();
  ++generation_;
  conversations_.clear();

  const HWND list = Item(IDC_NEW_CONVERSATION);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  AddRow(list, i18n::Text(i18n::Str::kNewConversationNew));
  SendMessageW(list, LB_SETCURSEL, 0, 0);
  if (backend < 0 || project.empty() || !IsFolder(project)) return;
  const ConversationLister& lister = session_->backends[backend].list;
  if (!lister) return;
  AddRow(list, i18n::Text(i18n::Str::kNewConversationsListing));

  std::thread([shared = shared_, lister, project, generation = generation_] {
    auto listed = std::make_unique<Listed>();
    listed->generation = generation;
    listed->ok = lister(project, &listed->conversations);
    std::lock_guard<std::mutex> guard(shared->lock);
    if (shared->dialog &&
        PostMessageW(shared->dialog, kMsgListed, 0,
                     reinterpret_cast<LPARAM>(listed.get()))) {
      (void)listed.release();  // the dialog owns it now
    }
  }).detach();
}

// The selection goes back to "Nový rozhovor": the row the reader was on said
// it was still looking, and is gone.
void NewSessionDialog::ShowConversations(const Listed& listed) {
  conversations_ = listed.conversations;
  const HWND list = Item(IDC_NEW_CONVERSATION);
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  AddRow(list, i18n::Text(i18n::Str::kNewConversationNew));
  for (const agent::Conversation& conversation : conversations_) {
    AddRow(list, RowText(conversation));
  }
  if (!listed.ok) {
    AddRow(list, i18n::Text(i18n::Str::kNewConversationsFailed));
  }
  SendMessageW(list, LB_SETCURSEL, 0, 0);
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
}

std::wstring NewSessionDialog::RowText(
    const agent::Conversation& conversation) const {
  std::wstring prompt = model::Utf16FromUtf8(
      conversation.title.empty() ? conversation.firstPrompt : conversation.title);
  if (prompt.size() > kPromptChars) {
    prompt.resize(kPromptChars);
    prompt += L"…";
  }
  if (prompt.empty()) prompt = i18n::Text(i18n::Str::kNewConversationNoPrompt);
  std::wstring row = prompt;
  if (conversation.lastActive != 0) {
    const std::wstring when = LocalTimeText(conversation.lastActive);
    if (!when.empty()) row += L", " + when;
  }
  if (OpenIndex(listedBackend_, conversation.id) >= 0) {
    row = i18n::Format(i18n::Str::kNewConversationOpen, {row});
  }
  return row;
}

int NewSessionDialog::OpenIndex(const std::string& backend,
                                const std::wstring& id) const {
  const std::string wanted = model::Utf8FromUtf16(id);
  for (size_t i = 0; i < session_->open.size(); ++i) {
    const OpenConversation& open = session_->open[i];
    if (!open.id.empty() && open.backend == backend && open.id == wanted) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool NewSessionDialog::OnCommand(int id, int notification) {
  if (id == IDC_NEW_BACKEND && notification == CBN_SELCHANGE) {
    backendChosen_ = true;
    ShowModels(SelectedBackend());
    ListConversations();
    return true;
  }
  if (id == IDC_NEW_BROWSE && notification == BN_CLICKED) {
    Browse();
    return true;
  }
  if (id == IDC_NEW_PROJECT && notification == EN_KILLFOCUS) {
    ListConversations();
    return true;
  }
  if (id == IDC_NEW_RECENT && notification == LBN_SELCHANGE) {
    TakeRecentFolder();
    return true;
  }
  if (id == IDC_NEW_RECENT && notification == LBN_KILLFOCUS) {
    ListConversations();
    return true;
  }
  return false;
}

bool NewSessionDialog::OnMessage(UINT message, WPARAM, LPARAM lParam) {
  if (message == kMsgListed) {
    std::unique_ptr<Listed> listed(reinterpret_cast<Listed*>(lParam));
    if (listed->generation == generation_) ShowConversations(*listed);
    return true;
  }
  if (message == kMsgFolders) {
    std::unique_ptr<FoldersListed> listed(
        reinterpret_cast<FoldersListed*>(lParam));
    ShowFolders(std::move(listed->folders));
    return true;
  }
  if (message == kMsgModels) {
    std::unique_ptr<ModelsListed> listed(
        reinterpret_cast<ModelsListed*>(lParam));
    TakeModels(*listed);
    return true;
  }
  if (message == WM_DESTROY) {
    {
      std::lock_guard<std::mutex> guard(shared_->lock);
      shared_->dialog = nullptr;
    }
    // Posted before the door closed and not yet taken: the window takes its
    // queue with it, and these with it, unfreed.
    MSG pending;
    while (PeekMessageW(&pending, hwnd_, kMsgListed, kMsgListed, PM_REMOVE)) {
      delete reinterpret_cast<Listed*>(pending.lParam);
    }
    while (PeekMessageW(&pending, hwnd_, kMsgFolders, kMsgFolders,
                        PM_REMOVE)) {
      delete reinterpret_cast<FoldersListed*>(pending.lParam);
    }
    while (PeekMessageW(&pending, hwnd_, kMsgModels, kMsgModels, PM_REMOVE)) {
      delete reinterpret_cast<ModelsListed*>(pending.lParam);
    }
    return false;
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
  if (!IsFolder(project)) {
    Refuse(IDC_NEW_PROJECT,
           i18n::Format(i18n::Str::kNewFolderMissing, {project}));
    return false;
  }
  const int backend = SelectedBackend();
  if (backend < 0) {
    Refuse(IDC_NEW_BACKEND, i18n::Text(i18n::Str::kNewChooseBackend));
    return false;
  }
  // Enter straight from the folder field never left it, so the list may
  // still be the previous folder's.  Then whatever was chosen in it belonged
  // to another folder, and the list starts again at "Nový rozhovor".
  ListConversations();
  const int row = static_cast<int>(
      SendMessageW(Item(IDC_NEW_CONVERSATION), LB_GETCURSEL, 0, 0));
  session_->resumeId.clear();
  session_->switchTo = -1;
  if (row > 0) {
    // The row that says it is still looking, or that looking failed.
    if (row - 1 >= static_cast<int>(conversations_.size())) {
      Refuse(IDC_NEW_CONVERSATION,
             i18n::Text(i18n::Str::kNewChooseConversation));
      return false;
    }
    const agent::Conversation& chosen = conversations_[row - 1];
    session_->switchTo = OpenIndex(listedBackend_, chosen.id);
    if (session_->switchTo < 0) session_->resumeId = chosen.id;
  }
  session_->backend = session_->backends[backend].name;
  session_->model = Trimmed(GetText(IDC_NEW_MODEL));
  session_->project = project;
  return true;
}

}  // namespace ui
