#ifndef MODEL_BOOKMARKS_H
#define MODEL_BOOKMARKS_H

// Places in the transcript worth coming back to.
//
// A mark is NOT an offset.  Offsets move the moment a block above them
// collapses or expands, and a bookmark that quietly slides three lines is
// worse than no bookmark at all -- the reader would have no way of telling.
// Nor is it an index: an index moves too, because a tool result is inserted
// behind the call it answers rather than at the end.  What does not move is
// Block::id.  So a mark is that id plus how far into the block it sat, and
// coming back clamps that distance into the block as it stands now.
//
// Slot 0 is not the reader's.  It is where they were standing when something
// new arrived, written by the application on every batch of new blocks.  That
// is the situation this whole feature exists for: you were somewhere in the
// text, an answer came in, and in a terminal there is no way back.

//
// Slots 1..9 also outlive the window (claude-gui-lkk.7.12), and there Block::id
// is no use: it is a counter, and a replayed history makes its blocks in a
// different order from the live run.  What a saved mark names instead is the
// block's anchor -- its kind and Block::sourceId, an id the CLI keeps in the
// history -- and it is bound back to a Block::id the first time it is jumped
// to.  The line it sat on goes along as a fingerprint, so that a block which
// reads differently now is said to, rather than landed in quietly.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "model/transcript.h"

namespace model {

// The blockId of a mark read from disk, before Bind has found its block.
inline constexpr size_t kUnbound = static_cast<size_t>(-1);

struct Mark {
  bool set = false;
  size_t blockId = 0;
  size_t offset = 0;  // characters from the start of that block
  // Which form of the block that distance was measured in.  The collapsed
  // and the expanded form share no line but the first, and even that one
  // reads differently (the expanded one carries a heading), so a distance
  // means something only in the form it was taken in.
  bool collapsed = false;
  // The anchor, as it was when the mark was made or read.  Serialize asks the
  // block again rather than trusting this: a live prompt gets its anchor only
  // when the CLI echoes it, after the mark may have been made.
  BlockKind kind = BlockKind::AssistantText;
  std::string sourceId;
  uint64_t line = 0;  // LineFingerprint of the marked line
};

class Bookmarks {
 public:
  // 1..9 are the reader's, 0 is "where I was when it arrived".
  static constexpr size_t kSlots = 10;

  void Set(size_t slot, const Mark& mark);
  const Mark& Get(size_t slot) const;

 private:
  std::array<Mark, kSlots> marks_ = {};
  const Mark empty_ = {};
};

// Where an offset is, said in a way that survives the text moving.
Mark MarkAt(const Transcript& transcript, size_t offset);

// And back again, clamped into the block as it now stands.  Empty when the
// slot was never set or the block is gone -- which cannot happen today, but
// restoring a session (claude-gui-lkk.7) will make it possible.  The block is
// looked up by id, so an insertion anywhere above it changes nothing here.
std::optional<size_t> OffsetOf(const Transcript& transcript, const Mark& mark);

// The block to expand before OffsetOf, when the mark sits past the first line
// of a block that has been collapsed since (claude-gui-lkk.5.30).  Without it
// the mark is clamped onto the summary and the reader has to expand and press
// the key a second time.  A mark on the first line expands nothing: that line
// is the summary in either form, and expanding a block the reader did not ask
// into would put hundreds of lines under them for nothing.
std::optional<size_t> BlockToExpand(const Transcript& transcript,
                                    const Mark& mark);

// Finds the block a mark read from disk names, and gives the mark its id.
// True when the mark is bound now or was already; false when no block carries
// its anchor -- a conversation compacted or rewritten since, or a kind the
// history does not keep.  Nothing is guessed from the order of blocks.
bool Bind(const Transcript& transcript, Mark& mark);

// Whether the line at offset is no longer the one the mark was made on.  Only
// when the mark's form matches the block's: in the other form OffsetOf lands
// on the start of the block by design, and that line was never the marked one.
// A mark of this session reads its own line back, so it changes only when the
// block was rewritten (a plan reworked, agent::ToolCallChanged).
bool LineChanged(const Transcript& transcript, const Mark& mark,
                 size_t offset);

uint64_t LineFingerprint(const std::wstring& line);

// The file of one conversation's bookmarks, one line per slot:
//   <slot> <kind> <offset> <collapsed 0|1> <fingerprint hex> <sourceId>
// Slot 0 is not written, and neither is a mark whose block has no anchor; a
// line that does not parse is skipped, the rest still count.  The kind is a
// word, not the enum's number, so that reordering BlockKind does not move
// every saved mark onto another kind.
std::string SerializeBookmarks(const Bookmarks& bookmarks,
                               const Transcript& transcript);
Bookmarks ParseBookmarks(const std::string& text);

// Whether SerializeBookmarks would write this mark now: its block has an
// anchor.  A live prompt has none until the CLI echoes it, and a block of our
// own making (SessionEnded, Interrupted) never does.
bool CanPersist(const Transcript& transcript, const Mark& mark);

}  // namespace model

#endif
