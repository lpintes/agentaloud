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

// A selection means somebody is working with the text, whatever else it looks
// like, and nothing here may move it.
bool HasSelection(HWND edit) {
  CHARRANGE range = {};
  SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
  return range.cpMin != range.cpMax;
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

  speech_.Open();

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
      SpeakAnswer();
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

void SessionPane::SpeakAnswer() {
  // Only what this turn produced, and only the answers -- not the thinking,
  // not the tool calls.  Those are mechanism; the reader asked for the answer.
  std::wstring answer;
  const std::vector<model::Block>& blocks = model_.blocks();
  for (size_t i = turnFirstBlock_; i < blocks.size(); ++i) {
    if (blocks[i].kind != model::BlockKind::AssistantText) continue;
    if (!answer.empty()) answer += L'\n';
    answer += blocks[i].body;
  }

  if (!answer.empty() && speech_.available()) {
    // No interrupt: this arrived on its own and must not cut across whatever
    // the reader was having read to them.
    speech_.Say(answer, false);
    return;
  }
  // Either there was no text answer -- a turn can end on a tool alone -- or
  // there is no screen reader listening.  A sound is then the only way to
  // know the turn is over without going to look.  MessageBeep rather than a
  // tone of our own: it goes through the system sounds, so it can be silenced
  // where everything else is.
  MessageBeep(MB_OK);
}

void SessionPane::ToggleBlockAtCaret() {
  const std::optional<size_t> index = model_.BlockAt(CaretOffset(transcript_));
  if (!index.has_value()) return;

  const model::Block& before = model_.blocks()[*index];
  if (!before.collapsible) {
    // Nothing behind the summary.  Say the line rather than saying nothing,
    // so a press is never answered with silence.
    speech_.Say(before.summary, true);
    return;
  }

  const bool collapsed = before.collapsed;
  Apply(model_.SetCollapsed(*index, !collapsed));

  // To the start of the block, always.  Collapsing can leave the caret past
  // the block's new end, and even when it does not, the line the reader wants
  // after pressing this is the one they acted on.
  const model::Block& after = model_.blocks()[*index];
  PutCaret(transcript_, after.start);

  // NVDA does not announce a caret it did not move itself -- confirmed by
  // trying it -- so moving the caret is no feedback at all.  Without this the
  // key answers with silence.  Interrupting is right here: the reader pressed
  // something and wants the answer to that press.
  speech_.Say(after.collapsed ? after.summary : after.summary + L", rozbalené",
              true);
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

  // Is the reader reading, or just listening?
  //
  // Sending moves the caret past the new prompt, so that the answer arrives
  // directly under it -- otherwise you walk through your own prompt to reach
  // the reply.  But it must not do that to somebody who is in the middle of
  // reading something further back; they would lose their place.
  //
  // "Caret at the end" alone does not decide it.  Once a turn has happened the
  // caret sits where we left it, just before that answer -- never at the end
  // -- so a reader who only listens and never touches the transcript would
  // have the caret pinned to the first answer of the day.  So the test is: at
  // the end, OR exactly where this left it last time and therefore untouched.
  // Anything else means they moved it themselves, and then it is theirs.
  const size_t caret = CaretOffset(transcript_);
  const bool following = !HasSelection(transcript_) &&
                         (caret >= model_.Text().size() || caret == anchor_);

  turnFirstBlock_ = model_.blocks().size();
  Apply(model_.AppendUserPrompt(text));
  if (following) {
    PutCaretAtEnd(transcript_);
    anchor_ = model_.Text().size();
  }

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
