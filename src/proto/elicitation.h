#ifndef PROTO_ELICITATION_H
#define PROTO_ELICITATION_H

// MCP elicitation: a server asks the reader to fill in a form.  The schema is
// MCP's (McpElicitationSchema, protocol 2025-11-25), not a CLI's, and both
// CLIs hand it on unchanged, so what reads it and what writes the answer is
// shared; each adapter only unwraps its own envelope around it.
//
// Measured with Codex (tools/probe_elicit_form.notes.md): every keyword
// arrives, but the properties come sorted by name, and nothing on the way
// checks a value against the schema.

#include <string>
#include <vector>

#include "agent/backend.h"
#include "agent/events.h"
#include "proto/jsonl.h"

namespace proto {

// The fields of `requestedSchema`, required first, the rest after, each part
// by name.  A property of a type the schema does not know is a text field:
// the reader can still answer it, and the server says if the answer is wrong.
std::vector<agent::FormField> ReadFormFields(const Json& schema);

// The `content` of an accepted answer: each value as the JSON type its field
// says.  A number that does not parse is sent as the text typed.
Json FormContent(const std::vector<agent::FormField>& fields,
                 const std::vector<std::vector<std::string>>& values);

// For the transcript, which no item of the CLI's fills: what the server asked
// and what went back.
agent::ToolCall FormCall(const agent::FormRequest& request,
                         const std::string& callId);
agent::ToolResult FormResult(const std::string& callId,
                             const agent::FormRequest& request,
                             const agent::FormAnswer& answer);

}  // namespace proto

#endif
