#include "ui/session_pane.h"

#include <commctrl.h>
#include <richedit.h>

#include "model/utf.h"

namespace ui {
namespace {

constexpr int kIdTranscriptLabel = 1001;
constexpr int kIdTranscript = 1002;
constexpr int kIdPromptLabel = 1003;
constexpr int kIdPrompt = 1004;

constexpr int kMargin = 8;
constexpr int kLabelHeight = 18;
constexpr int kPromptLines = 5;

// What the reader thread hands across for a permission decision.  Lives on
// that thread's stack for the duration of the SendMessage, which is safe
// precisely because SendMessage does not return until we are done with it.
struct PendingPermission {
  const proto::PermissionRequest* request;
  proto::PermissionDecision decision;
};

int TextLength(HWND edit) {
  GETTEXTLENGTHEX request = {};
  request.flags = GTL_NUMCHARS | GTL_PRECISE;
  request.codepage = 1200;  // UTF-16, so this counts what our map counts
  return static_cast<int>(
      SendMessageW(edit, EM_GETTEXTLENGTHEX,
                   reinterpret_cast<WPARAM>(&request), 0));
}

// Replace a range without disturbing the reader.
//
// This is invariant 3 from CLAUDE.md and the reason the model hands back a
// minimal Edit rather than the whole text.  Three things move if left alone
// and all three are restored here: the caret, the selection, and the first
// visible line.  Redrawing is off across the change so none of it is seen.
void ApplyEdit(HWND edit, size_t start, size_t removed,
               const std::wstring& inserted) {
  CHARRANGE saved = {};
  SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&saved));
  const LRESULT firstVisible = SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);

  SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
  CHARRANGE target = {static_cast<LONG>(start),
                      static_cast<LONG>(start + removed)};
  SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&target));
  SendMessageW(edit, EM_REPLACESEL, FALSE,
               reinterpret_cast<LPARAM>(inserted.c_str()));
  SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&saved));

  const LRESULT nowVisible = SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
  if (nowVisible != firstVisible) {
    SendMessageW(edit, EM_LINESCROLL, 0,
                 static_cast<LPARAM>(firstVisible - nowVisible));
  }
  SendMessageW(edit, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(edit, nullptr, TRUE);
}

size_t CaretOffset(HWND edit) {
  CHARRANGE range = {};
  SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
  return range.cpMin < 0 ? 0 : static_cast<size_t>(range.cpMin);
}

void PutCaret(HWND edit, size_t offset) {
  CHARRANGE at = {static_cast<LONG>(offset), static_cast<LONG>(offset)};
  SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&at));
  SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

void PutCaretAtEnd(HWND edit) {
  const int length = TextLength(edit);
  CHARRANGE end = {length, length};
  SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
  SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

std::wstring GetText(HWND edit) {
  const int length = GetWindowTextLengthW(edit);
  if (length <= 0) return {};
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  const int copied = GetWindowTextW(edit, text.data(), length + 1);
  text.resize(copied < 0 ? 0 : static_cast<size_t>(copied));
  return text;
}

bool IsBlank(const std::wstring& text) {
  for (wchar_t character : text) {
    if (character != L' ' && character != L'\t' && character != L'\r' &&
        character != L'\n') {
      return false;
    }
  }
  return true;
}

}  // namespace

bool SessionPane::Create(HWND host, HINSTANCE instance) {
  host_ = host;

  // The shell font, so the controls match every other window and follow the
  // user's size.  A screen reader does not care, but a magnifier user does.
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
  font_ = CreateFontIndirectW(&metrics.lfMessageFont);

  // A static label immediately before an edit is what gives the edit its
  // accessible name; without one a screen reader announces only "edit".
  transcriptLabel_ = CreateWindowExW(
      0, L"STATIC", L"&Prepis:", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0,
      host, reinterpret_cast<HMENU>(kIdTranscriptLabel), instance, nullptr);

  transcript_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE |
          ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL,
      0, 0, 0, 0, host, reinterpret_cast<HMENU>(kIdTranscript), instance,
      nullptr);

  promptLabel_ = CreateWindowExW(
      0, L"STATIC", L"P&rompt (Ctrl+Enter odošle):",
      WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, host,
      reinterpret_cast<HMENU>(kIdPromptLabel), instance, nullptr);

  prompt_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE |
          ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
      0, 0, 0, 0, host, reinterpret_cast<HMENU>(kIdPrompt), instance, nullptr);

  if (!transcript_ || !prompt_) return false;

  for (HWND control : {transcriptLabel_, transcript_, promptLabel_, prompt_}) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  }
  // No limit.  The default 32k would silently truncate a long session, and
  // silent truncation of a transcript is the worst failure this can have.
  SendMessageW(transcript_, EM_EXLIMITTEXT, 0, 0x7FFFFFFF);

  SetWindowSubclass(prompt_, PromptProc, kIdPrompt,
                    reinterpret_cast<DWORD_PTR>(this));
  SetWindowSubclass(transcript_, TranscriptProc, kIdTranscript,
                    reinterpret_cast<DWORD_PTR>(this));
  return true;
}

