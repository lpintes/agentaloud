#ifndef MODEL_CODE_H
#define MODEL_CODE_H

// Ctrl+Shift+C (claude-gui-lkk.100): the code an answer tells the reader to
// run or paste -- "run `make check`", or a fenced block of a script.
//
// Found in the markdown of each block's body, not in the buffer: the buffer
// puts the speaker's name in front of a block's first line, and an answer that
// opens with a fence would then have no fence at the start of a line.  Per
// block also keeps a fence one block left open from swallowing the next ones.
//
// Read as markdown is written, not as CommonMark specifies it to the letter: a
// fence may be indented any amount (models indent them inside list items), and
// inline code does not run over a line break -- a stray backtick in prose
// would otherwise pair with one three lines down.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace model {

class Transcript;

struct CodeSpan {
  // Into the text searched, delimiters included: the caret on the backtick or
  // on a fence line is on the code too.
  size_t start = 0;
  size_t length = 0;
  std::wstring code;    // what goes on the clipboard, no delimiters
  bool fenced = false;  // a block between fences rather than `inline`
};

// Every piece of code in `text`, in order.  An unclosed fence is no code, and
// its line is read as prose: an answer still streaming has its closing fence
// to come, and copying half a script is worse than copying nothing.  Code that
// is empty is not returned -- there would be nothing to paste.
std::vector<CodeSpan> FindCode(const std::wstring& text);

// The code of every block as it is on screen, offsets into Transcript::Text().
// A collapsed block shows its summary and offers nothing: what it hides is
// not what the reader is looking at.
std::vector<CodeSpan> FindCode(const Transcript& transcript);

// The span `offset` is in, or else the first one after it, or else -- with
// nothing left below the caret -- the last one before it.  The caret right
// after a span counts as on it.  Forward and not the nearest either way: the
// reader is on "Run `x`" because they are reading it, and the code a line
// they have read leads up to comes after it.  Nearest by distance picked the
// code ending the line above, one character back, over `x` six ahead -- and
// a rule a reader cannot predict is a rule they have to check every time.
std::optional<CodeSpan> CodeAtOrAfter(const std::vector<CodeSpan>& spans,
                                      size_t offset);

}  // namespace model

#endif
