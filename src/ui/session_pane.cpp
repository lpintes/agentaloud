#include "ui/session_pane.h"

#include <commctrl.h>
#include <richedit.h>

#include <ctime>

#include "model/utf.h"
#include "ui/resource.h"
#include "win/clipboard.h"

namespace ui {
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

}  // namespace

bool SessionPane::Create(HWND host, HINSTANCE instance) {
  host_ = host;

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
  if (TextLength(transcript_) != static_cast<int>(model_.Text().size())) {
    SetWindowTextW(host_, L"ClaudeLens — NESÚLAD MAPY ROZSAHOV");
  }
}

bool SessionPane::Start(const proto::Session::Options& options) {
  // The folder name in the bar, the whole path in the title.  The bar is read
  // out in one breath along with three other fields, and a path of eight
  // components there buries everything after it; the title is announced when
  // the window takes focus and nowhere else, which is exactly the right place
  // for the answer to "which checkout is this".
  std::wstring path = options.workingDir;
  while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) {
    path.pop_back();
  }
  const size_t slash = path.find_last_of(L"\\/");
  project_ = slash == std::wstring::npos ? path : path.substr(slash + 1);
  // The dialog gets the whole path, for the same reason the title does: it is
  // read on request and not in one breath with three other fields.
  details_.project = path;
  details_.permissionMode = options.permissionMode;
  if (statusBar_) statusBar_->Set(StatusBar::kProject, L"projekt " + project_);
  SetWindowTextW(host_, (L"ClaudeLens — " + path).c_str());
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
  // Where the reader is standing at the moment this batch arrives.  Taken
  // before anything is applied, and kept only if the batch really did add
  // something -- a drain that produced no block is not "new arrived", and
  // overwriting slot 0 with it would cost the reader the place they wanted.
  const model::Mark reading = model::MarkAt(model_, CaretOffset(transcript_));
  const size_t blocksBefore = model_.blocks().size();

  for (const proto::Event& event : events) {
    // Taken before the blocks are made, so that what the batch added can be
    // told from what was there.  An id and not a count: a tool result is
    // inserted behind its call, so the new blocks are not the tail.
    const size_t idBefore = model_.nextBlockId();
    for (const model::Edit& edit : model_.Append(event)) Apply(edit);
    // User as well as Assistant: a tool result comes back on a user record,
    // and it is half of what the turn is doing.  What each kind of block is
    // worth saying is AnnounceProgress's business -- our own prompt is a User
    // record too, and it filters that out by kind.
    if (event.kind == proto::EventKind::Assistant ||
        event.kind == proto::EventKind::User) {
      if (WantsProgressSpeech()) AnnounceProgress(idBefore);
    }
    if (event.kind == proto::EventKind::Assistant) {
      // How full the window is right now.  The newest message wins, and there
      // are several per turn -- each one was sent everything before it, so the
      // last is the only one that is still true.
      long long context = 0;
      if (proto::ParseContextTokens(event.raw, &context)) {
        details_.contextTokens = context;
      }
    }
    if (event.kind == proto::EventKind::SystemThinkingTokens && !thinkingSaid_ &&
        WantsProgressSpeech()) {
      // Once per stretch of thinking, not once per record -- there are dozens
      // of these per turn.  Cleared by anything else that speaks, so a turn
      // that thinks, calls a tool and thinks again says it twice, which is
      // what is happening.
      thinkingSaid_ = true;
      if (speech_.available()) speech_.Say(L"premýšľam", false);
    }
    if (event.kind == proto::EventKind::Result) {
      busy_ = false;
      // Overwritten, not added to: the numbers in modelUsage are the session's
      // running total, so each result is the whole answer -- see proto::Usage.
      // The model is passed in so that the context window reported is the one
      // this session runs, and not a subagent's.
      if (proto::ParseUsage(event.raw, model::Utf8FromUtf16(details_.model),
                            &details_.usage)) {
        details_.haveUsage = true;
      }
      // Empty, not "done".  Done says nothing a reader can use -- what was
      // done, and when?  The field is there to answer "is it working right
      // now", and the answer to that, once the turn is over, is nothing.
      SetStatus(L"");
      // A turn the reader stopped by hand ends differently from one that
      // finished, and the difference has to be audible: "prerušujem" answers
      // the key, this answers the turn, and without it a turn that stopped
      // sounds like one that never got the request.  It arrived on its own,
      // so it queues rather than cutting in -- invariant 7.
      //
      // Whatever the turn had already said stays said.  It arrived before Esc
      // did, and unsaying it is not on offer anyway -- see invariant 7 on why
      // nothing that came on its own may cut into the queue.
      //
      // Behind the window it is a sound, like any other end of a turn -- see
      // SignalTurnEnd for why speech does not carry across to a background
      // window at all.
      if (interrupted_) {
        interrupted_ = false;
        if (InForeground() && speech_.available()) {
          speech_.Say(L"prerušené", false);
        } else {
          MessageBeep(InForeground() ? MB_ICONASTERISK : kBackgroundEndSound);
        }
      } else {
        SignalTurnEnd();
      }
    } else if (event.kind == proto::EventKind::SystemInit) {
      // The turn field is NOT touched here.  system/init arrives at the start
      // of every turn, not once per session -- there are four of them in
      // tests/fixtures/basic.jsonl, one before each result -- so clearing the
      // field here wiped out the "pracujem" that Send had just written, and
      // the bar stayed blank for the whole turn.  Only Send and Result know
      // whether anything is running.
      ShowSessionFacts(event);
    } else if (event.kind == proto::EventKind::RateLimit) {
      ShowRateLimit(event);
    }
  }

  if (model_.blocks().size() != blocksBefore) bookmarks_.Set(0, reading);
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
  // the whole of a turn that the reader is only listening to.
  if (transcript_ == nullptr) return true;
  const size_t caret = CaretOffset(transcript_);
  return !HasSelection(transcript_) &&
         (caret >= model_.Text().size() || caret == anchor_);
}

