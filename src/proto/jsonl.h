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

using Json = nlohmann::json;

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

}  // namespace proto

#endif
