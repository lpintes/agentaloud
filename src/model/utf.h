#ifndef MODEL_UTF_H
#define MODEL_UTF_H

// UTF-8 to UTF-16 and back, written out rather than called through
// MultiByteToWideChar so that model/ does not have to include windows.h.
// The point of keeping model/ free of it is that the transcript can then be
// tested without a window, which is most of what makes the tests worth having.
//
// Lone surrogates and malformed sequences become U+FFFD rather than an error:
// a transcript that refuses to render because one tool printed a broken byte
// is worse than one that shows a replacement character.

#include <string>
#include <string_view>

namespace model {

std::wstring Utf16FromUtf8(std::string_view text);
std::string Utf8FromUtf16(std::wstring_view text);

}  // namespace model

#endif
