#include "ui/session_pane.h"

#include <commctrl.h>
#include <mmsystem.h>
#include <richedit.h>

#include <algorithm>
#include <ctime>

#include "i18n/i18n.h"
#include "model/history.h"
#include "model/utf.h"
#include "ui/ask_dialog.h"
#include "ui/command_dialog.h"
#include "ui/find_dialog.h"
#include "ui/keys_dialog.h"
#include "ui/permission_dialog.h"
#include "ui/resource.h"
#include "win/clipboard.h"

namespace ui {

using i18n::Str;

namespace {

constexpr int kIdTranscriptLabel = 1001;
constexpr int kIdTranscript = 1002;
constexpr int kIdPromptLabel = 1003;
constexpr int kIdPrompt = 1004;

// The end of a turn when the window is not in front.  Deliberately not the
// asterisk: that one already answers a narrower question ("the turn ended and
// nothing was said"), and a sound that answers two questions answers neither.
constexpr UINT kBackgroundEndSound = MB_ICONEXCLAMATION;

constexpr int kMargin = 8;
constexpr int kLabelHeight = 18;
constexpr int kPromptLines = 5;

// What the reader thread hands across for a decision.  Lives on that thread's
// stack for the duration of the SendMessage, which is safe precisely because
// SendMessage does not return until we are done with it.
struct PendingPermission {
  const agent::PermissionRequest* request;
  agent::PermissionAnswer answer;
};

struct PendingQuestion {
  const agent::QuestionRequest* request;
  agent::QuestionAnswer answer;
};

int TextLength(HWND edit) {
  GETTEXTLENGTHEX request = {};
  request.flags = GTL_NUMCHARS | GTL_PRECISE;
  request.codepage = 1200;  // UTF-16, so this counts what our map counts
  return static_cast<int>(
      SendMessageW(edit, EM_GETTEXTLENGTHEX,
                   reinterpret_cast<WPARAM>(&request), 0));
}

// Where an offset ends up after an edit.  Everything after the replaced range
// slides by the difference; everything before it stays.  An offset inside the
// replaced range has nowhere to be, so it goes to the start of it.
LONG MoveOffset(LONG offset, size_t start, size_t removed, size_t inserted) {
  const LONG from = static_cast<LONG>(start);
  const LONG to = static_cast<LONG>(start + removed);
  if (offset <= from) return offset;
  if (offset < to) return from;
  return offset + static_cast<LONG>(inserted) - static_cast<LONG>(removed);
}

// Replace a range without disturbing the reader.
//
// This is invariant 3 from CLAUDE.md and the reason the model hands back a
// minimal Edit rather than the whole text.  Three things move if left alone
// and all three are put back here: the caret, the selection, and the first
// visible line.  Redrawing is off across the change so none of it is seen.
//
// "Put back" is not the same as "restored to the number it had".  An edit that
// lands ABOVE the reader -- which is what inserting a tool result behind its
// call does -- pushes their line down, and holding the old offset would leave
// them staring at different text.  So every position is carried through
// MoveOffset, and the first visible line is carried as the character it starts
// at rather than as a line number, since the lines above it have just changed
// in number.
void ApplyEdit(HWND edit, size_t start, size_t removed,
               const std::wstring& inserted) {
  CHARRANGE saved = {};
  SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&saved));
  const LRESULT firstVisible = SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
  const LRESULT firstVisibleChar =
      SendMessageW(edit, EM_LINEINDEX, static_cast<WPARAM>(firstVisible), 0);

  SendMessageW(edit, WM_SETREDRAW, FALSE, 0);
  CHARRANGE target = {static_cast<LONG>(start),
                      static_cast<LONG>(start + removed)};
  SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&target));
  SendMessageW(edit, EM_REPLACESEL, FALSE,
               reinterpret_cast<LPARAM>(inserted.c_str()));

  CHARRANGE moved = {
      MoveOffset(saved.cpMin, start, removed, inserted.size()),
      MoveOffset(saved.cpMax, start, removed, inserted.size())};
  SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&moved));

  if (firstVisibleChar >= 0) {
    const LONG wanted = MoveOffset(static_cast<LONG>(firstVisibleChar), start,
                                   removed, inserted.size());
    const LRESULT wantedLine =
        SendMessageW(edit, EM_EXLINEFROMCHAR, 0, static_cast<LPARAM>(wanted));
    const LRESULT nowVisible = SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (nowVisible != wantedLine) {
      SendMessageW(edit, EM_LINESCROLL, 0,
                   static_cast<LPARAM>(wantedLine - nowVisible));
    }
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

// Ctrl+Shift+<letter> reaches the same jump from the prompt box, so that going
// to look at what the last tool did does not cost the prompt being typed.  It
// has to be read as a key and not as a character: Ctrl with a letter arrives
// in WM_CHAR as a control code, with nothing left to switch on.  The VK codes
// for these six letters sit where the US layout puts them on the Slovak one
// too -- QWERTZ moves only Y and Z.
bool IsJumpChord(WPARAM key) {
  if (GetKeyState(VK_CONTROL) >= 0 || GetKeyState(VK_SHIFT) >= 0) return false;
  return key == 'T' || key == 'R' || key == 'P' || key == 'A' || key == 'K' ||
         key == 'E';
}

// Ctrl+Shift+C, read as a key for the same reason as the jump chords: with
// Ctrl down there is no character left in WM_CHAR to switch on.  Not plain
// Ctrl+C -- in the transcript that is RichEdit's own "copy the selection", and
// taking it would be taking away the obvious way to quote what is on screen.
bool IsCopyIdChord(WPARAM key) {
  if (GetKeyState(VK_CONTROL) >= 0 || GetKeyState(VK_SHIFT) >= 0) return false;
  return key == 'C';
}

// Ctrl+F, without Shift: Ctrl+Shift+F is free for something else later, and
// taking it here as well would be taking it for nothing.
bool IsFindChord(WPARAM key) {
  return key == 'F' && GetKeyState(VK_CONTROL) < 0 &&
         GetKeyState(VK_SHIFT) >= 0;
}

// Case-blind in the reader's language.  CharLowerBuffW keeps the length,
// which Transcript::Find needs: its offsets are into the unfolded text.
std::wstring FoldCase(const std::wstring& text) {
  std::wstring folded = text;
  if (!folded.empty()) {
    CharLowerBuffW(folded.data(), static_cast<DWORD>(folded.size()));
  }
  return folded;
}

// Ctrl+<digit> and Ctrl+Shift+<digit>, read as keys for the same reason -- and
// with more force here, because the Slovak top row does not produce digits at
// all without Shift.  VK_0..VK_9 are positional and do.
//
// The numeric keypad is deliberately not accepted: NVDA's desktop layout owns
// it, and a bookmark that works or not depending on the screen reader's layout
// is worse than one key too few.
constexpr size_t kNoSlot = static_cast<size_t>(-1);

size_t BookmarkSlot(WPARAM key) {
  if (GetKeyState(VK_CONTROL) >= 0) return kNoSlot;
  if (key < '0' || key > '9') return kNoSlot;
  return static_cast<size_t>(key - '0');
}

// A program missing from PATH is the likely case and gets a sentence that says
// what to do; anything else is the system's own words, with the number to
// search for.  The folder is not among the likely ones -- the command line
// and the dialog both check it before a session is started.
std::wstring StartFailureText(const agent::StartFailure& failure) {
  if (failure.systemError == ERROR_FILE_NOT_FOUND ||
      failure.systemError == ERROR_PATH_NOT_FOUND) {
    return i18n::Format(Str::kProgramNotFound, {failure.program});
  }
  wchar_t* buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, failure.systemError, 0, reinterpret_cast<wchar_t*>(&buffer), 0,
      nullptr);
  std::wstring reason(buffer, length);
  LocalFree(buffer);
  while (!reason.empty() && iswspace(reason.back())) reason.pop_back();
  return i18n::Format(Str::kProgramNotStarted,
                      {failure.program, std::to_wstring(failure.systemError),
                       reason});
}

}  // namespace

bool SessionPane::Create(HWND host, HINSTANCE instance) {
  host_ = host;

  // A static label immediately before an edit is what gives the edit its
  // accessible name; without one a screen reader announces only "edit".
  transcriptLabel_ = CreateWindowExW(
      0, L"STATIC", i18n::Text(Str::kLabelTranscript), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0,
      host, reinterpret_cast<HMENU>(kIdTranscriptLabel), instance, nullptr);

  transcript_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE |
          ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL,
      0, 0, 0, 0, host, reinterpret_cast<HMENU>(kIdTranscript), instance,
      nullptr);

  promptLabel_ = CreateWindowExW(
      0, L"STATIC", i18n::Text(Str::kLabelPrompt),
      WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, host,
      reinterpret_cast<HMENU>(kIdPromptLabel), instance, nullptr);

  prompt_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE |
          ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
      0, 0, 0, 0, host, reinterpret_cast<HMENU>(kIdPrompt), instance, nullptr);

  if (!transcript_ || !prompt_) return false;

  ApplyFont();
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