void SessionPane::Layout(int width, int height) {
  const int promptHeight = kLabelHeight * kPromptLines;
  const int promptTop = height - kMargin - promptHeight;
  const int labelTop = promptTop - kLabelHeight;
  const int transcriptTop = kMargin + kLabelHeight;
  const int transcriptHeight = labelTop - transcriptTop - kMargin;
  const int usable = width - 2 * kMargin;

  MoveWindow(transcriptLabel_, kMargin, kMargin, usable, kLabelHeight, TRUE);
  MoveWindow(transcript_, kMargin, transcriptTop, usable,
             transcriptHeight > 0 ? transcriptHeight : 0, TRUE);
  MoveWindow(promptLabel_, kMargin, labelTop, usable, kLabelHeight, TRUE);
  MoveWindow(prompt_, kMargin, promptTop, usable, promptHeight, TRUE);
}

void SessionPane::Apply(const model::Edit& edit) {
  if (edit.empty()) return;
  ApplyEdit(transcript_, edit.start, edit.removed, edit.inserted);

  // The one invariant that cannot be unit tested: that the widget agrees with
  // the map.  If RichEdit ever counts a character differently -- a line break
  // it normalises, a character it substitutes -- navigation would land in the
  // wrong place and nothing else would say so.  Loud, in the title, because a
  // quiet log is a thing nobody reads.
  if (TextLength(transcript_) != static_cast<int>(model_.Text().size())) {
    SetWindowTextW(host_, L"ClaudeLens — NESÚLAD MAPY ROZSAHOV");
  }
}

bool SessionPane::Start(const proto::Session::Options& options) {
  return session_.Start(
      options,
      [this](const proto::Event& event) {
        // Reader thread.  Queue and wake the window; never touch a control.
        bool wake = false;
        {
          std::lock_guard<std::mutex> lock(queueMutex_);
          queue_.push_back(event);
          // One post per burst: a busy turn produces hundreds of records and
          // a message each would be its own kind of stall.
          wake = !drainPosted_;
          drainPosted_ = true;
        }
        if (wake) PostMessageW(host_, kMsgDrain, 0, 0);
      },
      [this](const proto::PermissionRequest& request) {
        PendingPermission pending;
        pending.request = &request;
        SendMessageW(host_, kMsgPermission, 0,
                     reinterpret_cast<LPARAM>(&pending));
        return pending.decision;
      });
}

void SessionPane::OnDrain() {
  std::vector<proto::Event> events;
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    events.swap(queue_);
    drainPosted_ = false;
  }
  for (const proto::Event& event : events) {
    Apply(model_.Append(event));
    if (event.kind == proto::EventKind::Result) {
      busy_ = false;
      SetStatus(L"hotové");
      // Docasne, kym krok 5 neprinesie rec (claude-gui-lkk.5).  Titulok okna
      // ziadna citacka sama necita, takze bez tohto niet ako zistit, ze tah
      // skoncil, inak nez sa chodit pozerat.  MessageBeep, nie vlastny ton:
      // ide cez systemove zvuky, takze sa da stisit tam, kde uzivatel stisuje
      // vsetko ostatne.
      MessageBeep(MB_OK);
    } else if (event.kind == proto::EventKind::SystemInit) {
      SetStatus(L"pripravené");
    }
  }
}

