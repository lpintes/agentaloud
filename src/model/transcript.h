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
#include <functional>
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
  // Assigned once and never changed.  The index of a block DOES change: a tool
  // result is inserted next to the call it belongs to, which pushes everything
  // after it down one.  Anything that has to name a block across time -- a
  // bookmark, the start of a turn -- names this and not the index.
  size_t id = 0;

  BlockKind kind = BlockKind::AssistantText;
  bool collapsed = false;
  // False when there is nothing behind the summary -- a tool result of one
  // short line is already shown whole, and offering to expand it would be
  // offering nothing.  SetCollapsed does nothing to such a block.
  bool collapsible = true;
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

  // Zero or more blocks, and therefore zero or more edits: a record carrying
  // two tool results for two calls made in parallel writes into two different
  // places, and an Edit is one contiguous range by design -- the view needs
  // the minimal range to put the caret back.
  std::vector<Edit> Append(const proto::Event& event);

  Edit SetCollapsed(size_t index, bool collapsed);

  const std::wstring& Text() const { return text_; }
  const std::vector<Block>& blocks() const { return blocks_; }

  // The id the next block to be created will get.  Everything made from here
  // on has an id at least this large, which is how "the blocks of this turn"
  // is expressed without holding indices that move.
  size_t nextBlockId() const { return nextBlockId_; }
  std::optional<size_t> IndexOfId(size_t id) const;

  // Which block an offset falls in.  Blocks tile the buffer, so this always
  // answers unless the transcript is empty.
  std::optional<size_t> BlockAt(size_t offset) const;

  // Navigation for the single-letter keys.  All four take a caret offset and
  // return a block index, skipping the block the caret is already in.
  //
  // The predicate pair exists because one of the keys does not select a kind:
  // "!" looks for trouble, and trouble is either a PermissionDenied block or a
  // tool result with is_error set.  Rather than invent a kind that the stream
  // does not have, the caller says what it is looking for.
  using BlockPredicate = std::function<bool(const Block&)>;
  std::optional<size_t> NextWhere(size_t offset,
                                  const BlockPredicate& match) const;
  std::optional<size_t> PreviousWhere(size_t offset,
                                      const BlockPredicate& match) const;
  std::optional<size_t> NextOfKind(size_t offset, BlockKind kind) const;
  std::optional<size_t> PreviousOfKind(size_t offset, BlockKind kind) const;

  // The whole line an offset falls in, without its newline.  What a screen
  // reader would read out if the reader had arrowed onto that line themselves.
  std::wstring LineAt(size_t offset) const;

  // The first line of a block as it stands on screen -- the heading of a
  // collapsed one, "..., rozbalene" when it is open, the speaker prefix and
  // the opening line of an answer.  It is what a screen reader would read out
  // if the reader had arrowed onto that line themselves, and since NVDA says
  // nothing about a caret it did not move, every jump has to say it instead.
  std::wstring FirstLine(size_t index) const;

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
  // `at` is the index to insert before; past the end means append.
  Edit InsertBlocks(size_t at, std::vector<Block> blocks);
  // Where a result for this tool_use_id belongs: right behind the call.
  // Empty when the call is not in the transcript, which happens when a tool
  // was started before we attached to the session.
  std::optional<size_t> PlaceForResult(const std::string& toolUseId) const;
  std::wstring Render(const Block& block) const;
  void NoteUnknown(const proto::Event& event);

  std::wstring text_;
  std::vector<Block> blocks_;
  size_t nextBlockId_ = 1;
  size_t unknownCount_ = 0;
  std::vector<std::string> unknownTypes_;
};

}  // namespace model

#endif