// The shell font, so the controls match every other window and follow the
// user's size.  A screen reader does not care, but a magnifier user does.
//
// Asked for THIS window's dpi rather than the system's: the process is
// per-monitor aware, so the same window can be at 96 on one screen and 144 on
// the next, and SystemParametersInfoW would answer for neither.
void SessionPane::ApplyFont() {
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                             &metrics, 0, Dpi());
  const HFONT wanted = CreateFontIndirectW(&metrics.lfMessageFont);
  if (!wanted) return;

  for (HWND control : {transcriptLabel_, transcript_, promptLabel_, prompt_}) {
    if (control) {
      SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(wanted), TRUE);
    }
  }
  // Only after nothing is drawing with it any more.
  if (font_) DeleteObject(font_);
  font_ = wanted;
}

UINT SessionPane::Dpi() const {
  const UINT dpi = host_ ? GetDpiForWindow(host_) : 0;
  return dpi != 0 ? dpi : 96;
}

void SessionPane::OnDpiChanged() {
  ApplyFont();
  // The layout follows from the host's WM_SIZE, which the move that comes
  // with WM_DPICHANGED sends anyway.
}

void SessionPane::Layout(int width, int height) {
  // The constants are in 96-DPI units; nothing scales them for us, because a
  // DPI-aware process is not scaled by Windows.  Without this the margins and
  // the label strip stay at their 96-DPI size while the font grows into them,
  // and the labels come out clipped.
  const UINT dpi = Dpi();
  const int margin = MulDiv(kMargin, dpi, 96);
  const int labelHeight = MulDiv(kLabelHeight, dpi, 96);

  const int promptHeight = labelHeight * kPromptLines;
  const int promptTop = height - margin - promptHeight;
  const int labelTop = promptTop - labelHeight;
  const int transcriptTop = margin + labelHeight;
  const int transcriptHeight = labelTop - transcriptTop - margin;
  const int usable = width - 2 * margin;

  MoveWindow(transcriptLabel_, margin, margin, usable, labelHeight, TRUE);
  MoveWindow(transcript_, margin, transcriptTop, usable,
             transcriptHeight > 0 ? transcriptHeight : 0, TRUE);
  MoveWindow(promptLabel_, margin, labelTop, usable, labelHeight, TRUE);
  MoveWindow(prompt_, margin, promptTop, usable, promptHeight, TRUE);
}

void SessionPane::Apply(const model::Edit& edit) {
  if (edit.empty()) return;
  // "The caret is at the end" is true only until the next append, and the
  // append is exactly what we are here for.  AppendBlocks starts its edit at
  // the old end, and MoveOffset leaves everything at or before the start
  // alone -- so a caret standing there ends up ABOVE the text that just
  // arrived and matches neither test in Following() any more.  A reader who
  // pressed Ctrl+End to catch up was therefore heard once and then went
  // silent for the rest of the session, and the next prompt no longer moved
  // their caret either.  So the moment is taken here, while it is still true,
  // and turned into the anchor, which the line below then carries along like
  // any other offset.
  //
  // The model has already applied this edit -- Append() returns what it did --
  // so the end to compare against is the one before it.
  const size_t before =
      model_.Text().size() + edit.removed - edit.inserted.size();
  if (transcript_ != nullptr && !HasSelection(transcript_) &&
      CaretOffset(transcript_) >= before) {
    anchor_ = CaretOffset(transcript_);
  }
  ApplyEdit(transcript_, edit.start, edit.removed, edit.inserted);
  // The anchor is an offset like any other, and an edit above it moves it.
  // Left behind, it would make the next prompt compare the caret against a
  // stale number, decide the reader had moved it, and stop following.
  anchor_ = static_cast<size_t>(MoveOffset(static_cast<LONG>(anchor_),
                                           edit.start, edit.removed,
                                           edit.inserted.size()));

  // The one invariant that cannot be unit tested: that the widget agrees with
  // the map.  If RichEdit ever counts a character differently -- a line break
  // it normalises, a character it substitutes -- navigation would land in the
  // wrong place and nothing else would say so.  Loud, in the title, because a
  // quiet log is a thing nobody reads.
  if (!mismatch_ &&
      TextLength(transcript_) != static_cast<int>(model_.Text().size())) {
    // The session's own title, which the frame shows beside its name: the
    // mismatch is this session's, and the folder says which one that is.
    mismatch_ = true;
    ShowName();
  }
}

void SessionPane::SetOrdinal(int ordinal) {
  if (ordinal == ordinal_) return;
  ordinal_ = ordinal;
  ShowName();
}

void SessionPane::ShowName() {
  const auto numbered = [this](const std::wstring& name) {
    return ordinal_ == 0 ? name
                         : i18n::Format(Str::kSessionOrdinal,
                                        {name, std::to_wstring(ordinal_)});
  };
  SetField(StatusBar::kProject,
           i18n::Format(Str::kStatusProject, {numbered(project_)}));
  // The path alone, numbered: the frame puts its own name in front of the
  // active session's title, and the window list in the menu names sessions
  // by it.
  std::wstring title = numbered(path_);
  if (mismatch_) {
    title += L" — ";
    title += i18n::Text(Str::kTitleRangeMismatch);
  }
  SetWindowTextW(host_, title.c_str());
}

bool SessionPane::Start(std::unique_ptr<agent::Backend> backend,
                        const agent::StartOptions& options,
                        std::wstring* failure) {
  backend_ = std::move(backend);
  // Before anything can make a block, a restored history included: the name
  // is part of every answer's rendered text.
  model_.SetAgentName(
      model::Utf16FromUtf8(backend_->capabilities().agentName));
  // The folder name in the bar, the whole path in the title.  The bar is read
  // out in one breath along with three other fields, and a path of eight
  // components there buries everything after it; the title is announced when
  // the window takes focus and nowhere else, which is exactly the right place
  // for the answer to "which checkout is this".
  std::wstring path = options.projectDir;
  while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
    path.pop_back();
  }
  const size_t slash = path.find_last_of(L"\\/");
  project_ = slash == std::wstring::npos ? path : path.substr(slash + 1);
  // The dialog gets the whole path, for the same reason the title does: it is
  // read on request and not in one breath with three other fields.
  details_.project = path;
  details_.permissionMode = model::Utf16FromUtf8(options.mode);
  // What was asked for, until the stream says what it actually got.  An alias
  // ("sonnet") is not the id the usage records are keyed by, but it is only
  // ever read by a human here; which model's usage to read is the backend's
  // question, and it answers it with the real id.
  details_.model = options.model;
  details_.requestedModel = options.model;
  path_ = path;
  ShowName();

  agent::Backend::Callbacks callbacks;
  callbacks.onEvents = [this](std::vector<agent::Event> batch) {
    // Reader thread.  Queue and wake the window; never touch a control.
    bool wake = false;
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      queue_.push_back(std::move(batch));
      // One post per burst: a busy turn produces hundreds of records and a
      // message each would be its own kind of stall.
      wake = !drainPosted_;
      drainPosted_ = true;
    }
    if (wake) PostMessageW(host_, kMsgDrain, 0, 0);
  };
  callbacks.onHistory = [this](std::vector<agent::Event> events) {
    // Claude hands it over from inside Start, on this very thread and before
    // there is a process -- then it goes in now, ahead of everything, which
    // is also what keeps the bar below from showing the asked-for model when
    // the history already knows better.  A backend that has to ask its CLI
    // for the history gets it on the reader thread, and then it waits for
    // the window like everything else from there.
    if (GetWindowThreadProcessId(host_, nullptr) == GetCurrentThreadId()) {
      RestoreHistory(events);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(queueMutex_);
      history_ = std::move(events);
    }
    PostMessageW(host_, kMsgHistory, 0, 0);
  };
  callbacks.onPermission = [this](const agent::PermissionRequest& request) {
    PendingPermission pending{&request, {}};
    SendMessageW(host_, kMsgPermission, 0, reinterpret_cast<LPARAM>(&pending));
    return pending.answer;
  };
  callbacks.onQuestion = [this](const agent::QuestionRequest& request) {
    PendingQuestion pending{&request, {}};
    SendMessageW(host_, kMsgQuestion, 0, reinterpret_cast<LPARAM>(&pending));
    return pending.answer;
  };

  // What is known already goes into the bar now: the first system/init comes
  // only with the first turn, and a bar that says nothing about the mode until
  // then cannot answer "am I in plan mode" when it is asked -- before the first
  // prompt is exactly when it is.  After Start, because a history handed over
  // inside it may have named the model.
  const bool started = backend_->Start(options, std::move(callbacks));
  if (!started) {
    if (failure) *failure = StartFailureText(backend_->startFailure());
    return false;
  }
  // The mode as the backend holds it, which may be spelled differently from
  // the command line ("manual" is Claude's other name for "default").  Taken
  // over without a word -- nothing has changed -- so that the first report of
  // the mode does not read as a change.
  if (const std::string mode = backend_->mode(); !mode.empty()) {
    details_.permissionMode = model::Utf16FromUtf8(mode);
  }
  RefreshModelField();
  return true;
}

