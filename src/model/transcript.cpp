#include "model/transcript.h"

#include "i18n/i18n.h"
#include "model/utf.h"

namespace model {
namespace {

constexpr size_t kSummaryLimit = 100;
constexpr size_t kInlineResultLimit = 60;

// Every line break becomes exactly one character, and it is '\n'.
//
// Not tidiness.  RichEdit stores a paragraph break as a single character and
// counts it as one, so text carrying "\r\n" would be two characters here and
// one there.  The map of block to character range is the whole basis of
// navigation, collapsing and bookmarks, so a drift of one character per line
// of tool output ends as a caret in the wrong place -- the exact failure this
// application exists to remove.  Tool output does carry "\r\n": it comes from
// Windows programs.
std::wstring NormalizeNewlines(std::wstring text) {
  std::wstring out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == L'\r') {
      if (i + 1 < text.size() && text[i + 1] == L'\n') ++i;
      out.push_back(L'\n');
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

// Terminal escape sequences, thrown away rather than rendered.
//
// Tool output is the output of terminal programs, so it carries them: colours
// (ESC [ 31;1 m), but also cursor control from progress bars (ESC [ 2K,
// ESC [ 1A, ESC [ G).  Left in, a screen reader reads them out character by
// character; they are noise in the one place where noise costs the most.
//
// Colouring them instead was considered and is claude-gui-lkk.5.16.  It does
// not change this function: the cursor sequences say "go back and overwrite
// the line", which is terminal redrawing, and this application does not redraw
// a terminal.  They would have to be dropped either way.
//
// CSI (ESC [ ... final) and OSC (ESC ] ... BEL or ESC \) go whole.  A lone ESC
// goes on its own, keeping what follows: in the corpus those are ordinary
// characters that happen to sit behind an ESC, not two-character sequences.
std::wstring StripEscapes(const std::wstring& text) {
  std::wstring out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const wchar_t character = text[i];
    if (character == 0x07) continue;  // BEL: a beep we did not ask for
    if (character != 0x1B) {
      out.push_back(character);
      continue;
    }
    if (i + 1 >= text.size()) break;  // trailing ESC, nothing to keep
    if (text[i + 1] == L'[') {
      // Parameters and intermediates, then one final byte in 0x40..0x7E.
      size_t at = i + 2;
      while (at < text.size() && text[at] >= 0x20 && text[at] <= 0x3F) ++at;
      if (at < text.size() && text[at] >= 0x40 && text[at] <= 0x7E) {
        i = at;
      } else {
        i = text.size() - 1;  // unterminated: the rest is not text either
      }
      continue;
    }
    if (text[i + 1] == L']') {
      size_t at = i + 2;
      while (at < text.size() && text[at] != 0x07 &&
             !(text[at] == 0x1B && at + 1 < text.size() &&
               text[at + 1] == L'\\')) {
        ++at;
      }
      i = at < text.size() && text[at] == 0x1B ? at + 1 : at;
      continue;
    }
    // Anything else: drop the ESC alone and let the next character stand.
  }
  return out;
}

// Control characters, written out as "\x00" rather than thrown away.
//
// Thrown away was the first fix and it was the wrong half of the answer: it
// stopped the damage and lost the information, and a transcript that quietly
// has fewer characters than the tool printed is the same class of thing this
// application exists to get rid of.  The escapes above are dropped because
// they are instructions to a terminal; a control character in the middle of
// output is CONTENT -- somebody read a binary -- and the reader asked to see
// it.  Written out, it is also audible: a screen reader says "backslash x
// zero zero", where the character itself is read as nothing at all.
//
// The notation is ambiguous with a tool that printed those four characters
// literally, and that is accepted: a terminal cannot tell them apart either,
// and the alternative notations that can (U+2400 CONTROL PICTURES) are read
// aloud as silence, which defeats the point.
//
// NUL is the one that breaks something.  The transcript reaches RichEdit as a
// null-terminated string (EM_REPLACESEL), so a NUL inside a block ENDS the
// insertion there: the model keeps text the widget never got, the range map is
// wrong from that offset on, and every jump after it lands somewhere else.
// Measured 6. 9. 2026 on a real session: `grep -a` over an .exe answered with
// 502 characters carrying 19 NULs -- strings in a binary are UTF-16LE, so
// every second byte is zero -- the widget took the first 47, and the title bar
// said NESÚLAD MAPY ROZSAHOV for the rest of the session.  Tool output is the
// output of terminal programs and some of them read binaries, so this is
// ordinary, not exotic.
//
// The others are written out for the two reasons already down here: VT (0x0B)
// and FF (0x0C) are a line break and a page break to RichEdit, so left as they
// are they would break a line the model does not know about -- that is
// invariant 4 -- and the rest are read out by a screen reader as nothing at
// all, which is invariant 8.  Tab and newline stay as themselves: they are
// text, and a transcript full of "\x0a" would be unreadable.
//
// After NormalizeNewlines, never before it.  CR is a control character too,
// and writing it out first would turn a lone CR -- a line break on its own in
// older output -- into the four characters "\x0d" instead of into a newline.
std::wstring EscapeControls(const std::wstring& text) {
  std::wstring out;
  out.reserve(text.size());
  for (wchar_t character : text) {
    if (character == L'\t' || character == L'\n' ||
        (character >= 0x20 && character != 0x7F)) {
      out.push_back(character);
      continue;
    }
    static const wchar_t kDigits[] = L"0123456789abcdef";
    out += L"\\x";
    out.push_back(kDigits[(character >> 4) & 0xF]);
    out.push_back(kDigits[character & 0xF]);
  }
  return out;
}

std::wstring Widen(const std::string& text) {
  return EscapeControls(NormalizeNewlines(StripEscapes(Utf16FromUtf8(text))));
}

// A break at the very end closes the last line rather than opening another:
// a file written as "Čaj je lepší než káva.\n" is one line, and counting it as
// two said "2 riadky" about it -- found live on Codex, true of Claude's Write.
size_t CountLines(const std::wstring& text) {
  if (text.empty()) return 0;
  size_t lines = 1;
  for (wchar_t character : text) {
    if (character == L'\n') ++lines;
  }
  if (text.back() == L'\n') --lines;
  return lines;
}

// One line, no newlines, not longer than the limit.  Used for every summary,
// because a summary that wraps stops being a summary.
std::wstring OneLine(const std::wstring& text, size_t limit) {
  std::wstring flat;
  flat.reserve(text.size());
  bool space = false;
  for (wchar_t character : text) {
    if (character == L'\n' || character == L'\r' || character == L'\t') {
      space = true;
      continue;
    }
    if (space && !flat.empty()) flat.push_back(L' ');
    space = false;
    flat.push_back(character);
    if (flat.size() >= limit) {
      flat += L"...";
      return flat;
    }
  }
  return flat;
}

std::wstring Count(size_t lines) {
  return i18n::Count(i18n::Plural::kLines, static_cast<long long>(lines));
}

// A path as it belongs in a summary line: what is left of it once the project
// is taken off the front.
//
// The whole path does not fit -- summaries are cut at kSummaryLimit -- and it
// is cut at the END, which on a path throws away the file name and keeps
// "C:\Users\...\AppData\Local\Temp\...".  Heard out loud at every tool call
// that is not merely long, it is the wrong half.
//
// Comparison is case-insensitive and treats the two separators as one: the
// working directory arrives from system/init with forward slashes and tool
// arguments come back with backslashes, for the same file.
std::wstring ShortenPath(const std::wstring& path, const std::wstring& root) {
  const auto same = [](wchar_t left, wchar_t right) {
    if (left == L'/') left = L'\\';
    if (right == L'/') right = L'\\';
    if (left >= L'A' && left <= L'Z') left = static_cast<wchar_t>(left + 32);
    if (right >= L'A' && right <= L'Z') right = static_cast<wchar_t>(right + 32);
    return left == right;
  };
  if (!root.empty() && path.size() > root.size() &&
      std::equal(root.begin(), root.end(), path.begin(), same)) {
    size_t at = root.size();
    if (path[at] == L'\\' || path[at] == L'/') ++at;
    if (at < path.size()) return path.substr(at);
  }
  // Not under the project: keep the tail, mark that the head is gone.  The
  // limit is the summary's, less the room the tool name and the marker take.
  constexpr size_t kPathLimit = 70;
  if (path.size() > kPathLimit) {
    return L"..." + path.substr(path.size() - kPathLimit);
  }
  return path;
}

// The questions of a call that puts them to the reader, as text.  Without
// this the summary and the body would be the field dump of an array of
// objects -- and this is the one call the reader has to be able to go back
// and re-read, because it is a question that was put to them.  See
// ui/ask_dialog.h for the answering.
//
// `full` picks the body from the summary: the summary is one line and gets the
// first question, the body gets every question with its options under it.
std::wstring RenderQuestions(const std::vector<agent::Question>& questions,
                             bool full) {
  if (questions.empty()) return {};
  if (!full) {
    // "(+2 ďalšie)" and not the rest of them: this is the line heard when
    // arrowing past the block, and the block itself is one keystroke away.
    const std::wstring first = Widen(questions.front().text);
    const size_t rest = questions.size() - 1;
    return rest == 0 ? first
                     : first + L" (+" + std::to_wstring(rest) + L" ďalšie)";
  }
  std::wstring text;
  size_t number = 0;
  for (const agent::Question& question : questions) {
    ++number;
    if (!text.empty()) text += L'\n';
    text += std::to_wstring(number) + L". " + Widen(question.text);
    if (question.multiSelect) text += L" (dá sa označiť viac)";
    for (const agent::QuestionOption& option : question.options) {
      text += L'\n';
      text += L"   " + Widen(option.label);
      if (!option.description.empty()) {
        text += L" — " + Widen(option.description);
      }
    }
  }
  return text;
}

// What the call actually does, for the one-line summary.  Which argument
// that is was decided by the adapter, which knows the tool; what is left here
// is how to say it.
std::wstring PrimaryText(const agent::ToolCall& call,
                         const std::wstring& root) {
  if (call.kind == agent::ToolKind::Question) {
    const std::wstring asked = RenderQuestions(call.questions, false);
    if (!asked.empty()) return asked;
  }
  const std::wstring primary = Widen(call.primary);
  return call.primaryIsPath ? ShortenPath(primary, root) : primary;
}

std::wstring RenderFields(const std::vector<agent::ToolField>& fields) {
  std::wstring body;
  for (const agent::ToolField& field : fields) {
    if (!body.empty()) body.push_back(L'\n');
    // A nameless field is an input that was not an object at all; it is
    // shown as it came.
    if (!field.name.empty()) body += Widen(field.name) + L": ";
    body += Widen(field.value);
  }
  return body;
}

}  // namespace

// What a tool call will actually do, for the calls where the field dump was
// the only place in the whole transcript that said what changed -- and said
// it as JSON.  "old_string: ... / new_string: ..." is a record of the
// arguments; a reader wants the two texts, one after the other, under a word
// that says which is which.
//
// No colours and no diff markers of our own.  A '+' and a '-' at the start of
// every line is read out as a character per line, and picking the changed
// lines apart would mean writing a diff -- the whole old and the whole new is
// what the call actually contained, and it is the truth.  A CLI that sends
// only a diff gets its diff shown, because then that is the truth.
//
// Anything else falls through to the field dump, which for a shell command or
// a grep is exactly right: a command IS its arguments.
//
// It is out of the anonymous namespace because the permission dialog shows the
// call before it runs and the transcript shows it after -- and those two have
// to be the same text, or the reader allows one thing and then reads another.
std::wstring RenderToolCall(const agent::ToolCall& call) {
  // Which file, whole, in front of what happens to it.  The summary line has
  // the path too, but shortened, and the permission dialog has no summary at
  // all: "obsah: ..." alone asked to allow a write without saying where to --
  // found on Codex's apply_patch and true of Claude's Write and Edit as well.
  const std::wstring file =
      call.primaryIsPath ? L"súbor: " + Widen(call.primary) + L'\n' : L"";
  if (call.kind == agent::ToolKind::EditFile && !call.replacements.empty()) {
    std::wstring body;
    for (const agent::TextReplacement& replacement : call.replacements) {
      if (!body.empty()) body += L'\n';
      body += L"pôvodné:\n" + Widen(replacement.before) + L"\nnové:\n" +
              Widen(replacement.after);
      if (replacement.everywhere) body += L"\nvšetky výskyty";
    }
    return file + body;
  }
  if (call.kind == agent::ToolKind::CreateFile && call.newContent) {
    return file + L"obsah:\n" + Widen(*call.newContent);
  }
  if (!call.diff.empty()) return file + L"zmena:\n" + Widen(call.diff);
  if (call.kind == agent::ToolKind::Question) {
    const std::wstring asked = RenderQuestions(call.questions, true);
    if (!asked.empty()) return asked;
  }
  return RenderFields(call.fields);
}

namespace {

// The size of what a call is about to do, said in the summary so that it does
// not have to be unfolded to be judged.  "Edit: transcript.cpp" and "Edit:
// transcript.cpp, 3 riadky na 40 riadkov" are two different pieces of news.
std::wstring InputDetail(const agent::ToolCall& call) {
  if (call.kind == agent::ToolKind::EditFile && !call.replacements.empty()) {
    const agent::TextReplacement& first = call.replacements.front();
    return Count(CountLines(Widen(first.before))) + L" na " +
           Count(CountLines(Widen(first.after)));
  }
  if (call.kind == agent::ToolKind::CreateFile && call.newContent) {
    return Count(CountLines(Widen(*call.newContent)));
  }
  return {};
}

Block MakeThinking(const std::wstring& text) {
  Block block;
  block.kind = BlockKind::Thinking;
  block.body = text;
  block.summary = L"premýšľanie (" + Count(CountLines(text)) + L")";
  block.collapsed = true;
  return block;
}

Block MakeAssistantText(const std::wstring& text) {
  Block block;
  block.kind = BlockKind::AssistantText;
  block.body = text;
  block.summary = OneLine(text, kSummaryLimit);
  return block;
}

Block MakeToolUse(const agent::ToolCall& call, const std::wstring& root) {
  Block block;
  block.kind = BlockKind::ToolUse;
  block.toolUseId = call.id;
  block.toolName = Widen(call.name);
  block.toolKind = call.kind;
  block.body = RenderToolCall(call);
  block.summary = block.toolName + L": " +
                  OneLine(PrimaryText(call, root), kSummaryLimit);
  const std::wstring detail = InputDetail(call);
  if (!detail.empty()) block.summary += L", " + detail;
  block.collapsed = true;
  return block;
}

// The calls whose successful result says nothing the call did not already
// say.  Their answer is one word, and it is the word the reader is waiting
// for: it happened.  Everything else -- the path, the size -- stands in the
// summary of the call, one line above.
const wchar_t* DoneWord(agent::ToolKind kind) {
  if (kind == agent::ToolKind::EditFile) return L"zapísané";
  if (kind == agent::ToolKind::CreateFile) return L"vytvorené";
  return nullptr;
}

// `call` is the block the result is filed behind, or nullptr when the call is
// not in the transcript; the result is then rendered as any tool's would be.
Block MakeToolResult(const agent::ToolResult& result, const Block* call) {
  Block block;
  block.kind = BlockKind::ToolResult;
  block.toolUseId = result.callId;
  if (call != nullptr) {
    block.toolName = call->toolName;
    block.toolKind = call->toolKind;
  }
  block.body = Widen(result.text);
  block.isError = result.isError;
  block.collapsed = true;

  // "The file ... has been updated successfully. (file state is current in
  // your context ...)" -- one line, but longer than kInlineResultLimit, so it
  // used to collapse into "výstup (1 riadok)": a line of transcript and a
  // sentence of speech that said nothing at all, after every single edit.
  const wchar_t* done =
      block.isError || call == nullptr ? nullptr : DoneWord(block.toolKind);
  if (done != nullptr) {
    block.body = done;
    block.summary = done;
    block.collapsible = false;
    block.collapsed = false;
    return block;
  }

  const std::wstring label = block.isError ? L"chyba" : L"výstup";
  const size_t lines = CountLines(block.body);
  if (lines <= 1 && block.body.size() <= kInlineResultLimit) {
    // A one-line result is shorter than the sentence describing it, so it is
    // shown whole and there is nothing to expand.  Marking it uncollapsible
    // rather than merely expanded matters: otherwise pressing the toggle key
    // on it would rewrite the line into a header and a copy of itself.
    block.summary = label + (block.body.empty() ? L" (prázdny)"
                                                : L": " + block.body);
    block.collapsible = false;
    block.collapsed = false;
  } else if (block.isError) {
    // An error says what went wrong in its first sentence, and that sentence
    // is the whole point of the block.  Collapsed to "chyba (3 riadky)" the
    // only difference from an ordinary output was one word, which is nothing
    // at all when heard -- reported from use.  The count stays behind it, so
    // that it is clear there is more to unfold.
    block.summary = label + L": " + OneLine(block.body, kSummaryLimit) +
                    L" (" + Count(lines) + L")";
  } else {
    block.summary = label + L" (" + Count(lines) + L")";
  }
  return block;
}

}  // namespace

// Who said it, written in front of the line.  Only the two kinds that carry
// speech get one: a mechanism block already names itself in its heading, and a
// denied tool was said by neither party.
//
// The prefix is not part of the block's text, so a copy of the body stays free
// of it.  It is not private to the rendering either: read aloud, an answer
// with no name in front of it is indistinguishable from a tool summary, which
// is exactly what a turn of alternating sentences and tools sounded like.
// Speech asks for it by the same call the transcript does -- one place decides
// what a speaker is called.
std::wstring Transcript::SpeakerPrefix(BlockKind kind) const {
  switch (kind) {
    case BlockKind::UserPrompt: return L"you: ";
    case BlockKind::AssistantText: return agentName_ + L": ";
    default: return L"";
  }
}

bool Transcript::SetAgentName(const std::wstring& name) {
  if (!blocks_.empty()) return false;
  agentName_ = name;
  return true;
}

bool IsMechanism(BlockKind kind) {
  switch (kind) {
    case BlockKind::Thinking:
    case BlockKind::ToolUse:
    case BlockKind::ToolResult:
      return true;
    case BlockKind::UserPrompt:
    case BlockKind::AssistantText:
    case BlockKind::PermissionDenied:
    case BlockKind::Interrupted:
      return false;
  }
  return false;
}

const wchar_t* KindLabel(BlockKind kind) {
  switch (kind) {
    case BlockKind::UserPrompt: return L"prompt";
    case BlockKind::AssistantText: return L"odpoveď";
    case BlockKind::PermissionDenied: return L"zamietnuté";
    case BlockKind::Interrupted: return L"prerušenie";
    case BlockKind::Thinking: return L"premýšľanie";
    case BlockKind::ToolUse: return L"nástroj";
    case BlockKind::ToolResult: return L"výstup";
  }
  return L"?";
}

std::wstring Transcript::Render(const Block& block) const {
  const std::wstring prefix = SpeakerPrefix(block.kind);
  if (block.collapsed) return prefix + block.summary + L'\n';
  // An expanded mechanism block keeps its heading.  Without it there is no
  // way to tell, reading line by line, where a tool's output starts, where it
  // ends, or whether what you are in is expanded at all -- the collapsed form
  // describes itself and the expanded form used to be bare text.  Content
  // blocks get no heading: a prompt and an answer are what the reader came
  // for and a label above every one of them is a line of noise per turn.
  // They get the speaker prefix instead, on the first line, which says the
  // same thing without spending a line on it.
  if (IsMechanism(block.kind) && block.collapsible) {
    return block.summary + L", rozbalené\n" + block.body + L'\n';
  }
  return prefix + block.body + L'\n';
}

Edit Transcript::AppendBlocks(std::vector<Block> blocks) {
  Edit edit;
  edit.start = text_.size();
  if (blocks.empty()) return edit;

  for (Block& block : blocks) {
    block.id = nextBlockId_++;
    const std::wstring rendered = Render(block);
    block.start = text_.size() + edit.inserted.size();
    block.length = rendered.size();
    edit.inserted += rendered;
    blocks_.push_back(std::move(block));
  }
  text_ += edit.inserted;
  return edit;
}

Edit Transcript::InsertBlocks(size_t at, std::vector<Block> blocks) {
  if (blocks.empty()) return Edit{text_.size(), 0, {}};
  if (at >= blocks_.size()) return AppendBlocks(std::move(blocks));

  Edit edit;
  edit.start = blocks_[at].start;
  std::vector<Block> made;
  size_t offset = edit.start;
  for (Block& block : blocks) {
    block.id = nextBlockId_++;
    const std::wstring rendered = Render(block);
    block.start = offset;
    block.length = rendered.size();
    offset += rendered.size();
    edit.inserted += rendered;
    made.push_back(std::move(block));
  }

  text_.insert(edit.start, edit.inserted);
  for (size_t i = at; i < blocks_.size(); ++i) {
    blocks_[i].start += edit.inserted.size();
  }
  blocks_.insert(blocks_.begin() + static_cast<ptrdiff_t>(at), made.begin(),
                 made.end());
  return edit;
}

std::optional<size_t> Transcript::PlaceForResult(
    const std::string& toolUseId) const {
  if (toolUseId.empty()) return std::nullopt;
  for (size_t i = blocks_.size(); i-- > 0;) {
    if (blocks_[i].kind == BlockKind::ToolUse &&
        blocks_[i].toolUseId == toolUseId) {
      return i + 1;
    }
  }
  return std::nullopt;
}

const Block* Transcript::CallFor(const std::string& toolUseId) const {
  const std::optional<size_t> place = PlaceForResult(toolUseId);
  if (!place.has_value()) return nullptr;
  return &blocks_[*place - 1];
}

std::optional<size_t> Transcript::IndexOfId(size_t id) const {
  for (size_t i = 0; i < blocks_.size(); ++i) {
    if (blocks_[i].id == id) return i;
  }
  return std::nullopt;
}

namespace {

// The prompt, whether it comes out of the edit box or out of a replayed
// history.  The edit box hands back "\r\n"; it needs the same normalising as
// anything off the stream -- and the same writing out of control characters,
// because text pasted into that box came from somewhere with rules of its own.
Block MakeUserPrompt(const std::wstring& text) {
  Block block;
  block.kind = BlockKind::UserPrompt;
  block.body = EscapeControls(NormalizeNewlines(text));
  block.summary = OneLine(block.body, kSummaryLimit);
  return block;
}

Block MakeInterrupted() {
  Block block;
  block.kind = BlockKind::Interrupted;
  block.body = L"Prerušené používateľom.";
  block.summary = block.body;
  // One line, so there is nothing behind the summary to unfold.
  block.collapsible = false;
  // So that the E key finds it.  Navigating to trouble means navigating to
  // every place the work did not go through, and a turn stopped by hand is
  // one of those -- the reader is looking for where things stopped.
  block.isError = true;
  return block;
}

}  // namespace

Edit Transcript::AppendUserPrompt(const std::wstring& text) {
  return AppendBlocks({MakeUserPrompt(text)});
}

Edit Transcript::AppendInterrupted() {
  return AppendBlocks({MakeInterrupted()});
}

void Transcript::NoteUnknown(const std::string& type) {
  ++unknownCount_;
  for (const std::string& seen : unknownTypes_) {
    if (seen == type) return;
  }
  unknownTypes_.push_back(type);
}

std::vector<Edit> Transcript::Append(const std::vector<agent::Event>& events) {
  std::vector<Block> made;

  for (const agent::Event& event : events) {
    // Empty text and empty thinking do arrive -- a message can carry a block
    // that never got any content, and text can be nothing but escapes.  They
    // are not worth a line each; "premýšľanie (0 riadkov)" is noise between
    // the things the reader came for.  An empty tool result is different and
    // stays: that a command printed nothing is an answer.
    if (const auto* text = std::get_if<agent::AssistantText>(&event)) {
      const std::wstring wide = Widen(text->text);
      if (!wide.empty()) made.push_back(MakeAssistantText(wide));
    } else if (const auto* thinking = std::get_if<agent::Thinking>(&event)) {
      const std::wstring wide = Widen(thinking->text);
      if (!wide.empty()) made.push_back(MakeThinking(wide));
    } else if (const auto* started =
                   std::get_if<agent::ToolCallStarted>(&event)) {
      made.push_back(MakeToolUse(started->call, projectRoot_));
    } else if (const auto* finished =
                   std::get_if<agent::ToolCallFinished>(&event)) {
      // The call is looked up among the blocks already placed.  A batch that
      // carried a call and its own result would not find it, and the result
      // would go to the end -- which is right behind the call anyway.
      made.push_back(MakeToolResult(finished->result,
                                    CallFor(finished->result.callId)));
    } else if (const auto* denied = std::get_if<agent::ToolDenied>(&event)) {
      Block block;
      block.kind = BlockKind::PermissionDenied;
      const std::wstring message = Widen(denied->message);
      block.body = message.empty()
                       ? L"Nástroj " + Widen(denied->toolName) +
                             L" bol zamietnutý."
                       : message;
      block.summary = OneLine(block.body, kSummaryLimit);
      made.push_back(std::move(block));
    } else if (const auto* prompt = std::get_if<agent::UserPrompt>(&event)) {
      made.push_back(MakeUserPrompt(Utf16FromUtf8(prompt->text)));
    } else if (std::holds_alternative<agent::Interrupted>(event)) {
      made.push_back(MakeInterrupted());
    } else if (const auto* directory =
                   std::get_if<agent::WorkingDirectory>(&event)) {
      // Makes no block, but it is the project, and tool paths are shortened
      // against it.
      if (projectRoot_.empty()) projectRoot_ = Widen(directory->path);
    } else if (const auto* unknown = std::get_if<agent::Unrecognised>(&event)) {
      NoteUnknown(unknown->type);
    }
    // Everything else is deliberately not in the transcript: the mode, the
    // model, the usage and the limits belong in the status bar, which a
    // screen reader reads on request and not on every change.  That is the
    // whole reason this application exists.
  }

  // A tool result goes behind the call it answers, not at the end.  Claude
  // runs tools in parallel, so the results come back in whatever order they
  // finish, and appended in arrival order the transcript reads as: a call, an
  // output, another call, a third call, two outputs.  Which output belonged to
  // which command was then something the reader had to guess -- reported from
  // use, and the reason this exists.
  std::vector<Edit> edits;
  std::vector<Block> atEnd;
  auto flush = [this, &edits, &atEnd]() {
    if (atEnd.empty()) return;
    edits.push_back(AppendBlocks(std::move(atEnd)));
    atEnd.clear();
  };
  for (Block& block : made) {
    const std::optional<size_t> place =
        block.kind == BlockKind::ToolResult ? PlaceForResult(block.toolUseId)
                                            : std::nullopt;
    if (!place.has_value() || *place >= blocks_.size()) {
      atEnd.push_back(std::move(block));
      continue;
    }
    // Anything already waiting for the end goes first, so that blocks made
    // from one record keep the order the record had them in.
    flush();
    std::vector<Block> one;
    one.push_back(std::move(block));
    edits.push_back(InsertBlocks(*place, std::move(one)));
  }
  flush();
  return edits;
}

Edit Transcript::SetCollapsed(size_t index, bool collapsed) {
  Edit edit;
  if (index >= blocks_.size()) return edit;
  Block& block = blocks_[index];
  if (!block.collapsible || block.collapsed == collapsed) return edit;

  block.collapsed = collapsed;
  const std::wstring rendered = Render(block);
  edit.start = block.start;
  edit.removed = block.length;
  edit.inserted = rendered;

  text_.replace(block.start, block.length, rendered);
  const size_t oldLength = block.length;
  block.length = rendered.size();

  // Everything after it moves by the difference.  Signed arithmetic on
  // purpose: collapsing shrinks the buffer and unsigned would wrap.
  const ptrdiff_t shift =
      static_cast<ptrdiff_t>(rendered.size()) - static_cast<ptrdiff_t>(oldLength);
  for (size_t i = index + 1; i < blocks_.size(); ++i) {
    blocks_[i].start = static_cast<size_t>(
        static_cast<ptrdiff_t>(blocks_[i].start) + shift);
  }
  return edit;
}

std::optional<size_t> Transcript::BlockAt(size_t offset) const {
  if (blocks_.empty()) return std::nullopt;
  if (offset >= text_.size()) return blocks_.size() - 1;
  for (size_t i = 0; i < blocks_.size(); ++i) {
    if (offset < blocks_[i].start + blocks_[i].length) return i;
  }
  return blocks_.size() - 1;
}

std::optional<size_t> Transcript::NextWhere(
    size_t offset, const BlockPredicate& match) const {
  const std::optional<size_t> here = BlockAt(offset);
  const size_t from = here.has_value() ? *here + 1 : 0;
  for (size_t i = from; i < blocks_.size(); ++i) {
    if (match(blocks_[i])) return i;
  }
  return std::nullopt;
}

std::optional<size_t> Transcript::PreviousWhere(
    size_t offset, const BlockPredicate& match) const {
  const std::optional<size_t> here = BlockAt(offset);
  if (!here.has_value()) return std::nullopt;
  // Backwards means "the nearest block whose start is before the caret", so
  // the block the caret is standing in counts as long as the caret is not
  // already at its start.  Skipping it unconditionally makes the last block
  // unreachable from the end of the buffer -- Ctrl+End and then 'A' landed on
  // the last but one answer, and there is no key that goes back to the one it
  // stepped over.  At the start of a block the caret has already arrived
  // there (that is where GoToBlock puts it), so it must move on, or the key
  // would answer with the same block for ever.
  const size_t from = offset > blocks_[*here].start ? *here + 1 : *here;
  for (size_t i = from; i-- > 0;) {
    if (match(blocks_[i])) return i;
  }
  return std::nullopt;
}

std::optional<size_t> Transcript::NextOfKind(size_t offset,
                                             BlockKind kind) const {
  return NextWhere(offset,
                   [kind](const Block& block) { return block.kind == kind; });
}

std::optional<size_t> Transcript::PreviousOfKind(size_t offset,
                                                 BlockKind kind) const {
  return PreviousWhere(
      offset, [kind](const Block& block) { return block.kind == kind; });
}

std::wstring Transcript::LineAt(size_t offset) const {
  if (offset > text_.size()) offset = text_.size();
  // From offset - 1, not from offset: standing on a newline means standing at
  // the end of the line it closes, not at the start of the next one.
  const size_t previous =
      offset == 0 ? std::wstring::npos : text_.rfind(L'\n', offset - 1);
  const size_t begin = previous == std::wstring::npos ? 0 : previous + 1;
  const size_t end = text_.find(L'\n', begin);
  const size_t stop = end == std::wstring::npos ? text_.size() : end;
  return text_.substr(begin, stop - begin);
}

std::wstring Transcript::FirstLine(size_t index) const {
  if (index >= blocks_.size()) return {};
  return LineAt(blocks_[index].start);
}

bool Transcript::CheckInvariants(std::string* problem) const {
  size_t expected = 0;
  for (size_t i = 0; i < blocks_.size(); ++i) {
    const Block& block = blocks_[i];
    if (block.start != expected) {
      if (problem) {
        *problem = "blok " + std::to_string(i) + " zacina na " +
                   std::to_string(block.start) + ", cakalo sa " +
                   std::to_string(expected);
      }
      return false;
    }
    if (block.length == 0) {
      if (problem) *problem = "blok " + std::to_string(i) + " ma nulovu dlzku";
      return false;
    }
    expected += block.length;
  }
  if (expected != text_.size()) {
    if (problem) {
      *problem = "sucet dlzok blokov je " + std::to_string(expected) +
                 ", buffer ma " + std::to_string(text_.size());
    }
    return false;
  }
  return true;
}

}  // namespace model
