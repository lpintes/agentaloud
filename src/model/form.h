#ifndef MODEL_FORM_H
#define MODEL_FORM_H

// Checking a value of a form an MCP server asked for (agent::FormRequest)
// against its field, before it goes back.  Nothing on the way does it --
// Codex passes a missing required field, a word for a number or a value
// outside the list to the server unchanged (tools/probe_elicit_form.notes.md)
// -- and a server that gets it wrong answers the model, not the reader, who
// would then not learn what to fix.

#include <string>
#include <vector>

#include "agent/backend.h"

namespace model {

// Empty when `values` may go back for `field`, else the sentence that says
// what to fix, in the reader's language.  `values` as agent::FormAnswer
// carries them: none, or one empty text, is a field left blank, which only a
// required field refuses.
std::string CheckFormField(const agent::FormField& field,
                           const std::vector<std::string>& values);

}  // namespace model

#endif