void SessionPane::RestoreHistory(const std::vector<agent::Event>& events) {
  // One edit for the lot.  The replay only ever adds after what is already in
  // the buffer -- a tool result is filed behind its own call, and every call it
  // could be filed behind arrived in the same replay -- so the whole of it is
  // one contiguous insertion at the old end.  Applying a few thousand edits one
  // at a time would be a slow way to the same text, and each of them would go
  // through EM_REPLACESEL and the range check.
  const size_t before = model_.Text().size();
  const model::HistoryCounts counts = model::RestoreHistory(events, &model_);
  // The model that answered last before the session was closed is the best
  // thing known until this process says otherwise.
  for (const agent::Event& event : events) {
    if (const auto* changed = std::get_if<agent::ModelChanged>(&event)) {
      details_.model = model::Utf16FromUtf8(changed->model);
    }
  }
  if (model_.Text().size() > before && counts.blocks > 0) {
    model::Edit edit;
    edit.start = before;
    edit.inserted = model_.Text().substr(before);
    Apply(edit);
  }
  // At the end, not at offset zero.  A conversation is resumed in order to
  // carry it on, so the caret belongs where it will be carried on from --
  // otherwise the first thing the reader has to do is walk past everything
  // they have already read to reach what comes next.  What is behind them is
  // not lost: Ctrl+Home leads to the first prompt, which is the answer to
  // "which conversation is this".
  //
  // The anchor goes with it, because "at the end" survives no append on its
  // own (see Apply) and the first turn's running commentary depends on it --
  // invariant 11.
  PutCaretAtEnd(transcript_);
  anchor_ = model_.Text().size();
}

void SessionPane::OnHistoryPosted() {
  std::vector<agent::Event> events;
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    events.swap(history_);
  }
  RestoreHistory(events);
  RefreshModelField();
}

void SessionPane::OnDrain() {
  std::vector<std::vector<agent::Event>> batches;
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    batches.swap(queue_);
    drainPosted_ = false;
  }
  // Where the reader is standing at the moment this batch arrives.  Taken
  // before anything is applied, and kept only if the batch really did add
  // something -- a drain that produced no block is not "new arrived", and
  // overwriting slot 0 with it would cost the reader the place they wanted.
  const model::Mark reading = model::MarkAt(model_, CaretOffset(transcript_));
  const size_t blocksBefore = model_.blocks().size();
  // The newest mode the drain carried.  Followed once, after the lot: two
  // changes in one drain are said as where they ended up.
  std::optional<std::string> mode;

  for (const std::vector<agent::Event>& batch : batches) {
    // Taken before the blocks are made, so that what the batch added can be
    // told from what was there.  An id and not a count: a tool result is
    // inserted behind its call, so the new blocks are not the tail.
    const size_t idBefore = model_.nextBlockId();
    for (const model::Edit& edit : model_.Append(batch)) Apply(edit);
    // Whatever the batch added, in the order it was added: the assistant's
    // text, a tool call, a tool result.  What each kind of block is worth
    // saying is AnnounceProgress's business, and a batch that added nothing
    // has nothing for it to say.
    if (model_.nextBlockId() != idBefore && WantsProgressSpeech()) {
      AnnounceProgress(idBefore);
    }

    for (const agent::Event& event : batch) {
      if (const auto* changed = std::get_if<agent::ModelChanged>(&event)) {
        ShowModel(changed->model);
      } else if (const auto* context = std::get_if<agent::ContextUsed>(&event)) {
        details_.contextTokens = context->tokens;
      } else if (std::holds_alternative<agent::ThinkingTick>(event)) {
        if (!thinkingSaid_ && WantsProgressSpeech()) {
          // Once per stretch of thinking, not once per tick -- there are
          // dozens of these per turn.  Cleared by anything else that speaks,
          // so a turn that thinks, calls a tool and thinks again says it
          // twice, which is what is happening.
          thinkingSaid_ = true;
          if (speech_.available()) speech_.Say(i18n::Text(Str::kSayThinking), false);
        }
      } else if (const auto* usage = std::get_if<agent::UsageChanged>(&event)) {
        // Overwritten, not added to: it is the session's running total, so
        // each report is the whole answer.
        details_.usage = usage->usage;
        details_.haveUsage = true;
      } else if (const auto* asked =
                     std::get_if<agent::QuestionByPrompt>(&event)) {
        questionsByPrompt_.push_back(*asked);
      } else if (std::holds_alternative<agent::TurnEnded>(event)) {
        OnTurnEnded();
      } else if (std::holds_alternative<agent::SessionEnded>(event)) {
        OnSessionEnded();
      } else if (const auto* limit =
                     std::get_if<agent::RateLimitChanged>(&event)) {
        ShowRateLimit(*limit);
      } else if (const auto* changed = std::get_if<agent::ModeChanged>(&event)) {
        mode = changed->id;
      }
    }
  }
  if (mode) FollowPermissionMode(*mode);

  if (model_.blocks().size() != blocksBefore) bookmarks_.Set(0, reading);
}

void SessionPane::OnTurnEnded() {
  busy_ = false;
  // Empty, not "done".  Done says nothing a reader can use -- what was done,
  // and when?  The field is there to answer "is it working right now", and the
  // answer to that, once the turn is over, is nothing.
  //
  // Only here and in Send.  The start of a turn is not a place to touch it:
  // system/init arrives at the start of every turn, and clearing the field
  // there once wiped out the "pracujem" that Send had just written, so the bar
  // stayed blank for the whole turn.
  SetStatus(L"");
  // A turn that ended on a question waiting for the next prompt ends in the
  // dialog for it, and the dialog is what is heard -- its caption and the
  // question.  "hotovo" first would be cut off by the focus moving to it
  // (invariant 6, nothing is said behind a dialog).  A turn the reader
  // stopped does not get one: Esc means they are done with it.
  if (!questionsByPrompt_.empty() && !interrupted_) {
    PostMessageW(host_, kMsgQuestionByPrompt, 0, 0);
    return;
  }
  questionsByPrompt_.clear();
  // A turn the reader stopped by hand ends differently from one that
  // finished, and the difference has to be audible: "prerušujem" answers the
  // key, this answers the turn, and without it a turn that stopped sounds like
  // one that never got the request.  It arrived on its own, so it queues
  // rather than cutting in -- invariant 7.
  //
  // Whatever the turn had already said stays said.  It arrived before Esc did,
  // and unsaying it is not on offer anyway -- see invariant 7 on why nothing
  // that came on its own may cut into the queue.
  //
  // Behind the window it is a sound, like any other end of a turn -- see
  // SignalTurnEnd for why speech does not carry across to a background window
  // at all.
  if (interrupted_) {
    interrupted_ = false;
    if (InForeground() && speech_.available()) {
      speech_.Say(i18n::Text(Str::kSayInterrupted), false);
    } else {
      MessageBeep(InForeground() ? MB_ICONASTERISK : kBackgroundEndSound);
    }
  } else {
    SignalTurnEnd();
  }
}

void SessionPane::OnSessionEnded() {
  ended_ = true;
  // A turn the process died in gets no TurnEnded; it is over all the same,
  // and nothing it was waiting for will come.
  busy_ = false;
  interrupted_ = false;
  questionsByPrompt_.clear();
  // Not cleared like the end of a turn.  An empty field says "nothing is
  // running, ask away", and here the second half is no longer true.
  SetStatus(i18n::Text(Str::kStatusAgentEnded));
  // The first line of the block, queued -- it arrived on its own (invariant
  // 7) -- and in the foreground whatever the caret is doing.  Invariant 11
  // keeps progress quiet for a reader who is reading elsewhere, but this is
  // not progress: it is the one fact that changes what every key does next,
  // and nothing later would say it.  The CLI's own words stay in the block,
  // where they can be read line by line; spoken, a stack trace is noise.
  // Behind the window it is the sound of a turn ending there, which it is.
  const std::vector<model::Block>& blocks = model_.blocks();
  if (InForeground() && speech_.available() && !blocks.empty() &&
      blocks.back().kind == model::BlockKind::SessionEnded) {
    speech_.Say(blocks.back().summary, false);
  } else {
    MessageBeep(InForeground() ? MB_ICONASTERISK : kBackgroundEndSound);
  }
}

