#include "model/code.h"

#include "model/transcript.h"

namespace model {

namespace {

size_t LineEnd(const std::wstring& text, size_t at) {
  const size_t end = text.find(L'\n', at);
  return end == std::wstring::npos ? text.size() : end;
}

bool IsBlank(wchar_t c) { return c == L' ' || c == L'\t'; }

size_t SkipBlanks(const std::wstring& text, size_t at, size_t end) {
  while (at < end && IsBlank(text[at])) ++at;
  return at;
}

size_t RunOf(const std::wstring& text, size_t at, size_t end, wchar_t c) {
  size_t run = 0;
  while (at + run < end && text[at + run] == c) ++run;
  return run;
}

struct Fence {
  wchar_t mark = L'`';
  size_t count = 0;
  size_t indent = 0;  // blanks in front of the opening fence
};

// "```" or "~~~", three or more, with a language after it if any.  A
// backtick fence with a backtick in the rest of its line is not a fence but
// inline code: "```x```" on one line.
bool OpensFence(const std::wstring& text, size_t start, size_t end,
                Fence* fence) {
  const size_t at = SkipBlanks(text, start, end);
  if (at == end || (text[at] != L'`' && text[at] != L'~')) return false;
  const wchar_t mark = text[at];
  const size_t count = RunOf(text, at, end, mark);
  if (count < 3) return false;
  if (mark == L'`' && text.find(L'`', at + count) < end) return false;
  *fence = {mark, count, at - start};
  return true;
}

// At least as long as the opening one and nothing after it: "```bash" opens
// a block, it never closes one.
bool ClosesFence(const std::wstring& text, size_t start, size_t end,
                 const Fence& fence) {
  const size_t at = SkipBlanks(text, start, end);
  const size_t count = RunOf(text, at, end, fence.mark);
  if (count < fence.count) return false;
  return SkipBlanks(text, at + count, end) == end;
}

// The fence's own indentation comes off every line inside it: a block in a
// list item is indented as a whole, and the script pasted should not be.
std::wstring Dedent(const std::wstring& text, size_t start, size_t end,
                    size_t indent) {
  size_t at = start;
  while (at < end && at - start < indent && IsBlank(text[at])) ++at;
  return text.substr(at, end - at);
}

bool AllBlank(const std::wstring& text) {
  for (wchar_t c : text) {
    if (!IsBlank(c) && c != L'\n') return false;
  }
  return true;
}

// `code` and ``code with a ` in it`` on the line [start, end).  A run of
// backticks is closed only by a run of the same length; one with no partner
// is a literal backtick.
void FindInline(const std::wstring& text, size_t start, size_t end,
                std::vector<CodeSpan>* spans) {
  size_t at = start;
  while (at < end) {
    if (text[at] != L'`') {
      ++at;
      continue;
    }
    const size_t open = at;
    const size_t count = RunOf(text, at, end, L'`');
    at += count;
    for (size_t look = at; look < end;) {
      if (text[look] != L'`') {
        ++look;
        continue;
      }
      const size_t run = RunOf(text, look, end, L'`');
      if (run != count) {
        look += run;
        continue;
      }
      std::wstring code = text.substr(at, look - at);
      // The one space each side that lets code start or end with a backtick.
      if (code.size() >= 2 && code.front() == L' ' && code.back() == L' ' &&
          !AllBlank(code)) {
        code = code.substr(1, code.size() - 2);
      }
      if (!AllBlank(code)) {
        spans->push_back({open, look + run - open, std::move(code), false});
      }
      at = look + run;
      break;
    }
  }
}

}  // namespace

std::vector<CodeSpan> FindCode(const std::wstring& text) {
  std::vector<CodeSpan> spans;
  size_t line = 0;
  while (line <= text.size()) {
    const size_t end = LineEnd(text, line);
    Fence fence;
    bool fenced = false;
    if (OpensFence(text, line, end, &fence)) {
      std::wstring code;
      bool first = true;
      for (size_t inner = end; inner < text.size();) {
        const size_t innerStart = inner + 1;
        const size_t innerEnd = LineEnd(text, innerStart);
        if (ClosesFence(text, innerStart, innerEnd, fence)) {
          if (!AllBlank(code)) {
            spans.push_back({line, innerEnd - line, std::move(code), true});
          }
          fenced = true;
          line = innerEnd + 1;
          break;
        }
        if (!first) code += L'\n';
        code += Dedent(text, innerStart, innerEnd, fence.indent);
        first = false;
        inner = innerEnd;
      }
    }
    if (fenced) continue;
    FindInline(text, line, end, &spans);
    if (end == text.size()) break;
    line = end + 1;
  }
  return spans;
}

std::vector<CodeSpan> FindCode(const Transcript& transcript) {
  const std::wstring& text = transcript.Text();
  std::vector<CodeSpan> spans;
  for (const Block& block : transcript.blocks()) {
    if (block.collapsed || block.body.empty()) continue;
    // The body is the end of what Render drew, before the block's newline,
    // whatever stands in front of it -- a speaker's name, an expanded tool's
    // heading.  Checked rather than trusted: a one-line error draws its
    // summary instead, and code found in a body that is not on screen would
    // be copied from under some other text.
    if (block.length < block.body.size() + 1) continue;
    const size_t bodyStart = block.start + block.length - 1 - block.body.size();
    if (text.compare(bodyStart, block.body.size(), block.body) != 0) continue;
    for (CodeSpan& span : FindCode(block.body)) {
      span.start += bodyStart;
      spans.push_back(std::move(span));
    }
  }
  return spans;
}

std::optional<CodeSpan> CodeAtOrAfter(const std::vector<CodeSpan>& spans,
                                      size_t offset) {
  if (spans.empty()) return std::nullopt;
  // The spans are in order, so the first one not over before the caret is
  // the one it is on or the next one.
  for (const CodeSpan& span : spans) {
    if (span.start + span.length >= offset) return span;
  }
  return spans.back();
}

}  // namespace model
