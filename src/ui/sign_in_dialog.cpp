#include "ui/sign_in_dialog.h"

#include <shellapi.h>

#include <utility>

#include "i18n/i18n.h"
#include "model/utf.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"
#include "win/clipboard.h"

namespace ui {
namespace {

using i18n::Str;

std::wstring Trimmed(const std::wstring& text) {
  const size_t first = text.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) return {};
  const size_t last = text.find_last_not_of(L" \t\r\n");
  return text.substr(first, last - first + 1);
}

}  // namespace

SignInDialog::SignInDialog(std::wstring agentName, Actions actions)
    : agentName_(std::move(agentName)), actions_(std::move(actions)) {}

bool SignInDialog::OnInit() {
  LocalizeDialog(hwnd_, IDD_SIGN_IN);
  Refresh();
  return false;  // the status box is the first tab stop
}

std::wstring SignInDialog::StatusText() const {
  if (finished_) {
    if (finished_->signedIn) return i18n::Format(Str::kSignInDone, {agentName_});
    if (finished_->error.empty()) return i18n::Text(Str::kSignInFailedPlain);
    return i18n::Format(Str::kSignInFailed,
                        {model::Utf16FromUtf8(finished_->error)});
  }
  if (checking_) return i18n::Text(Str::kSignInChecking);
  if (!prompt_) return i18n::Format(Str::kSignInWaiting, {agentName_});
  if (!prompt_->userCode.empty()) {
    return i18n::Format(Str::kSignInDeviceCode,
                        {model::Utf16FromUtf8(prompt_->userCode)});
  }
  return i18n::Text(prompt_->wantsCode ? Str::kSignInPasteCode
                                       : Str::kSignInOpenOnly);
}

void SignInDialog::Refresh() {
  if (!hwnd_) return;
  SetTextLines(IDC_SIGNIN_STATUS, StatusText());
  SetText(IDC_SIGNIN_URL, prompt_ ? model::Utf16FromUtf8(prompt_->url) : L"");
  SetText(IDC_SIGNIN_CODE,
          prompt_ ? model::Utf16FromUtf8(prompt_->userCode) : L"");

  // Hidden rather than disabled: a field that can never hold anything for
  // this agent is one more stop on the way through the dialog.
  const bool code = prompt_ && !prompt_->userCode.empty();
  const bool answer = prompt_ && prompt_->wantsCode;
  const int show = SW_SHOWNA;
  ShowWindow(Item(IDC_SIGNIN_CODE_LABEL), code ? show : SW_HIDE);
  ShowWindow(Item(IDC_SIGNIN_CODE), code ? show : SW_HIDE);
  ShowWindow(Item(IDC_SIGNIN_COPY), code ? show : SW_HIDE);
  ShowWindow(Item(IDC_SIGNIN_ANSWER_LABEL), answer ? show : SW_HIDE);
  ShowWindow(Item(IDC_SIGNIN_ANSWER), answer ? show : SW_HIDE);
  ShowWindow(Item(IDC_SIGNIN_SUBMIT), answer ? show : SW_HIDE);

  // Once it is over there is nothing left to open or to type: what is left
  // is reading the outcome and closing.
  const bool running = !finished_;
  SetEnabled(IDC_SIGNIN_OPEN, prompt_ && running);
  SetEnabled(IDC_SIGNIN_COPY, running);
  SetEnabled(IDC_SIGNIN_ANSWER, running);
  SetEnabled(IDC_SIGNIN_SUBMIT, running);
  SetText(IDCANCEL,
          i18n::Text(running ? Str::kDlgSignInCancel : Str::kDlgClose));
}

std::wstring SignInDialog::ShowPrompt(const agent::SignInPrompt& prompt) {
  prompt_ = prompt;
  checking_ = false;
  Refresh();
  return StatusText();
}

std::wstring SignInDialog::ShowFinished(const agent::SignInFinished& finished) {
  finished_ = finished;
  checking_ = false;
  if (!hwnd_) return StatusText();
  // The status box takes the focus before the buttons go grey, so that the
  // focus is not left on a disabled one -- and NVDA reads a box the focus
  // lands in, which says the outcome.  When it is there already, nothing
  // moves and nothing is read, so the sentence goes back to be said.
  const bool there = Focused(IDC_SIGNIN_STATUS);
  Refresh();
  if (there) return StatusText();
  SendMessageW(hwnd_, WM_NEXTDLGCTL,
               reinterpret_cast<WPARAM>(Item(IDC_SIGNIN_STATUS)), TRUE);
  return {};
}

bool SignInDialog::Focused(int id) const {
  const HWND focus = GetFocus();
  return focus != nullptr && focus == Item(id);
}

bool SignInDialog::OnCommand(int id, int notification) {
  if (notification != BN_CLICKED) return false;
  switch (id) {
    case IDC_SIGNIN_OPEN:
      OpenInBrowser();
      return true;
    case IDC_SIGNIN_COPY:
      CopyCode();
      return true;
    case IDC_SIGNIN_SUBMIT:
      SubmitCode();
      return true;
    default:
      return false;
  }
}

// Enter: in the code field it hands the code over, anywhere else it opens
// the address, and once it is all over it closes.
bool SignInDialog::OnOk() {
  if (finished_) return true;
  if (Focused(IDC_SIGNIN_ANSWER)) {
    SubmitCode();
  } else {
    OpenInBrowser();
  }
  return false;
}

void SignInDialog::OpenInBrowser() {
  if (!prompt_ || prompt_->url.empty()) {
    actions_.announce(i18n::Text(Str::kSignInNoAddress));
    return;
  }
  // Only a web address.  The text comes from the CLI, and ShellExecute runs
  // whatever it is given -- a path to a program included.
  const std::wstring url = model::Utf16FromUtf8(prompt_->url);
  if (url.rfind(L"https://", 0) != 0) {
    actions_.announce(i18n::Text(Str::kSignInBadAddress));
    return;
  }
  // Nothing said on success: the browser takes the foreground, and NVDA
  // reads it.
  const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(
      nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (result <= 32) actions_.announce(i18n::Text(Str::kSignInOpenFailed));
}

void SignInDialog::CopyCode() {
  if (!prompt_ || prompt_->userCode.empty()) return;
  const bool copied =
      win::SetClipboardText(hwnd_, model::Utf16FromUtf8(prompt_->userCode));
  actions_.announce(i18n::Text(copied ? Str::kSignInCodeCopied
                                      : Str::kSignInCopyFailed));
}

void SignInDialog::SubmitCode() {
  if (checking_) {
    actions_.announce(i18n::Text(Str::kSignInChecking));
    return;
  }
  const std::wstring code = Trimmed(GetText(IDC_SIGNIN_ANSWER));
  if (code.empty()) {
    actions_.announce(i18n::Text(Str::kSignInNoCode));
    return;
  }
  if (!actions_.submit(model::Utf8FromUtf16(code))) {
    actions_.announce(i18n::Text(Str::kRequestFailed));
    return;
  }
  checking_ = true;
  Refresh();
  actions_.announce(i18n::Text(Str::kSignInChecking));
}

}  // namespace ui