void SessionPane::SignalWaiting() const {
  // Nothing to do when the window is in front: the box takes the focus and
  // NVDA reads it, which is the whole of the notification.
  if (InForeground()) return;
  const HWND top = host_ ? GetAncestor(host_, GA_ROOT) : nullptr;
  if (top == nullptr) return;

  // Measured 2026-09-05, and it corrected the assumption this was filed under:
  // Windows does NOT let a background process take the foreground, so the box
  // does not interrupt anyone -- it just stands there.  Nothing said so, and
  // the reader came back on their own a minute later to a stopped turn.  The
  // turn cannot go on without an answer, so being told is not a courtesy here.
  FLASHWINFO flash = {};
  flash.cbSize = sizeof(flash);
  flash.hwnd = top;
  // Until the window comes to the front -- and it can only come to the front
  // to be answered.  A fixed count would stop while the question still stands.
  flash.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
  FlashWindowEx(&flash);

  // The flash is the half a sighted user gets; this is the other half, and for
  // this application it is the important one.  Speech is not on offer -- see
  // invariant 11: on a background window it is cancelled by the reader's next
  // keystroke and never arrives.
  //
  // Not MessageBeep, and that is the point.  Both of its usable sounds already
  // mean something ("the turn ended and nothing was said", "the turn ended
  // behind the window"), and a sound that answers two questions answers
  // neither.  MB_ICONQUESTION would have been the obvious third, but the
  // Windows default scheme leaves that event unassigned, so it would have
  // failed the only way that matters here: silently.
  //
  // Without SND_NODEFAULT an alias that is not assigned falls back to the
  // system default sound rather than to nothing, which is the behaviour wanted
  // on a machine whose scheme has been cut down.
  PlaySoundW(L"Notification.Default", nullptr, SND_ALIAS | SND_ASYNC);
}

LRESULT SessionPane::OnPermission(LPARAM pointer) {
  PendingPermission* pending = reinterpret_cast<PendingPermission*>(pointer);

  // Before the box goes up, not after: the call below does not return until
  // there is an answer.
  SignalWaiting();

  pending->answer.verdict = AskPermission(host_, *pending->request);
  // No message: what the model is told on a denial is the adapter's to say
  // (proto::kDeniedInstruction for Claude).
  return 0;
}

LRESULT SessionPane::OnQuestion(LPARAM pointer) {
  PendingQuestion* pending = reinterpret_cast<PendingQuestion*>(pointer);

  // The model asking the reader something, which is not a permission however
  // it travelled -- telling the two apart is the adapter's business
  // (invariant 12).  Until that was done a question with three options was
  // offered with Yes and No and the answer was lost.
  SignalWaiting();

  pending->answer.declined =
      !AskQuestions(host_, pending->request->questions, &pending->answer.chosen);
  return 0;
}

bool SessionPane::Following() const {
  // Is the reader reading, or just listening?
  //
  // "Caret at the end" alone does not decide it.  Once a turn has happened the
  // caret sits where we left it, just before that answer -- never at the end
  // -- so a reader who only listens and never touches the transcript would
  // look like somebody who wandered off.  So the test is: at the end, OR
  // exactly where Send left it last time and therefore untouched.  Anything
  // else means they moved it themselves, and then the place is theirs.
  //
  // anchor_ is carried through every edit (see Apply), so this stays true for
  // the whole of a turn that the reader is only listening to.  It is also
  // where "at the end" is kept alive: standing at the end survives no append
  // on its own, so Apply adopts the caret as the anchor while it is still
  // there.  The end test below is what covers the moment before that -- a
  // Ctrl+End with no edit in between.
  if (transcript_ == nullptr) return true;
  const size_t caret = CaretOffset(transcript_);
  return !HasSelection(transcript_) &&
         (caret >= model_.Text().size() || caret == anchor_);
}

bool SessionPane::InForeground() const {
  // A session behind another one is as much out of sight as a window behind
  // another application.
  if (host_ == nullptr || !active_) return false;
  const HWND top = GetAncestor(host_, GA_ROOT);
  return top != nullptr && GetForegroundWindow() == top;
}

bool SessionPane::WantsProgressSpeech() const {
  // Three states, and only the third one is spoken to.
  //
  // 1. The window is not in front.  Whatever the reader is doing, it is not
  //    this, and our sentences would land in the middle of it.  Invariant 7
  //    forbids cutting into NVDA's queue; filling that queue with text nobody
  //    asked for is the same offence from the other side.
  // 2. The window is in front, the focus is in the transcript, and the caret
  //    is somewhere the reader put it.  They are reading an older passage,
  //    arrowing line by line, and NVDA is already speaking those lines -- our
  //    running commentary interleaves with them into nonsense.
  // 3. The focus is in the prompt, or the caret is following the end.  Then
  //    the reader is waiting for exactly this.  Unchanged.
  //
  // Nothing is owed for what was not said.  It stands in the transcript and
  // t/r/a/E and Ctrl+0 lead to it -- that is the thing a terminal does not
  // have, and the reason this is not the same case as the interrupt that
  // needed a second sentence because it left nothing behind.
  if (!InForeground()) return false;
  if (GetFocus() != transcript_) return true;
  return Following();
}

void SessionPane::AnnounceProgress(size_t firstNewId) {
  // What the turn is doing, while it does it, in the order it does it.
  //
  // The tool calls alone were not enough, and reading the answer whole at the
  // end was wrong.  A turn that says "first tool", calls Bash, says "second
  // tool" and calls Bash again was heard as two bare "Bash: echo ..." lines
  // with the sentences that explained them arriving afterwards, out of order
  // and detached from what they introduced.  That is precisely what the
  // terminal does better, and it does it by showing each piece where it
  // happened.  Reported from use.
  //
  // So all three speaking kinds go out here, as they arrive: the assistant's
  // text whole -- it is the answer, and between two tools it is one sentence
  // -- the tool call as its summary line ("Bash: git status"), and the tool
  // result as its summary too.  The result's summary is a whole one-line
  // output or else just its size ("výstup (12 riadkov)"), so a tool that
  // printed a thousand lines costs one short sentence, not a thousand.
  //
  // Thinking is still not read: it is dozens of records a turn and it is not
  // addressed to the reader.  "Premýšľam" stands in for the lot.
  //
  // Queued, never interrupting.  This arrived on its own -- invariant 7.
  for (const model::Block& block : model_.blocks()) {
    if (block.id < firstNewId) continue;
    const std::wstring* said = nullptr;
    switch (block.kind) {
      case model::BlockKind::AssistantText: said = &block.body; break;
      case model::BlockKind::ToolUse:
      case model::BlockKind::ToolResult: said = &block.summary; break;
      default: break;
    }
    if (said == nullptr || said->empty()) continue;
    thinkingSaid_ = false;
    spokeThisTurn_ = true;
    // With the speaker's name in front, exactly as the transcript writes it.
    // Read aloud, "Nástroj 1." and "Bash: echo ..." are two sentences in the
    // same voice with nothing to tell them apart; "claude: Nástroj 1." says
    // which of them the model actually said.  Empty for the mechanism kinds --
    // a tool summary already names itself.
    if (speech_.available()) {
      speech_.Say(model_.SpeakerPrefix(block.kind) + *said, false);
    }
  }
}

void SessionPane::SignalTurnEnd() {
  // The answer is not read here any more -- AnnounceProgress already said it,
  // in its place among the tools.  What is left is the one thing the reader
  // still cannot know: that nothing more is coming.
  //
  // It used to be knowable without being said, because the answer arrived only
  // at the end: hearing it meant the turn was over.  Once the text is spoken
  // as it arrives, that sign is gone, and a turn that ends on a sentence
  // sounds exactly like a turn that is about to say another one.
  //
  // Behind another window it is a sound and never a sentence.  Not because
  // the end of a turn does not matter there -- it is precisely what somebody
  // who switched away is waiting for -- but because speech does not survive
  // the trip.  Whoever is writing a mail meanwhile cancels our sentence with
  // the first key they press: NVDA drops its queue when typing starts, and
  // "hotovo" dies in the middle.  A sound is not in that queue and is not
  // cancelled by anything.
  //
  // A different one from the asterisk below, which already means something
  // narrower ("the turn ended and nothing was said").  Two events answering
  // with the same sound would make the sound mean neither.
  if (!InForeground()) {
    MessageBeep(kBackgroundEndSound);
    return;
  }
  // A word and not a beep, when anything was said.  MessageBeep plays at once
  // while the speech it belongs after is still in NVDA's queue, so the "done"
  // would land in the middle of the answer -- and a queued beep is not on
  // offer, NVDA speaks text.  It queues, like everything that arrived on its
  // own -- invariant 7.
  if (spokeThisTurn_ && speech_.available()) {
    speech_.Say(i18n::Text(Str::kSayDone), false);
    return;
  }
  // Nothing was said all turn -- it did no tool and produced no text -- or
  // there is no screen reader listening.  A sound is then the only way to know
  // the turn is over without going to look, and nothing is queued for it to
  // cut across.  MessageBeep rather than a tone of our own: it goes through
  // the system sounds, so it can be silenced where everything else is.
  // Asterisk, not the Default Beep -- see Announce.
  MessageBeep(MB_ICONASTERISK);
}

