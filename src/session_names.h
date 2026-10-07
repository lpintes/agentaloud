#ifndef SESSION_NAMES_H
#define SESSION_NAMES_H

// Telling apart sessions opened on the same folder.  A session is named after
// its folder -- in its title, in the Window menu, in the name NVDA says on
// Ctrl+Tab -- and two sessions in one checkout were two identical names with
// nothing to choose between them.  So they get a number.
//
// Kept apart from ui/ and without windows.h so the tests can hold the rule.

#include <string>
#include <vector>

namespace app {

// One number per session, in the order the sessions were opened.  Zero for a
// session alone in its folder: the number is there to tell sessions apart,
// and a lone one has nothing to be told apart from -- "(1)" on every title
// would be one more word read with every switch.  Sessions sharing a folder
// are numbered 1..n with no gaps, so closing the second of three makes the
// third the second: a number is then always "which of the ones in this
// folder", never a count of what has been opened since startup.
//
// Keys are compared as they are; making two spellings of one folder the same
// key -- case, slashes -- is the caller's, which has the API for it.
std::vector<int> SessionOrdinals(const std::vector<std::wstring>& keys);

}  // namespace app

#endif
