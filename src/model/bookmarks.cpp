#include "model/bookmarks.h"

namespace model {

void Bookmarks::Set(size_t slot, const Mark& mark) {
  if (slot >= kSlots) return;
  marks_[slot] = mark;
}

const Mark& Bookmarks::Get(size_t slot) const {
  if (slot >= kSlots) return empty_;
  return marks_[slot];
}

namespace {

// SetCollapsed ignores a block with nothing behind its summary, so the flag
// alone can say collapsed about a block that reads the same either way.
bool ShowsCollapsed(const Block& block) {
  return block.collapsed && block.collapsible;
}

}  // namespace

Mark MarkAt(const Transcript& transcript, size_t offset) {
  Mark mark;
  const std::optional<size_t> block = transcript.BlockAt(offset);
  if (!block.has_value()) return mark;  // empty transcript: nothing to mark
  mark.set = true;
  mark.blockId = transcript.blocks()[*block].id;
  mark.offset = offset - transcript.blocks()[*block].start;
  mark.collapsed = ShowsCollapsed(transcript.blocks()[*block]);
  return mark;
}

std::optional<size_t> BlockToExpand(const Transcript& transcript,
                                    const Mark& mark) {
  if (!mark.set || mark.collapsed) return std::nullopt;
  const std::optional<size_t> index = transcript.IndexOfId(mark.blockId);
  if (!index.has_value()) return std::nullopt;
  if (!ShowsCollapsed(transcript.blocks()[*index])) return std::nullopt;
  // The newline ending the first line still belongs to it.
  const size_t firstLineEnd = transcript.ExpandedText(*index).find(L'\n');
  if (mark.offset <= firstLineEnd) return std::nullopt;
  return index;
}

std::optional<size_t> OffsetOf(const Transcript& transcript, const Mark& mark) {
  if (!mark.set) return std::nullopt;
  const std::optional<size_t> index = transcript.IndexOfId(mark.blockId);
  if (!index.has_value()) return std::nullopt;
  const Block& block = transcript.blocks()[*index];
  // Measured in the other form, the distance names no particular character
  // here.  What is left of it is the block, and its start is the one line both
  // forms agree on.  A mark deeper than that was expanded into by
  // BlockToExpand before this was asked, so the forms match by now.
  if (mark.collapsed != ShowsCollapsed(block)) return block.start;
  // The block may have been collapsed since, and then the remembered distance
  // points past its end.  Clamping keeps the mark inside the block it named,
  // which is the part of it that was meant; landing in the next block would be
  // worse than landing on the wrong line of the right one.  length always
  // counts the trailing newline, so length - 1 is the last character before it.
  const size_t inside =
      mark.offset < block.length ? mark.offset : block.length - 1;
  return block.start + inside;
}

}  // namespace model