bool SessionPane::Navigate(wchar_t key) {
  // A capital letter means backwards.  Reading the case rather than asking for
  // the shift state is what makes this work on any layout: by the time a
  // character arrives, the system has already applied the keyboard.
  const bool backwards = key >= L'A' && key <= L'Z';
  const wchar_t lower =
      backwards ? static_cast<wchar_t>(key - L'A' + L'a') : key;

  model::Transcript::BlockPredicate match;
  std::wstring what;
  auto ofKind = [&match, &what](model::BlockKind kind) {
    match = [kind](const model::Block& block) { return block.kind == kind; };
    what = model::KindLabel(kind);
  };
  switch (lower) {
    case L't': ofKind(model::BlockKind::ToolUse); break;
    case L'r': ofKind(model::BlockKind::ToolResult); break;
    case L'p': ofKind(model::BlockKind::UserPrompt); break;
    case L'a': ofKind(model::BlockKind::AssistantText); break;
    case L'k': ofKind(model::BlockKind::Thinking); break;
    case L'e':
    // '!' stays as a silent alias forwards: it is one case in this switch, it
    // is already in the fingers, and dropping it would gain nothing.  It is
    // not documented any more, because it cannot go backwards -- there is no
    // capital '!' -- and on the Slovak layout it is not on Shift+1 at all.
    case L'!':
      // Not a kind: trouble is either a denied tool or a tool result the CLI
      // marked as an error, and the stream has no one type for the two.
      match = [](const model::Block& block) {
        return block.isError ||
               block.kind == model::BlockKind::PermissionDenied;
      };
      what = i18n::Text(Str::kError);
      break;
    default:
      return false;
  }

  const size_t caret = CaretOffset(transcript_);
  const std::optional<size_t> found = backwards
                                          ? model_.PreviousWhere(caret, match)
                                          : model_.NextWhere(caret, match);
  if (!found.has_value()) {
    Announce(i18n::Format(
        backwards ? Str::kNoPreviousMatch : Str::kNoNextMatch, {what}));
    return true;
  }
  GoToBlock(*found);
  return true;
}

void SessionPane::SetBookmark(size_t slot) {
  if (slot >= model::Bookmarks::kSlots) return;
  if (slot == 0) {
    Announce(i18n::Text(Str::kBookmarkZeroIsAuto));
    return;
  }
  const model::Mark mark = model::MarkAt(model_, CaretOffset(transcript_));
  if (!mark.set) {
    Announce(i18n::Text(Str::kTranscriptEmpty));
    return;
  }
  bookmarks_.Set(slot, mark);
  // The slot number and the line, in that order.  The number alone leaves the
  // reader wondering what they just marked; the line alone leaves them
  // wondering whether the key arrived.
  Announce(i18n::Format(Str::kBookmarkSet,
                        {std::to_wstring(slot),
                         model_.LineAt(CaretOffset(transcript_))}));
}

void SessionPane::GoToBookmark(size_t slot) {
  const model::Mark& mark = bookmarks_.Get(slot);
  const std::optional<size_t> offset = model::OffsetOf(model_, mark);
  if (!offset.has_value()) {
    // Slot 0 is empty until something arrives while you are reading, which is
    // the only situation it is for -- so it says something different.
    Announce(slot == 0 ? std::wstring(i18n::Text(Str::kNothingNewSince))
                       : i18n::Format(Str::kBookmarkEmpty,
                                      {std::to_wstring(slot)}));
    return;
  }
  // Expanded the way a search expands its hit, and through Apply for the same
  // reason (invariant 3).  The line said is the marked one, and the heading
  // above it says the block is open to anyone who reads up.
  if (const std::optional<size_t> index = model::BlockToExpand(model_, mark)) {
    Apply(model_.SetCollapsed(*index, false));
  }
  GoToOffset(*model::OffsetOf(model_, mark));
}

void SessionPane::GoToOffset(size_t offset) {
  PutCaret(transcript_, offset);
  Announce(model_.LineAt(offset));
}

void SessionPane::GoToBlock(size_t index) {
  const std::vector<model::Block>& blocks = model_.blocks();
  if (index >= blocks.size()) return;
  // anchor_ is deliberately left alone, here and in GoToOffset.  It records
  // where Send() put the caret, and its whole purpose is to tell "nobody has
  // touched this" from "the reader is reading".  A jump is the reader reading,
  // so from here on the caret is theirs and the next prompt must not drag it
  // to the end.
  GoToOffset(blocks[index].start);
}

void SessionPane::Announce(const std::wstring& text) {
  // Interrupting is right here: the reader pressed a key and wants the answer
  // to that press, not the tail of the previous one.
  if (speech_.available()) {
    speech_.Say(text, true);
    return;
  }
  // MB_ICONASTERISK and not MB_OK.  MB_OK is the Default Beep, which is the
  // sound Windows makes at a key a dialog will not take -- so using it here
  // says the opposite of what happened: the key worked, there was just no
  // voice to say what it did.  Reported from use, and it was the reason a
  // missing DLL read as "this build does not know these keys".
  MessageBeep(MB_ICONASTERISK);
}

void SessionPane::ToggleBlockAtCaret() {
  const std::optional<size_t> index = model_.BlockAt(CaretOffset(transcript_));
  if (!index.has_value()) return;

  if (!model_.blocks()[*index].collapsible) {
    // Nothing behind the summary.  Say the line rather than saying nothing,
    // so a press is never answered with silence.
    Announce(model_.FirstLine(*index));
    return;
  }

  Apply(model_.SetCollapsed(*index, !model_.blocks()[*index].collapsed));

  // To the start of the block, always.  Collapsing can leave the caret past
  // the block's new end, and even when it does not, the line the reader wants
  // after pressing this is the one they acted on.  GoToBlock says which line
  // that now is -- NVDA does not announce a caret it did not move itself,
  // confirmed by trying it, so without that the key answers with silence.
  GoToBlock(*index);
}

void SessionPane::SetStatus(std::wstring text) {
  status_ = std::move(text);
  // Into the bar and not into the title any more.  The title is announced when
  // the window takes focus and never again, so a turn that ends while you are
  // reading would not have been heard there anyway -- and the bar can be asked
  // at any time with NVDA+End.
  SetField(StatusBar::kTurn, status_);
}

void SessionPane::SetField(StatusBar::Field field, const std::wstring& text) {
  fields_[field] = text;
  if (statusBar_) statusBar_->Set(field, text);
}

void SessionPane::SetStatusBar(StatusBar* bar) {
  statusBar_ = bar;
  if (!statusBar_) return;
  // Every field, the empty ones too: an empty field here is a fact about this
  // session, and the previous session's text in it would be a false one.
  for (int field = 0; field < StatusBar::kFieldCount; ++field) {
    statusBar_->Set(static_cast<StatusBar::Field>(field), fields_[field]);
  }
}

void SessionPane::ShowModel(const std::string& model) {
  // Into details_ whether or not there is a bar: the details dialog needs it
  // too, and the bar is optional here.
  details_.model = model::Utf16FromUtf8(model);
  RefreshModelField();
}

std::wstring SessionPane::ModeLabel(const std::wstring& id) const {
  const agent::Mode* mode =
      agent::FindMode(backend_->capabilities(), model::Utf8FromUtf16(id));
  return mode != nullptr ? model::Utf16FromUtf8(mode->label) : id;
}

// What is said when the mode changes, whoever changed it: the name first, so
// "did the key work" is answered at once, then what the mode does.  One
// sentence for the key and for a change the CLI made, so the two never sound
// like different things.
std::wstring SessionPane::ModeSentence(const std::wstring& id) const {
  std::wstring said = i18n::Format(Str::kModeNamed, {ModeLabel(id)});
  const agent::Mode* mode =
      agent::FindMode(backend_->capabilities(), model::Utf8FromUtf16(id));
  if (mode != nullptr && !mode->gloss.empty()) {
    said += L" — " + model::Utf16FromUtf8(mode->gloss);
  }
  return said;
}