bool SessionPane::InForeground() const {
  if (host_ == nullptr) return false;
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
      speech_.Say(model::SpeakerPrefix(block.kind) + *said, false);
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
    speech_.Say(L"hotovo", false);
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
      what = L"chyba";
      break;
    default:
      return false;
  }

  const size_t caret = CaretOffset(transcript_);
  const std::optional<size_t> found = backwards
                                          ? model_.PreviousWhere(caret, match)
                                          : model_.NextWhere(caret, match);
  if (!found.has_value()) {
    Announce((backwards ? L"žiadny predchádzajúci výskyt: "
                        : L"žiadny ďalší výskyt: ") +
             what);
    return true;
  }
  GoToBlock(*found);
  return true;
}

void SessionPane::SetBookmark(size_t slot) {
  if (slot >= model::Bookmarks::kSlots) return;
  if (slot == 0) {
    Announce(L"nultá záložka sa nenastavuje, píše ju aplikácia");
    return;
  }
  const model::Mark mark = model::MarkAt(model_, CaretOffset(transcript_));
  if (!mark.set) {
    Announce(L"prepis je prázdny");
    return;
  }
  bookmarks_.Set(slot, mark);
  // The slot number and the line, in that order.  The number alone leaves the
  // reader wondering what they just marked; the line alone leaves them
  // wondering whether the key arrived.
  Announce(L"záložka " + std::to_wstring(slot) + L": " +
           model_.LineAt(CaretOffset(transcript_)));
}

void SessionPane::GoToBookmark(size_t slot) {
  const model::Mark& mark = bookmarks_.Get(slot);
  const std::optional<size_t> offset = model::OffsetOf(model_, mark);
  if (!offset.has_value()) {
    // Slot 0 is empty until something arrives while you are reading, which is
    // the only situation it is for -- so it says something different.
    Announce(slot == 0 ? L"odvtedy nič nepribudlo"
                       : L"záložka " + std::to_wstring(slot) + L" je prázdna");
    return;
  }
  GoToOffset(*offset);
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
  if (statusBar_) statusBar_->Set(StatusBar::kTurn, status_);
}

void SessionPane::ShowSessionFacts(const proto::Event& event) {
  const auto text = [&event](const char* name) -> std::wstring {
    auto found = event.raw.find(name);
    if (found == event.raw.end() || !found->is_string()) return {};
    return model::Utf16FromUtf8(found->get<std::string>());
  };

  // Gathered before the bar is written and whether or not there is a bar: the
  // details dialog needs these too, and the bar is optional here.
  if (!text("model").empty()) details_.model = text("model");
  if (!text("permissionMode").empty()) {
    details_.permissionMode = text("permissionMode");
  }

  if (!statusBar_) return;
  // Model and permission mode in one field.  The design said "model and
  // effort", but system/init carries no effort -- and the permission mode is
  // the more useful of the two anyway: it decides whether anything will be put
  // to you at all.
  // Every field says what it is.  Read out one after another they are four
  // bare values otherwise, and "claude-opus-5, pokus, 5 h 83 %" is a riddle.
  std::wstring facts = L"model " + text("model");
  const std::wstring mode = text("permissionMode");
  if (!mode.empty() && mode != L"default") facts += L", režim " + mode;
  statusBar_->Set(StatusBar::kModel, facts);
}

