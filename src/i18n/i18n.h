#ifndef I18N_I18N_H
#define I18N_I18N_H

// The language of everything the reader hears or sees.
//
// Below every other layer and dependent on none of them, because the text is
// not only in ui/: model/ writes block summaries that are both transcript text
// and speech, and proto/ names the modes of its CLI.  No windows.h, so that
// LoadString and a STRINGTABLE were never an option -- model/ and proto/ may
// not call them.
//
// One file per language (i18n/<code>.def) over one list of ids (ids.def), so
// that a language is added as a file and not as a column in every line, and
// a language with a string missing does not compile.  Compiled in, not read
// from beside the .exe: the day a translator comes from outside, the
// one-file-per-language shape is what a loader would read anyway.
//
// The language is chosen once, at startup, before anything that makes text
// (main.cpp), and never changes after.  Block summaries are part of the
// transcript buffer; switching the language under a running session would
// change their length and move every range after them without an edit that
// knows (invariant 3).
//
// What the application says to the agent -- an instruction to the model, not
// text for the reader -- is not here; it stays English whatever the language.

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace i18n {

// In the order the help lists them.
enum class Lang { kSlovak, kEnglish };

enum class Str {
#define S(id) id,
#define P(id)
#include "i18n/ids.def"
#undef S
#undef P
  kCount
};

enum class Plural {
#define S(id)
#define P(id) id,
#include "i18n/ids.def"
#undef S
#undef P
  kCount
};

// Set once, before any text is made.  Slovak until then, which is what the
// application spoke before it spoke anything else.
void SetLanguage(Lang lang);
Lang Language();

// Every language there is.
std::vector<Lang> Languages();

// The code as written in settings.txt and as Windows starts its locale names:
// "sk", "en".  ParseLanguage is false for a code there is no language for.
bool ParseLanguage(std::string_view code, Lang* lang);
const char* LanguageCode(Lang lang);

const wchar_t* Text(Str id);

// The string with {0}, {1}... replaced by the arguments in order.  A
// placeholder without its argument stays as it is, so a missing argument is
// heard rather than silently dropped.
std::wstring Format(Str id, std::initializer_list<std::wstring_view> args);

// The number in the form its language wants.  Which form that is, is the
// language's rule: Slovak has three and looks at the whole number, English
// two, and a language like Polish would look at the last digits.
std::wstring Count(Plural id, long long n);

// How many forms the language's rule picks from, and how many its file gives
// for `id`.  The two must agree; a test holds them to it, because the
// compiler cannot.
size_t PluralFormCount(Lang lang);
size_t PluralFormsGiven(Lang lang, Plural id);

// For proto/, which speaks UTF-8.  The catalog is the only input, so there
// is no malformed text to guard against.
std::string Utf8(std::wstring_view text);
std::string Utf8(Str id);
// Format for proto/: the arguments are UTF-8 off the wire -- a path, a
// command -- and proto/ has no way to widen them (see sessions.h).
std::string Utf8(Str id, std::initializer_list<std::string_view> args);

}  // namespace i18n

#endif