void SessionPane::FollowPermissionMode(const std::string& reported) {
  const std::wstring live = model::Utf16FromUtf8(reported);
  if (live.empty() || live == details_.permissionMode) return;
  // A mode appearing where none was known is not a change: that is the
  // handshake answering a session started without --permission-mode.
  const bool changed = !details_.permissionMode.empty();
  details_.permissionMode = live;
  RefreshModelField();
  if (!changed) return;
  // Said, because otherwise the last word on the mode is a wrong one: after a
  // refused Shift+Tab the key already announced the mode that did not happen,
  // and after ExitPlanMode or auto being dropped the reader was last told a
  // mode that is gone.  It came off the stream, so it queues -- invariant 7.
  // In the foreground only, like everything that is not a key's answer
  // (invariant 11); behind the window the bar keeps it.
  //
  // Not gated on the caret following, as the turn's commentary is: this is
  // not commentary but a correction of a fact the reader holds, it is one
  // sentence, and it is rare.
  if (InForeground() && speech_.available()) {
    speech_.Say(ModeSentence(live), false);
  }
}

void SessionPane::RefreshModelField() {
  // Model and permission mode in one field.  The design said "model and
  // effort", but system/init carries no effort -- and the permission mode is
  // the more useful of the two anyway: it decides whether anything will be put
  // to you at all.
  // Every field says what it is.  Read out one after another they are four
  // bare values otherwise, and "claude-opus-5, pokus, 5 h 83 %" is a riddle.
  // A part that is not known yet is left out rather than said empty.
  std::wstring facts;
  if (!details_.model.empty()) facts = i18n::Format(Str::kStatusModel, {details_.model});
  const std::wstring& mode = details_.permissionMode;
  // The ordinary mode is left off: it is the state a reader assumes, and
  // naming it every time would crowd the field that has to be read in one
  // breath.
  const agent::Mode* known =
      agent::FindMode(backend_->capabilities(), model::Utf8FromUtf16(mode));
  if (!mode.empty() && !(known != nullptr && known->ordinary)) {
    if (!facts.empty()) facts += L", ";
    facts += i18n::Format(Str::kModeNamed, {ModeLabel(mode)});
  }
  SetField(StatusBar::kModel, facts);
}

void SessionPane::ShowRateLimit(const agent::RateLimitChanged& limit) {
  const auto percent = [](double share) {
    return std::to_wstring(static_cast<int>(share * 100 + 0.5)) + L" %";
  };
  // A window by its length, the short way: the field is read in one breath
  // with three others.
  const auto length = [](int minutes) {
    if (minutes > 0 && minutes % (24 * 60) == 0) {
      return i18n::Format(Str::kDurationDays,
                          {std::to_wstring(minutes / (24 * 60))});
    }
    if (minutes > 0 && minutes % 60 == 0) {
      return i18n::Format(Str::kDurationHours, {std::to_wstring(minutes / 60)});
    }
    return i18n::Format(Str::kDurationMinutes, {std::to_wstring(minutes)});
  };
  std::wstring text;
  long long resetsAt = limit.resetsAt;
  for (const agent::LimitWindow& window : limit.windows) {
    if (window.used < 0) continue;
    if (!text.empty()) text += L", ";
    text += length(window.minutes) + L" " + percent(window.used);
    if (resetsAt == 0) resetsAt = window.resetsAt;
  }
  if (text.empty() && limit.used >= 0) text = percent(limit.used);
  if (resetsAt > 0) {
    const std::time_t when = static_cast<std::time_t>(resetsAt);
    std::tm local = {};
    if (localtime_s(&local, &when) == 0) {
      wchar_t stamp[32] = {};
      // Day and time, not just time: a seven-day window resets on some other
      // day, and "resets at 6:00" would be read as this morning.
      std::wcsftime(stamp, 32, i18n::Text(Str::kLimitStampFormat), &local);
      if (!text.empty()) text += L", ";
      text += i18n::Format(Str::kLimitResets, {stamp});
    }
  }
  // The state, when there is one, is what names the field; otherwise the word
  // "limit" does.  Both used to be put in front, so an exhausted limit read
  // "limit limit vyčerpaný".
  const wchar_t* state = limit.state == agent::LimitState::Exhausted
                             ? i18n::Text(Str::kLimitExhausted)
                         : limit.state == agent::LimitState::Warning
                             ? i18n::Text(Str::kLimitWarning)
                             : nullptr;
  if (state != nullptr) {
    text = text.empty() ? std::wstring(state) : state + (L", " + text);
  } else if (!text.empty()) {
    text = i18n::Format(Str::kStatusLimit, {text});
  }
  SetField(StatusBar::kLimit, text);
}

void SessionPane::FocusPrompt() const { SetFocus(prompt_); }

void SessionPane::RestoreFocus() const {
  SetFocus(lastFocus_ ? lastFocus_ : prompt_);
}

void SessionPane::Send() {
  // Both refusals say so.  Ctrl+Enter is a chord and a chord can be missed --
  // Enter alone puts a line break in the box and does nothing else -- so a
  // send that answers with silence cannot be told from a key that half
  // arrived.  The text is left in the box in both cases; that is why the
  // wording says what is in the way rather than that something was lost.
  if (ended_) {
    Announce(i18n::Text(Str::kAgentNotRunning));
    return;
  }
  if (busy_) {
    Announce(i18n::Text(Str::kTurnStillRunning));
    return;
  }
  const std::wstring text = GetText(prompt_);
  if (IsBlank(text)) {
    Announce(i18n::Text(Str::kPromptEmpty));
    return;
  }
  SendText(text);
  SetWindowTextW(prompt_, L"");
}

namespace {

// One question is answered with the answer alone, as a person would type it;
// several with each question in front, so the model can tell which answer is
// which.
std::string AnswerText(const std::vector<agent::Question>& questions,
                       const std::vector<std::vector<std::string>>& chosen) {
  std::string answer;
  for (size_t i = 0; i < questions.size() && i < chosen.size(); ++i) {
    std::string picked;
    for (const std::string& label : chosen[i]) {
      if (!picked.empty()) picked += ", ";
      picked += label;
    }
    if (questions.size() == 1) {
      answer = picked;
    } else {
      if (!answer.empty()) answer += "\n";
      answer += questions[i].text + " " + picked;
    }
  }
  return answer;
}

}  // namespace

void SessionPane::OnQuestionByPrompt() {
  std::vector<agent::QuestionByPrompt> asked;
  asked.swap(questionsByPrompt_);
  // A prompt typed in the meantime came first; the question is then
  // answered, or not, by that.
  if (asked.empty() || busy_) return;

  // One dialog for all of them, in the order they were asked.
  std::vector<agent::Question> questions;
  for (const agent::QuestionByPrompt& item : asked) {
    questions.insert(questions.end(), item.questions.begin(),
                     item.questions.end());
  }

  SignalWaiting();
  std::vector<std::vector<std::string>> chosen;
  const bool answered = AskQuestions(host_, questions, &chosen);

  // The outcome behind each call, the same as the blocking question gets
  // from its adapter -- what was chosen, or "bez odpovede".  Without it a
  // dismissed question read exactly like one still waiting
  // (claude-gui-lkk.44.11).
  std::vector<agent::Event> results;
  size_t next = 0;
  for (const agent::QuestionByPrompt& item : asked) {
    agent::ToolResult result;
    result.callId = item.callId;
    if (answered) {
      const auto first = chosen.begin() + static_cast<ptrdiff_t>(
                                              std::min(next, chosen.size()));
      const auto last =
          chosen.begin() + static_cast<ptrdiff_t>(std::min(
                               next + item.questions.size(), chosen.size()));
      result.text = AnswerText(item.questions, {first, last});
    } else {
      result.text = i18n::Utf8(Str::kNoAnswer);
      result.isError = true;
    }
    next += item.questions.size();
    results.push_back(agent::ToolCallFinished{std::move(result)});
  }
  for (const model::Edit& edit : model_.Append(results)) Apply(edit);

  // Dismissed: nothing is sent.  The prompt box is where an answer of another
  // kind is typed.
  if (!answered) return;
  const std::string answer = AnswerText(questions, chosen);
  if (!answer.empty()) SendText(model::Utf16FromUtf8(answer));
}