void SessionPane::ShowRateLimit(const proto::Event& event) {
  proto::RateLimit limit;
  if (!statusBar_ || !proto::ParseRateLimit(event.raw, &limit)) return;

  const auto percent = [](double share) {
    return std::to_wstring(static_cast<int>(share * 100 + 0.5)) + L" %";
  };
  std::wstring text;
  if (limit.fiveHourUtilization >= 0) {
    text = L"5 h " + percent(limit.fiveHourUtilization);
  }
  if (limit.sevenDayUtilization >= 0) {
    if (!text.empty()) text += L", ";
    text += L"7 d " + percent(limit.sevenDayUtilization);
  }
  if (text.empty() && limit.utilization >= 0) {
    text = percent(limit.utilization);
  }
  if (limit.status == "rejected") {
    text = text.empty() ? L"limit vyčerpaný" : L"limit vyčerpaný, " + text;
  } else if (limit.status == "allowed_warning") {
    text = text.empty() ? L"blízko limitu" : L"blízko limitu, " + text;
  }
  if (limit.resetsAt > 0) {
    const std::time_t when = static_cast<std::time_t>(limit.resetsAt);
    std::tm local = {};
    if (localtime_s(&local, &when) == 0) {
      wchar_t stamp[32] = {};
      // Day and time, not just time: a seven-day window resets on some other
      // day, and "resets at 6:00" would be read as this morning.
      std::wcsftime(stamp, 32, L"%#d.%#m. %H:%M", &local);
      text += (text.empty() ? L"" : L", ") + std::wstring(L"obnova ") + stamp;
    }
  }
  statusBar_->Set(StatusBar::kLimit, text.empty() ? text : L"limit " + text);
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
  if (busy_) {
    Announce(L"ťah ešte beží, prompt zostal v poli");
    return;
  }
  const std::wstring text = GetText(prompt_);
  if (IsBlank(text)) {
    Announce(L"prázdny prompt");
    return;
  }

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

  session_.SendPrompt(model::Utf8FromUtf16(text));
  SetWindowTextW(prompt_, L"");
  interrupted_ = false;
  thinkingSaid_ = false;
  spokeThisTurn_ = false;
  busy_ = true;
  SetStatus(L"pracujem");
  // Said as well as written.  The bar is silent until NVDA+End is pressed, and
  // "did that go?" is a question one has right after pressing the key, not one
  // worth a second key.  This answers the key, so it interrupts -- invariant 7
  // allows exactly that, and only that.
  Announce(L"pracujem");
}

void SessionPane::Interrupt() {
  if (!busy_) {
    Announce(L"nič nebeží");
    return;
  }
  if (!session_.Interrupt()) {
    // The session and the pane disagree about whether a turn is running.  Say
    // so rather than pretending: the reader is about to wait for something to
    // stop that nobody is stopping.
    Announce(L"prerušenie sa nepodarilo poslať");
    return;
  }
  // Not busy_ = false.  The turn ends when the CLI says it does, with a
  // Result like any other turn; until then something is still coming and the
  // status line must not claim otherwise.
  interrupted_ = true;
  SetStatus(L"prerušujem");
  Apply(model_.AppendInterrupted());
  Announce(L"prerušujem");
}

void SessionPane::CopySessionId() {
  RefreshFacts();
  // Before the first system/init there is no id at all -- the CLI makes it,
  // we only read it back -- and a key that quietly put an empty string on the
  // clipboard would be found out in the terminal, pasting nothing.
  if (details_.id.empty()) {
    Announce(L"id session zatiaľ nie je známe, pošlite najprv prompt");
    return;
  }
  if (!win::SetClipboardText(host_, details_.id)) {
    Announce(L"schránku sa nepodarilo otvoriť, id skopírované nebolo");
    return;
  }
  // Short on purpose.  The id itself is 36 characters of hex and reading it
  // out is no use to anybody: the only thing done with it is pasting it.
  Announce(L"id skopírované");
}

void SessionPane::RefreshFacts() {
  // Asked for at the moment they are wanted, not kept up to date from the
  // records: Session takes the id off the FIRST record that carries one,
  // whichever kind that turns out to be, and copying it in system/init would
  // mean the id existed but stayed invisible until an init happened to come.
  details_.id = model::Utf16FromUtf8(session_.sessionId());
  // The mode is known from the initialize handshake, which is answered before
  // the first turn -- so this is the one fact here that does not have to say
  // "not yet".  Not overwritten with nothing when the answer has not arrived:
  // what was asked for on the command line is better than a blank.
  const std::string mode = session_.permissionMode();
  if (!mode.empty()) details_.permissionMode = model::Utf16FromUtf8(mode);
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
    // Both of these are the same key in both boxes, like Esc and the chords:
    // which box has the focus is not something to have to remember first.  A
    // function key needs no second discard either -- it produces no WM_CHAR at
    // all, which is one whole class of trap it cannot fall into.
    if (wParam == VK_F2) {
      pane->ShowDetails();
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
      // Two controls, so Tab is a toggle.  Done here rather than through
      // IsDialogMessage because win::RunMessageLoop is shared with another
      // project and this must not change how it behaves there.
      SetFocus(pane->transcript_);
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
  if (message == WM_CHAR && (wParam == VK_TAB || wParam == 0x0A)) return 0;
  if (message == WM_DESTROY) RemoveWindowSubclass(window, PromptProc, id);
  return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK SessionPane::TranscriptProc(HWND window, UINT message,
                                             WPARAM wParam, LPARAM lParam,
                                             UINT_PTR id, DWORD_PTR data) {
  SessionPane* pane = reinterpret_cast<SessionPane*>(data);
  if (message == WM_SETFOCUS) pane->lastFocus_ = window;
  if (message == WM_KEYDOWN && wParam == VK_TAB) {
    SetFocus(pane->prompt_);
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
  if (message == WM_KEYDOWN && wParam == VK_F2) {
    pane->ShowDetails();
    return 0;
  }
  if (message == WM_KEYDOWN && IsCopyIdChord(wParam)) {
    pane->CopySessionId();
    return 0;
  }
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
