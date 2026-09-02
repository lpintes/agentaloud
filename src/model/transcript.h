#ifndef MODEL_TRANSCRIPT_H
#define MODEL_TRANSCRIPT_H

// The conversation as one block of text, plus the map that says which part of
// it is which.  Knows nothing about RichEdit, windows, or how any of it is
// drawn -- ui/transcript_view is the only thing that turns an offset here into
// a caret position.
//
// The buffer is std::wstring, that is UTF-16, and that is deliberate.  The
// view is a RichEdit and RichEdit counts in UTF-16 code units; keeping the
// buffer in UTF-8 would mean converting every offset on every navigation and
// every collapse, which is a class of off-by-a-few bug that would show up as
// the one thing this application exists to avoid -- a caret that lands in the
// wrong place.  The conversion happens once, when a block is built.
//
// Collapsing is done by rewriting the text, not by hiding it.  RichEdit can
// hide characters with CFE_HIDDEN, which would leave every offset alone and be
// much less work, but it is not established that a screen reader honours it --
// and if it does not, the failure is a "collapsed" block that gets read out in
// full, discovered by a user rather than by us.  Rewriting costs the range map
// below, which navigation and bookmarks need anyway, so it is paid for once.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "proto/events.h"

namespace model {

enum class BlockKind {
  UserPrompt,        // what we typed; added locally, not read off the stream
  AssistantText,     // the answer
  PermissionDenied,  // a tool a rule would not let through
  Thinking,
  ToolUse,
  ToolResult,
};

// Content or mechanism.  Mechanism collapses by default; content never does.
// The split is not by length: a two-line tool call collapses and a long answer
// does not, because the question is what the text is for, not how big it is.
bool IsMechanism(BlockKind kind);

const wchar_t* KindLabel(BlockKind kind);

struct Block {
  BlockKind kind = BlockKind::AssistantText;
  bool collapsed = false;
  std::wstring summary;  // the single line shown when collapsed, no newline
  std::wstring body;     // the whole thing, may be many lines, no newline
  std::string toolUseId; // ties ToolUse to its ToolResult; empty otherwise
  bool isError = false;

  // Offsets into Transcript::Text(), maintained by Transcript.  length always
  // includes the block's trailing newline, so the blocks tile the buffer with
  // no gaps -- which is what CheckInvariants() verifies.
  size_t start = 0;
  size_t length = 0;
};

// One replacement for the view to apply.  The view needs the minimal range
// because it has to restore the caret afterwards, and it can only do that if
// it knows exactly what moved.
struct Edit {
  size_t start = 0;
  size_t removed = 0;
  std::wstring inserted;

  bool empty() const { return removed == 0 && inserted.empty(); }
};

class Transcript {
 public:
  // Added when the prompt is sent, not when the stream echoes it back: the
  // text should appear the moment it is sent, and --replay-user-messages would
  // put it a round trip away.
  Edit AppendUserPrompt(const std::wstring& text);

  // Zero or more blocks.  An assistant message with thinking, text and a tool
  // call yields three; system/init yields none.
  Edit Append(const proto::Event& event);

  Edit SetCollapsed(size_t index, bool collapsed);

  const std::wstring& Text() const { return text_; }
  const std::vector<Block>& blocks() const { return blocks_; }

  // Which block an offset falls in.  Blocks tile the buffer, so this always
  // answers unless the transcript is empty.
  std::optional<size_t> BlockAt(size_t offset) const;

  // Navigation for the single-letter keys.  Both take a caret offset and
  // return a block index, skipping the block the caret is already in.
  std::optional<size_t> NextOfKind(size_t offset, BlockKind kind) const;
  std::optional<size_t> PreviousOfKind(size_t offset, BlockKind kind) const;

  // Records we did not recognise.  Not an error and not shown: Claude Code
  // gains record types between releases, and an application that stopped at
  // the first one would break on somebody else's schedule.  The soak tests
  // watch this so a new type is noticed by us and not by a user.
  size_t unknownCount() const { return unknownCount_; }
  const std::vector<std::string>& unknownTypes() const {
    return unknownTypes_;
  }

  // Every block accounted for, in order, tiling the buffer exactly.  Cheap
  // enough to call after every event in the tests.
  bool CheckInvariants(std::string* problem) const;

 private:
  Edit AppendBlocks(std::vector<Block> blocks);
  std::wstring Render(const Block& block) const;
  void NoteUnknown(const proto::Event& event);

  std::wstring text_;
  std::vector<Block> blocks_;
  size_t unknownCount_ = 0;
  std::vector<std::string> unknownTypes_;
};

}  // namespace model

#endif
