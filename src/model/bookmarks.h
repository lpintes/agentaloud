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

#include <array>
#include <cstddef>
#include <optional>

#include "model/transcript.h"

namespace model {

struct Mark {
  bool set = false;
  size_t blockId = 0;
  size_t offset = 0;  // characters from the start of that block
  // Which form of the block that distance was measured in.  The collapsed
  // and the expanded form share no line but the first, and even that one
  // reads differently (the expanded one carries a heading), so a distance
  // means something only in the form it was taken in.
  bool collapsed = false;
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

}  // namespace model

#endif
