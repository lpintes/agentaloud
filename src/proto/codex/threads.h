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
// active first.  An empty cursor is the first page; an empty cwd is every
// folder's threads (the filter is optional in the schema).
Json ThreadListParams(const std::string& cwdUtf8, const std::string& cursor,
                      int limit);

// Appends the threads of one thread/list result and returns the cursor of the
// next page, empty after the last.  A thread without an id is skipped: there
// is nothing to resume it by.
std::string AppendThreadPage(const Json& result,
                             std::vector<agent::Conversation>* out);

// The same page read for the folders: one entry per thread, its cwd and
// updatedAt.  A thread without a cwd is skipped.  Returns the next cursor.
std::string AppendThreadFolders(const Json& result,
                                std::vector<agent::Project>* out);

// One entry per folder, the newest thread's time.  Folders are told apart by
// the text Codex stored, exactly: the cwd is stored as the client wrote it,
// so one folder can come in two spellings here, and making those one is the
// caller's, who merges the lists of every agent anyway.
std::vector<agent::Project> FoldersOfThreads(
    std::vector<agent::Project> threads);

}  // namespace proto::codex

#endif
