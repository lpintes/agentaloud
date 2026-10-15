#ifndef MODEL_LINKS_H
#define MODEL_LINKS_H

// Alt+Enter (claude-gui-lkk.74): web addresses in transcript text.
//
// Found in text rather than taken from structure because no CLI marks them:
// an address is whatever the model or a tool happened to write, in prose, in
// markdown, in a JSON string or in a shell's output.  So the work is in the
// edges -- what stops an address, and what at its end belongs to the sentence
// around it rather than to it.
//
// Only http and https.  What is found here goes to ShellExecute, which runs
// whatever it is given, and text in the transcript is written by a model and
// by tools nobody vetted.  A file:// or a bare path would be a program run
// from a chat; paths are claude-gui-lkk.7.10's, and open in a window of ours.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace model {

struct Link {
  size_t start = 0;   // into the text searched
  size_t length = 0;
  std::wstring url;   // the text at [start, start + length)
  // The text of a markdown link, "[title](url)"; empty for a bare address.
  // A list of addresses alone is a list of hosts and paths, and which of
  // them is "the PEP" or "the Czech article" is what the model wrote in front.
  std::wstring title;
};

// Every address in `text`, in order.
std::vector<Link> FindLinks(const std::wstring& text);

// The address `offset` falls in, if any.  The caret before the first
// character counts, the caret after the last does not: that is the caret on
// whatever follows.
std::optional<Link> LinkAt(const std::wstring& text, size_t offset);

// The links of FindLinks, each address once, in the order they first appear.
// A list that offers the same page twice makes the reader compare them.  The
// title is the first one the address was given, if any was.
std::vector<Link> DistinctLinks(const std::wstring& text);

}  // namespace model

#endif