void SessionPane::SendText(const std::wstring& text) {
  // Sending moves the caret past the new prompt, so that the answer arrives
  // directly under it -- otherwise you walk through your own prompt to reach
  // the reply.  But it must not do that to somebody who is in the middle of
  // reading something further back; they would lose their place.
  const bool following = Following();

  turnFirstId_ = model_.nextBlockId();
  Apply(model_.AppendUserPrompt(text));
  if (following) {
    PutCaretAtEnd(transcript_);
    anchor_ = model_.Text().size();
  }

  backend_->SendPrompt(model::Utf8FromUtf16(text));
  interrupted_ = false;
  thinkingSaid_ = false;
  spokeThisTurn_ = false;
  busy_ = true;
  SetStatus(i18n::Text(Str::kWorking));
  // Said as well as written.  The bar is silent until NVDA+End is pressed, and
  // "did that go?" is a question one has right after pressing the key, not one
  // worth a second key.  This answers the key, so it interrupts -- invariant 7
  // allows exactly that, and only that.
  Announce(i18n::Text(Str::kWorking));
}

void SessionPane::Interrupt() {
  if (!busy_) {
    Announce(i18n::Text(Str::kNothingRunning));
    return;
  }
  if (!backend_->Interrupt()) {
    // The session and the pane disagree about whether a turn is running.  Say
    // so rather than pretending: the reader is about to wait for something to
    // stop that nobody is stopping.
    Announce(i18n::Text(Str::kInterruptFailed));
    return;
  }
  // Not busy_ = false.  The turn ends when the CLI says it does, with a
  // Result like any other turn; until then something is still coming and the
  // status line must not claim otherwise.
  interrupted_ = true;
  SetStatus(i18n::Text(Str::kInterrupting));
  Apply(model_.AppendInterrupted());
  Announce(i18n::Text(Str::kInterrupting));
}

void SessionPane::CyclePermissionMode() {
  const std::string current = backend_->mode();
  if (current.empty()) {
    // Started without --permission-mode, and the CLI has not answered the
    // handshake yet: the mode comes from settings and nobody here knows it.
    // Cycling from a guess would send a mode the reader did not step to --
    // which is exactly how plan used to become acceptEdits on the first press.
    Announce(i18n::Text(Str::kModeNotKnownYet));
    return;
  }
  const std::string next = agent::NextMode(backend_->capabilities(), current);
  if (next.empty()) {
    // An agent whose modes cannot be switched while it runs.  Said, like
    // every key that has nothing to do -- invariant 6.
    Announce(i18n::Text(Str::kModeFixed));
    return;
  }
  if (!backend_->SetMode(next)) {
    // The pipe is gone.  Say so rather than leaving the key silent.
    Announce(i18n::Text(Str::kModeSwitchFailed));
    return;
  }
  // Said now, on the key, and the bar rewritten now too.  The CLI's
  // confirmation comes back on the reader thread, and invariant 7 keeps
  // interrupting speech for key responses only -- a confirmation folded in
  // later could not cut in to be heard.  Nearly every switch goes through, so
  // the wait is not worth a silent key; the one that may not (auto, on an
  // account without it) is put right by FollowPermissionMode when the refusal
  // arrives, and said then.  Session::permissionMode is already moved;
  // details_ and the bar follow it here, which is also what keeps
  // FollowPermissionMode from saying this same mode a second time.
  details_.permissionMode = model::Utf16FromUtf8(next);
  RefreshModelField();
  Announce(ModeSentence(details_.permissionMode));
}

void SessionPane::CopySessionId() {
  RefreshFacts();
  // Before the first system/init there is no id at all -- the CLI makes it,
  // we only read it back -- and a key that quietly put an empty string on the
  // clipboard would be found out in the terminal, pasting nothing.
  if (details_.id.empty()) {
    Announce(i18n::Text(Str::kSessionIdUnknown));
    return;
  }
  if (!win::SetClipboardText(host_, details_.id)) {
    Announce(i18n::Text(Str::kClipboardFailed));
    return;
  }
  // Short on purpose.  The id itself is 36 characters of hex and reading it
  // out is no use to anybody: the only thing done with it is pasting it.
  Announce(i18n::Text(Str::kIdCopied));
}

void SessionPane::RefreshFacts() {
  // Asked for at the moment they are wanted, not kept up to date from the
  // records: the backend may learn the id off whichever record carries it,
  // whichever kind that turns out to be, and copying it in system/init would
  // mean the id existed but stayed invisible until an init happened to come.
  details_.id = model::Utf16FromUtf8(backend_->conversationId());
  // The mode is not taken here.  It used to be, from the handshake, and the
  // handshake is a snapshot from the start: after a Shift+Tab F2 showed the
  // mode the session had already left.  details_.permissionMode is kept up to
  // date by FollowPermissionMode after every batch, and taking it here as well
  // would absorb a change without it being said or put into the bar.
  const agent::Account known = backend_->account();
  // The address and what it is paying with, in one field: separately they
  // would be two rows to tab through for one fact.  Nothing is invented when a
  // piece is missing -- the field is only as complete as the answer was.
  std::wstring account = model::Utf16FromUtf8(known.email);
  if (!known.plan.empty()) {
    if (!account.empty()) account += L", ";
    account += model::Utf16FromUtf8(known.plan);
  }
  // Empty in the ordinary case; the backend says it only when the cost
  // numbers mean something unusual, and a word on the usual account would be
  // noise in a field whose whole job is to be read in one breath.
  if (!known.billingNote.empty()) {
    if (!account.empty()) account += L", ";
    account += model::Utf16FromUtf8(known.billingNote);
  }
  if (!account.empty()) details_.account = account;
}

void SessionPane::ShowKeys() {
  // Nothing announced here, for the same reason as ShowDetails: the dialog
  // announces itself, and a sentence of ours would arrive on top of it.
  ui::ShowKeys(host_, backend_->capabilities());
}

void SessionPane::ShowDetails() {
  RefreshFacts();
  // Nothing is announced here, and that is not an oversight.  A dialog is the
  // one thing NVDA reads of its own accord -- the title, then the focused
  // control -- so the key does answer.  A sentence of ours would arrive on top
  // of that announcement and cut it off, which is invariant 7 read from the
  // other side: the reply we would be interrupting is the reader's own.
  SessionDetailsDialog dialog(details_, [this] { CopySessionId(); });
  dialog.ShowModal(host_, IDD_SESSION_DETAILS);
}

void SessionPane::ShowCommands() {
  if (!backend_->capabilities().slashCommands) {
    Announce(i18n::Text(Str::kNoCommandList));
    return;
  }
  const std::vector<agent::SlashCommand> commands = backend_->commands();
  // The list comes with the answer to the initialize handshake, and that
  // answer is not immediate -- in this project, whose SessionStart hook runs
  // bd prime, it took over twenty seconds.  So "no list yet" is a normal state
  // of a session that has only just started, and the one thing it must not do
  // is open an empty dialog: an empty list reads as "this session has no
  // commands", which is a different and false statement.
  if (commands.empty()) {
    Announce(i18n::Text(Str::kCommandsNotYet));
    return;
  }

  agent::SlashCommand chosen;
  if (!PickCommand(host_, commands, &chosen)) return;

  // Into the prompt, at the caret, as text -- not sent, and not run.  A
  // headless session does not expand slash commands itself: measured
  // 2026-09-06 (tools/probe_slash.py), the text reaches the model and the
  // model launches what is behind it with the Skill tool.  So this key saves
  // the typing and the remembering of the name, and the sending stays where
  // every other prompt's sending is, on Ctrl+Enter.
  std::wstring text = L"/" + model::Utf16FromUtf8(chosen.name);
  // The trailing space only when there is something to type after it.  With
  // it, a command that takes no arguments would go out with a space on the
  // end; without it, one that does needs the space typed first.
  if (!chosen.argumentHint.empty()) text += L" ";
  SetFocus(prompt_);
  SendMessageW(prompt_, EM_REPLACESEL, TRUE,
               reinterpret_cast<LPARAM>(text.c_str()));

  // And nothing is said.  It used to say "vložené /x, argumenty: ..." here,
  // on the grounds that a caret moving in the prompt box is exactly the move
  // NVDA does not announce -- but reported from use, nobody ever heard it:
  // closing a dialog is a focus change, NVDA announces those and cancels
  // speech while doing it, and its own event loop gets there tens of
  // milliseconds after this line has already spoken.  Queueing instead of
  // interrupting does not save it either, because cancelSpeech empties the
  // whole queue.
  //
  // A timer would land the sentence behind that announcement, and behind is
  // where it dies: what NVDA says when a dialog closes is the window title
  // and the project path, which is long enough that the reader silences it
  // with Ctrl -- taking anything queued after it along.
  //
  // Invariant 6 is satisfied anyway, by NVDA rather than by us: the focus
  // lands in the prompt and the line it reads out is the one with the
  // command just inserted in it.  The argument hint is the part that is lost,
  // and it was on screen in the dialog a second ago, in the list line and in
  // the description box both.
}

