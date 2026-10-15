#ifndef PROTO_JSONL_H
#define PROTO_JSONL_H

// Turning a byte stream into JSON records.
//
// The pipe gives whatever the operating system had ready, which is not lines:
// a single read can end halfway through a record, and a single record can
// arrive in four reads.  Claude's records are also large -- a tool result of a
// few hundred kilobytes on one line is ordinary -- so the split has to happen
// without copying the whole buffer each time.

#include <functional>
#include <string>
#include <string_view>

#include "vendor/json.hpp"

namespace proto {

// Ordered, because what has no shape of our own is read out in the order the
// server wrote it, and a server puts the name of a thing before its details;
// sorted, the name came last (claude-gui-lkk.78).  Records have a dozen keys,
// so the linear lookup costs nothing.  Equality is order-sensitive now.
using Json = nlohmann::ordered_json;

class LineAssembler {
 public:
  // Called once per complete line, without its terminator.  Empty lines are
  // dropped here rather than at every call site.
  using LineCallback = std::function<void(std::string_view)>;

  explicit LineAssembler(LineCallback onLine) : onLine_(std::move(onLine)) {}

  void Feed(std::string_view bytes);

  // Anything left when the stream ends.  A well-behaved child ends on a
  // newline and this does nothing; a killed one may not, and a half record is
  // worth seeing rather than losing silently.
  void Flush();

 private:
  LineCallback onLine_;
  std::string pending_;
};

// Parses one line.  Returns false and leaves `out` untouched when the line is
// not JSON -- which happens: a crashing child can put a stack trace on stdout,
// and that must not take the reader down with it.
bool ParseLine(std::string_view line, Json* out, std::string* error);

// One line, whitespace collapsed.  What goes into a sentence read out in one
// breath -- a prompt of twenty lines there would bury everything after it.
std::string OneLine(std::string_view text);

// JSON said as text: "key: value" a line, nested values indented under their
// key, list items numbered, a list of plain values on one line.  For the
// arguments and answers of tools that have no shape of our own -- MCP servers
// above all -- where the dump was braces, quotes and "\n" read out one by one
// (claude-gui-lkk.69).
std::string ReadableJson(const Json& value);

// The same for one argument, to go behind "name:".  A value that takes lines
// of its own starts with '\n' and comes indented.
std::string ReadableField(const Json& value);

// `text` as ReadableJson when the whole of it is a JSON object or array, else
// unchanged.  Only for what a tool returned that no one reads verbatim: a
// command's output or a file is the truth even when it is JSON.
std::string ReadableIfJson(const std::string& text);

}  // namespace proto

#endif