LRESULT SessionPane::OnPermission(LPARAM pointer) {
  PendingPermission* pending = reinterpret_cast<PendingPermission*>(pointer);
  const proto::PermissionRequest& request = *pending->request;

  // A MessageBox, not a dialog from a template, and only until step 6
  // (claude-gui-lkk.6).  It is here rather than nowhere because without it
  // every tool that hits an "ask" rule would be refused, and this milestone
  // is meant to be usable for a day's work.  What it cannot do is let the
  // command be read line by line, which is the whole point of the real one.
  std::wstring text = L"Nástroj: " + model::Utf16FromUtf8(request.toolName);
  if (!request.decisionReasonType.empty()) {
    text += L"  (dôvod: " + model::Utf16FromUtf8(request.decisionReasonType) +
            L")";
  }
  text += L"\n\n" + model::Utf16FromUtf8(request.input.dump(2));
  text += L"\n\nPovoliť?";

  const int answer = MessageBoxW(host_, text.c_str(), L"ClaudeLens — povolenie",
                                 MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
  pending->decision.allow = answer == IDYES;
  pending->decision.denyMessage =
      "Používateľ to zamietol. Nepokračuj a spýtaj sa, čo ďalej.";
  return 0;
}

void SessionPane::ToggleBlockAtCaret() {
  const std::optional<size_t> index = model_.BlockAt(CaretOffset(transcript_));
  if (!index.has_value()) return;

  const bool collapsed = model_.blocks()[*index].collapsed;
  Apply(model_.SetCollapsed(*index, !collapsed));

  // To the start of the block, always.  Collapsing can leave the caret past
  // the block's new end, and even when it does not, the line the reader wants
  // after pressing this is the one they acted on.  The move is also the only
  // feedback there is until step 5 brings speech: a screen reader announces
  // the line the caret lands on, so it says either the summary or the first
  // line of what just appeared.
  PutCaret(transcript_, model_.blocks()[*index].start);
}

void SessionPane::SetStatus(std::wstring text) {
  status_ = std::move(text);
  SetWindowTextW(host_, (L"ClaudeLens — " + status_).c_str());
}

void SessionPane::FocusPrompt() const { SetFocus(prompt_); }

void SessionPane::Send() {
  if (busy_) return;
  const std::wstring text = GetText(prompt_);
  if (IsBlank(text)) return;

  Apply(model_.AppendUserPrompt(text));
  // Past the prompt that was just added, so the answer arrives directly under
  // the caret.
  //
  // This does not contradict "the caret never moves on its own" -- it sharpens
  // it.  The rule is about text ARRIVING; sending is something the reader did,
  // and after doing it the thing they want to read next is what comes back,
  // not what they themselves just wrote.  Leaving the caret alone meant having
  // to walk through your own prompt to reach the answer.
  //
  // Considered and rejected: doing this only when the caret was already at the
  // end.  It sounds more careful and is worse -- after the first turn the
  // caret sits at the start of that answer, never at the end, so the condition
  // would hold once and never again.  Getting back to a place left behind is
  // what Ctrl+0 is for (claude-gui-lkk.5).
  PutCaretAtEnd(transcript_);

  session_.SendPrompt(model::Utf8FromUtf16(text));
  SetWindowTextW(prompt_, L"");
  busy_ = true;
  SetStatus(L"pracujem");
}

LRESULT CALLBACK SessionPane::PromptProc(HWND window, UINT message,
                                         WPARAM wParam, LPARAM lParam,
                                         UINT_PTR id, DWORD_PTR data) {
  SessionPane* pane = reinterpret_cast<SessionPane*>(data);
  if (message == WM_KEYDOWN) {
    const bool control = GetKeyState(VK_CONTROL) < 0;
    if (wParam == VK_RETURN && control) {
      pane->Send();
      return 0;
    }
    if (wParam == VK_TAB) {
      // Two controls, so Tab is a toggle.  Done here rather than through
      // IsDialogMessage because win::RunMessageLoop is shared with another
      // project and this must not change how it behaves there.
      SetFocus(pane->transcript_);
      return 0;
    }
  }
  if (message == WM_CHAR && wParam == VK_TAB) return 0;  // no tab character
  if (message == WM_DESTROY) RemoveWindowSubclass(window, PromptProc, id);
  return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK SessionPane::TranscriptProc(HWND window, UINT message,
                                             WPARAM wParam, LPARAM lParam,
                                             UINT_PTR id, DWORD_PTR data) {
  SessionPane* pane = reinterpret_cast<SessionPane*>(data);
  if (message == WM_KEYDOWN && wParam == VK_TAB) {
    SetFocus(pane->prompt_);
    return 0;
  }
  if (message == WM_KEYDOWN && wParam == VK_RETURN) {
    pane->ToggleBlockAtCaret();
    return 0;
  }
  if (message == WM_DESTROY) RemoveWindowSubclass(window, TranscriptProc, id);
  return DefSubclassProc(window, message, wParam, lParam);
}

}  // namespace ui