std::optional<model::SearchHit> SessionPane::FindText(const std::wstring& text,
                                                      bool backwards) {
  const std::optional<model::SearchHit> hit =
      model_.Find(text, CaretOffset(transcript_), backwards, FoldCase);
  if (!hit.has_value()) return std::nullopt;
  const model::Block& block = model_.blocks()[hit->index];
  if (block.collapsed && block.collapsible) {
    Apply(model_.SetCollapsed(hit->index, false));
  }
  // anchor_ is left alone, as in GoToBlock: a search is the reader reading.
  PutCaret(transcript_, model_.blocks()[hit->index].start + hit->offset);
  return hit;
}

bool SessionPane::SearchAndSay(const std::wstring& text, bool backwards) {
  const std::optional<model::SearchHit> hit = FindText(text, backwards);
  if (!hit.has_value()) {
    Announce(i18n::Format(Str::kSearchNotFound, {text}));
    return false;
  }
  const std::wstring line = model_.LineAt(CaretOffset(transcript_));
  Announce(hit->wrapped
               ? i18n::Format(backwards ? Str::kSearchWrappedBottom
                                        : Str::kSearchWrappedTop,
                              {line})
               : line);
  return true;
}

void SessionPane::ShowFind() {
  bool found = false;
  FindDialog(&searchText_,
             [this, &found](const std::wstring& text) {
               if (SearchAndSay(text, false)) found = true;
             })
      .ShowModal(host_, IDD_FIND);
  // Into the transcript only when the caret there was moved, so that what
  // was found is where the reader lands.  Otherwise the focus goes back to
  // wherever it came from, and a half-typed prompt is not left behind.
  if (found) SetFocus(transcript_);
}

void SessionPane::FindNext(bool backwards) {
  if (searchText_.empty()) {
    ShowFind();
    return;
  }
  SearchAndSay(searchText_, backwards);
}

LRESULT CALLBACK SessionPane::PromptProc(HWND window, UINT message,
                                         WPARAM wParam, LPARAM lParam,
                                         UINT_PTR id, DWORD_PTR data) {
  SessionPane* pane = reinterpret_cast<SessionPane*>(data);
  if (message == WM_SETFOCUS) pane->lastFocus_ = window;
  if (message == WM_KEYDOWN) {
    const bool control = GetKeyState(VK_CONTROL) < 0;
    if (wParam == VK_RETURN && control) {
      pane->Send();
      return 0;
    }
    // Esc costs nothing here: a plain edit control does nothing with it, and
    // this window is not a dialog, so nothing is waiting to be closed by it.
    if (wParam == VK_ESCAPE) {
      pane->Interrupt();
      return 0;
    }
    if (IsJumpChord(wParam)) {
      pane->Navigate(static_cast<wchar_t>(wParam - 'A' + L'a'));
      return 0;
    }
    // All three are the same key in both boxes, like Esc and the chords:
    // which box has the focus is not something to have to remember first.  A
    // function key needs no second discard either -- it produces no WM_CHAR at
    // all, which is one whole class of trap it cannot fall into.
    if (wParam == VK_F1) {
      pane->ShowKeys();
      return 0;
    }
    if (wParam == VK_F2) {
      pane->ShowDetails();
      return 0;
    }
    if (wParam == VK_F4) {
      pane->ShowCommands();
      return 0;
    }
    if (IsFindChord(wParam)) {
      pane->ShowFind();
      return 0;
    }
    if (wParam == VK_F3) {
      pane->FindNext(GetKeyState(VK_SHIFT) < 0);
      return 0;
    }
    if (IsCopyIdChord(wParam)) {
      pane->CopySessionId();
      return 0;
    }
    const size_t slot = BookmarkSlot(wParam);
    if (slot != kNoSlot) {
      if (GetKeyState(VK_SHIFT) < 0) {
        pane->SetBookmark(slot);
      } else {
        pane->GoToBookmark(slot);
      }
      return 0;
    }
    if (wParam == VK_TAB) {
      // Shift+Tab is the terminal's key for the permission mode, and it is the
      // same in both boxes like Esc and the chords.  Plain Tab is the toggle
      // between the two controls -- done here rather than through
      // IsDialogMessage because win::RunMessageLoop is shared with another
      // project and this must not change how it behaves there.
      if (GetKeyState(VK_SHIFT) < 0) {
        pane->CyclePermissionMode();
      } else {
        SetFocus(pane->transcript_);
      }
      return 0;
    }
  }
  // Neither of these may reach the box as a character.  TranslateMessage runs
  // in the message loop, BEFORE the key ever gets here, so a key this
  // procedure "handled" still produces its WM_CHAR: Tab would type a tab, and
  // Ctrl+Enter produces 0x0A, which a multiline EDIT dutifully inserts.  On a
  // send that goes through, the box is cleared and nobody sees it; on one that
  // is refused -- a turn already running, an empty prompt -- the line break
  // stays and is carried into the next prompt.
  // Ctrl+F arrives as 0x06 the same way.  A cancelled search dialog leaves
  // the box as it was, and that character would not.
  if (message == WM_CHAR &&
      (wParam == VK_TAB || wParam == 0x0A || wParam == 0x06)) {
    return 0;
  }
  if (message == WM_DESTROY) RemoveWindowSubclass(window, PromptProc, id);
  return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK SessionPane::TranscriptProc(HWND window, UINT message,
                                             WPARAM wParam, LPARAM lParam,
                                             UINT_PTR id, DWORD_PTR data) {
  SessionPane* pane = reinterpret_cast<SessionPane*>(data);
  if (message == WM_SETFOCUS) pane->lastFocus_ = window;
  if (message == WM_KEYDOWN && wParam == VK_TAB) {
    // Shift+Tab cycles the permission mode from here too; plain Tab is the
    // toggle back to the prompt.
    if (GetKeyState(VK_SHIFT) < 0) {
      pane->CyclePermissionMode();
    } else {
      SetFocus(pane->prompt_);
    }
    return 0;
  }
  if (message == WM_KEYDOWN && wParam == VK_RETURN) {
    pane->ToggleBlockAtCaret();
    return 0;
  }
  // The same key in both boxes, for the same reason as the chords below: the
  // reader should not have to know where the focus is to stop a turn.
  if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
    pane->Interrupt();
    return 0;
  }
  // The chord works here too.  Which of the two boxes the focus is in is not
  // something a reader should have to remember before pressing a key.
  if (message == WM_KEYDOWN && IsJumpChord(wParam)) {
    pane->Navigate(static_cast<wchar_t>(wParam - 'A' + L'a'));
    return 0;
  }
  if (message == WM_KEYDOWN && wParam == VK_F1) {
    pane->ShowKeys();
    return 0;
  }
  if (message == WM_KEYDOWN && wParam == VK_F2) {
    pane->ShowDetails();
    return 0;
  }
  // The same key in both boxes, like everything else here.  It inserts into
  // the prompt from either side, which is also why it may be pressed while
  // reading the transcript.
  if (message == WM_KEYDOWN && wParam == VK_F4) {
    pane->ShowCommands();
    return 0;
  }
  if (message == WM_KEYDOWN && IsCopyIdChord(wParam)) {
    pane->CopySessionId();
    return 0;
  }
  if (message == WM_KEYDOWN && IsFindChord(wParam)) {
    pane->ShowFind();
    return 0;
  }
  if (message == WM_KEYDOWN && wParam == VK_F3) {
    pane->FindNext(GetKeyState(VK_SHIFT) < 0);
    return 0;
  }
  // The 0x06 Ctrl+F leaves behind, which a read-only box would answer with
  // a beep after the dialog has closed.
  if (message == WM_CHAR && wParam == 0x06) return 0;
  // Bookmarks reach the transcript the same way, and for the same reason: the
  // reader should not have to know which box has the focus.
  if (message == WM_KEYDOWN) {
    const size_t slot = BookmarkSlot(wParam);
    if (slot != kNoSlot) {
      if (GetKeyState(VK_SHIFT) < 0) {
        pane->SetBookmark(slot);
      } else {
        pane->GoToBookmark(slot);
      }
      return 0;
    }
  }
  // The bare letters, as characters rather than as keys: which key produces
  // "!" is a question about the layout, and on the Slovak one it is not
  // Shift+1.  Swallowed whether they navigated or not -- this box is
  // read-only, so a character that falls through is answered with a beep and
  // nothing else, which reads as "that key is broken".
  if (message == WM_CHAR && wParam >= L' ') {
    pane->Navigate(static_cast<wchar_t>(wParam));
    return 0;
  }
  if (message == WM_DESTROY) RemoveWindowSubclass(window, TranscriptProc, id);
  return DefSubclassProc(window, message, wParam, lParam);
}

}  // namespace ui
