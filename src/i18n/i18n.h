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
// The language is chosen once, at startup, before anything that makes text
// (main.cpp), and never changes after.  Block summaries are part of the
// transcript buffer; switching the language under a running session would
// change their length and move every range after them without an edit that
// knows (invariant 3).
//
// What the application says to the agent -- an instruction to the model, not
// text for the reader -- is not here; it stays English whatever the language.

#include <initializer_list>
#include <string>
#include <string_view>

namespace i18n {

enum class Lang { kSlovak, kEnglish };

enum class Str {
#define S(id, sk, en) id,
#define P(id, sk1, sk2, sk5, en1, enN)
#include "i18n/strings.def"
#undef S
#undef P
};

enum class Plural {
#define S(id, sk, en)
#define P(id, sk1, sk2, sk5, en1, enN) id,
#include "i18n/strings.def"
#undef S
#undef P
};

// Set once, before any text is made.  Slovak until then, which is what the
// application spoke before it spoke anything else.
void SetLanguage(Lang lang);
Lang Language();

// "sk" and "en", as written in settings.txt.  False for anything else.
bool ParseLanguage(std::string_view code, Lang* lang);
const char* LanguageCode(Lang lang);

const wchar_t* Text(Str id);

// The string with {0}, {1}... replaced by the arguments in order.  A
// placeholder without its argument stays as it is, so a missing argument is
// heard rather than silently dropped.
std::wstring Format(Str id, std::initializer_list<std::wstring_view> args);

// The number in the form its language wants: Slovak has three (1 riadok,
// 2-4 riadky, anything else riadkov -- 0 included), English two.
std::wstring Count(Plural id, long long n);

// For proto/, which speaks UTF-8.  The catalog is the only input, so there
// is no malformed text to guard against.
std::string Utf8(std::wstring_view text);

}  // namespace i18n

#endif
