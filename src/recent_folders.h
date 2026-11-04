#ifndef RECENT_FOLDERS_H
#define RECENT_FOLDERS_H

// The list of recently used folders the New session dialog offers
// (claude-gui-lkk.89): one list for all agents, made of what each of them
// says it has worked in (agent::Backend::ListProjects).  A folder both agents
// know is one line, under the agent that worked in it last -- picking the line
// is picking the folder AND the agent, and the one used last is the likelier
// to be wanted again.
//
// Kept apart from ui/ and without windows.h so the tests can hold the rule.

#include <string>
#include <vector>

namespace app {

// One folder as one agent reported it.
struct FolderReport {
  // What makes two spellings of one folder equal.  The caller's to make
  // (win::FolderKey), which has the API for case; compared here as it is.
  std::wstring key;
  std::wstring path;         // as the agent spelt it
  long long lastActive = 0;  // as agent::Project::lastActive
  int backend = 0;           // index into the caller's list of agents
};

struct RecentFolder {
  // The spelling of the newest report: the one the agent would recognise
  // today, should a folder have been written two ways over the years.
  std::wstring path;
  long long lastActive = 0;
  int backend = 0;  // the agent of the newest report
};

// One entry per key, newest first.  A tie in lastActive keeps the report that
// came first, and folders equally recent stay in the order of their first
// report, so the list does not reshuffle between two openings of the dialog.
std::vector<RecentFolder> MergeRecentFolders(
    const std::vector<FolderReport>& reports);

// Whether the folder whose key is `key` lies in one of `rootKeys`, or is one
// of them -- a profile's projects= (claude-gui-lkk.90).  Keys made by the same
// rule as FolderReport::key, so C:\Praca and c:/praca/ are one root.  Whole
// folders only: C:\praca does not take in C:\pracaX.  No roots takes in
// everything.
bool UnderAnyRoot(const std::wstring& key,
                  const std::vector<std::wstring>& rootKeys);

}  // namespace app

#endif
