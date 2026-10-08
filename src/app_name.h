#pragma once

// The application's name, in one place.  It used to be written out in some
// twenty captions and messages, and renaming meant a hunt through grep.
// Narrow on purpose: the resource compiler includes this too, and C++ gets
// the wide form by concatenation (L"" APP_NAME), the same way the version
// does.  Not in the generated version header: that one changes with every
// commit, and everything that names the app would be rebuilt with it.
#define APP_NAME "AgentAloud"

// The file name, for the version resource and for messages that tell the
// reader what to look for beside it.
#define APP_EXE "agentaloud.exe"

// Where the project lives, for O programe.
#define APP_URL "https://github.com/lpintes/agentaloud"
