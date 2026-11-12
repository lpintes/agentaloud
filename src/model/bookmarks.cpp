#include "model/bookmarks.h"

#include <utility>
#include <vector>

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
  const Block& found = transcript.blocks()[*block];
  mark.set = true;
  mark.blockId = found.id;
  mark.offset = offset - found.start;
  mark.collapsed = ShowsCollapsed(found);
  mark.kind = found.kind;
  mark.sourceId = found.sourceId;
  mark.line = LineFingerprint(transcript.LineAt(offset));
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

uint64_t LineFingerprint(const std::wstring& line) {
  // FNV-1a over the UTF-16 units, the form the transcript holds.  Not for
  // secrecy, for a short line in a file a reader may open: equal or not is
  // all it has to tell.
  uint64_t hash = 14695981039346656037ull;
  for (const wchar_t unit : line) {
    hash ^= static_cast<uint16_t>(unit);
    hash *= 1099511628211ull;
  }
  return hash;
}

namespace {

// Only the kinds whose blocks carry an id from the CLI.  The rest are ours,
// made locally, and have no sourceId to find them by after a restart.
struct KindName {
  BlockKind kind;
  const char* name;
};
constexpr KindName kKindNames[] = {
    {BlockKind::UserPrompt, "prompt"},
    {BlockKind::AssistantText, "text"},
    {BlockKind::Thinking, "thinking"},
    {BlockKind::ToolUse, "tool-use"},
    {BlockKind::ToolResult, "tool-result"},
};

const char* NameOf(BlockKind kind) {
  for (const KindName& entry : kKindNames) {
    if (entry.kind == kind) return entry.name;
  }
  return nullptr;
}

std::optional<BlockKind> KindNamed(const std::string& name) {
  for (const KindName& entry : kKindNames) {
    if (name == entry.name) return entry.kind;
  }
  return std::nullopt;
}

// The anchor to write for a mark: the block's as it is now when the mark is
// bound, the one read from disk when it is not.
const Block* BoundBlock(const Transcript& transcript, const Mark& mark) {
  const std::optional<size_t> index = transcript.IndexOfId(mark.blockId);
  if (!index.has_value()) return nullptr;
  return &transcript.blocks()[*index];
}

bool Anchor(const Transcript& transcript, const Mark& mark, BlockKind* kind,
            std::string* sourceId) {
  if (!mark.set) return false;
  if (mark.blockId == kUnbound) {
    *kind = mark.kind;
    *sourceId = mark.sourceId;
  } else {
    const Block* block = BoundBlock(transcript, mark);
    if (block == nullptr) return false;
    *kind = block->kind;
    *sourceId = block->sourceId;
  }
  return !sourceId->empty() && NameOf(*kind) != nullptr;
}

// A whole token of digits, nothing else.  std::stoull would take "12abc" and
// a leading minus.
bool ParseUnsigned(const std::string& token, int base, uint64_t* value) {
  if (token.empty() || token.size() > 20) return false;
  uint64_t result = 0;
  for (const char c : token) {
    int digit = -1;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    if (digit < 0) return false;
    const uint64_t radix = static_cast<uint64_t>(base);
    const uint64_t add = static_cast<uint64_t>(digit);
    if (result > (UINT64_MAX - add) / radix) return false;  // overflow
    result = result * radix + add;
  }
  *value = result;
  return true;
}

std::vector<std::string> SplitSpaces(const std::string& line) {
  std::vector<std::string> tokens;
  size_t at = 0;
  while (at < line.size()) {
    const size_t end = line.find(' ', at);
    const size_t stop = end == std::string::npos ? line.size() : end;
    tokens.push_back(line.substr(at, stop - at));
    at = stop + 1;
  }
  return tokens;
}

std::optional<std::pair<size_t, Mark>> ParseLine(const std::string& line) {
  const std::vector<std::string> tokens = SplitSpaces(line);
  if (tokens.size() != 6) return std::nullopt;
  uint64_t slot = 0;
  uint64_t offset = 0;
  uint64_t fingerprint = 0;
  if (!ParseUnsigned(tokens[0], 10, &slot) || slot == 0 ||
      slot >= Bookmarks::kSlots) {
    return std::nullopt;
  }
  const std::optional<BlockKind> kind = KindNamed(tokens[1]);
  if (!kind.has_value()) return std::nullopt;
  if (!ParseUnsigned(tokens[2], 10, &offset)) return std::nullopt;
  if (tokens[3] != "0" && tokens[3] != "1") return std::nullopt;
  if (tokens[4].size() != 16 || !ParseUnsigned(tokens[4], 16, &fingerprint)) {
    return std::nullopt;
  }
  if (tokens[5].empty()) return std::nullopt;
  Mark mark;
  mark.set = true;
  mark.blockId = kUnbound;
  mark.offset = static_cast<size_t>(offset);
  mark.collapsed = tokens[3] == "1";
  mark.kind = *kind;
  mark.sourceId = tokens[5];
  mark.line = fingerprint;
  return std::make_pair(static_cast<size_t>(slot), mark);
}

std::string Hex16(uint64_t value) {
  static const char kDigits[] = "0123456789abcdef";
  std::string text(16, '0');
  for (size_t i = 16; i-- > 0;) {
    text[i] = kDigits[value & 0xF];
    value >>= 4;
  }
  return text;
}

}  // namespace

bool Bind(const Transcript& transcript, Mark& mark) {
  if (!mark.set) return false;
  if (mark.blockId != kUnbound) return true;
  if (mark.sourceId.empty()) return false;
  for (const Block& block : transcript.blocks()) {
    if (block.kind == mark.kind && block.sourceId == mark.sourceId) {
      mark.blockId = block.id;
      return true;
    }
  }
  return false;
}

bool LineChanged(const Transcript& transcript, const Mark& mark,
                 size_t offset) {
  const Block* block = BoundBlock(transcript, mark);
  if (block == nullptr) return false;
  if (mark.collapsed != ShowsCollapsed(*block)) return false;
  return LineFingerprint(transcript.LineAt(offset)) != mark.line;
}

bool CanPersist(const Transcript& transcript, const Mark& mark) {
  BlockKind kind;
  std::string sourceId;
  return Anchor(transcript, mark, &kind, &sourceId);
}

std::string SerializeBookmarks(const Bookmarks& bookmarks,
                               const Transcript& transcript) {
  std::string text;
  for (size_t slot = 1; slot < Bookmarks::kSlots; ++slot) {
    const Mark& mark = bookmarks.Get(slot);
    BlockKind kind;
    std::string sourceId;
    if (!Anchor(transcript, mark, &kind, &sourceId)) continue;
    // An id with a space or a line break in it would read back as another
    // line.  Neither CLI makes one, so refusing it costs nothing.
    if (sourceId.find_first_of(" \r\n") != std::string::npos) continue;
    text += std::to_string(slot) + ' ' + NameOf(kind) + ' ' +
            std::to_string(mark.offset) + ' ' + (mark.collapsed ? '1' : '0') +
            ' ' + Hex16(mark.line) + ' ' + sourceId + '\n';
  }
  return text;
}

Bookmarks ParseBookmarks(const std::string& text) {
  Bookmarks bookmarks;
  size_t at = 0;
  while (at < text.size()) {
    const size_t end = text.find('\n', at);
    const size_t stop = end == std::string::npos ? text.size() : end;
    std::string line = text.substr(at, stop - at);
    at = stop + 1;
    // A file edited in Notepad comes back with "\r\n".
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (const auto parsed = ParseLine(line)) {
      bookmarks.Set(parsed->first, parsed->second);
    }
  }
  return bookmarks;
}

}  // namespace model
