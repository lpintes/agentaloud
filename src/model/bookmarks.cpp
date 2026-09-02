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

Mark MarkAt(const Transcript& transcript, size_t offset) {
  Mark mark;
  const std::optional<size_t> block = transcript.BlockAt(offset);
  if (!block.has_value()) return mark;  // empty transcript: nothing to mark
  mark.set = true;
  mark.blockId = transcript.blocks()[*block].id;
  mark.offset = offset - transcript.blocks()[*block].start;
  return mark;
}

std::optional<size_t> OffsetOf(const Transcript& transcript, const Mark& mark) {
  if (!mark.set) return std::nullopt;
  const std::optional<size_t> index = transcript.IndexOfId(mark.blockId);
  if (!index.has_value()) return std::nullopt;
  const Block& block = transcript.blocks()[*index];
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
