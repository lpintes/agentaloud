#ifndef PROTO_CODEX_THREADS_H
#define PROTO_CODEX_THREADS_H

// A folder's conversations as Codex lists them: thread/list, a page at a time.
//
// Only the shape of the wire is here, so that the tests see it; the process
// that is asked is CodexBackend::ListConversations.  What the shape is was
// measured, not read (tools/probe_codex_list.notes.md): the default list
// already leaves out subagents' threads, archived ones and threads that never
// had a turn, and the cwd filter compares paths, not text -- so nothing here
// filters or normalizes.

#include <string>
#include <vector>

#include "agent/backend.h"
#include "proto/jsonl.h"

namespace proto::codex {

// The params of thread/list for one page of `cwd`'s threads, most recently
// active first.  An empty cursor is the first page.
Json ThreadListParams(const std::string& cwdUtf8, const std::string& cursor,
                      int limit);

// Appends the threads of one thread/list result and returns the cursor of the
// next page, empty after the last.  A thread without an id is skipped: there
// is nothing to resume it by.
std::string AppendThreadPage(const Json& result,
                             std::vector<agent::Conversation>* out);

}  // namespace proto::codex

#endif
