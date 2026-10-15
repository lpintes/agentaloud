#include "model/links.h"

#include <algorithm>

namespace model {

namespace {

wchar_t Lower(wchar_t c) {
  return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

// Case-blind: a model writes "HTTPS://" now and then, and a browser takes it.
bool StartsWithAt(const std::wstring& text, size_t at, const wchar_t* prefix) {
  for (size_t i = 0; prefix[i]; ++i) {
    if (at + i >= text.size() || Lower(text[at + i]) != prefix[i]) return false;
  }
  return true;
}

bool IsWordChar(wchar_t c) {
  return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
         (c >= L'0' && c <= L'9');
}

// What cannot be in an address and so ends one.  The quotes of every kind
// because an address is quoted in prose, in JSON and in Python's repr alike;
// the backtick because markdown puts code in it.  Letters outside ASCII stay:
// a browser takes an address with diacritics in the path.
bool Stops(wchar_t c) {
  if (c <= L' ' || c == 0x7F || c == 0xA0) return true;
  switch (c) {
    case L'"': case L'\'': case L'`': case L'<': case L'>':
    case L'{': case L'}': case L'|': case L'\\':
    case 0x2018: case 0x2019: case 0x201A: case 0x201C: case 0x201D:
    case 0x201E: case 0x00AB: case 0x00BB:
      return true;
    default:
      return false;
  }
}

// What ends a sentence or a markdown emphasis.  An address may end in one,
// but "see https://example.com." means the page, not the page with a dot.
bool Trailing(wchar_t c) {
  switch (c) {
    case L'.': case L',': case L';': case L':': case L'!': case L'?':
    case L'*':
      return true;
    default:
      return false;
  }
}

// The end of the address starting at `start`, its scheme `schemeLength` long.
size_t EndOf(const std::wstring& text, size_t start, size_t schemeLength) {
  // Brackets are counted, not stopped at.  A closing one without its opening
  // one inside the address belongs to the text around it -- "(see
  // https://x.org)" and markdown's "[text](https://x.org)" -- but Wikipedia
  // has addresses with brackets in them, and those come in pairs.
  int parens = 0;
  int squares = 0;
  size_t end = start + schemeLength;
  for (; end < text.size(); ++end) {
    const wchar_t c = text[end];
    if (Stops(c)) break;
    if (c == L'(') ++parens;
    if (c == L')' && parens-- == 0) break;
    if (c == L'[') ++squares;
    if (c == L']' && squares-- == 0) break;
  }
  while (end > start + schemeLength && Trailing(text[end - 1])) --end;
  return end;
}

// The "title" of "[title](" right before an address starting at `start`, or
// empty.  Brackets inside the title are counted; a line break ends the search,
// because markdown does not carry a link's text over one.
std::wstring TitleBefore(const std::wstring& text, size_t start) {
  if (start < 3 || text[start - 1] != L'(' || text[start - 2] != L']') {
    return {};
  }
  const size_t close = start - 2;
  int depth = 0;
  for (size_t i = close; i-- > 0;) {
    const wchar_t c = text[i];
    if (c == L'\n') return {};
    if (c == L']') ++depth;
    if (c == L'[' && depth-- == 0) return text.substr(i + 1, close - i - 1);
  }
  return {};
}

}  // namespace

std::vector<Link> FindLinks(const std::wstring& text) {
  std::vector<Link> links;
  size_t at = 0;
  while (at < text.size()) {
    size_t schemeLength = 0;
    if (StartsWithAt(text, at, L"https://")) {
      schemeLength = 8;
    } else if (StartsWithAt(text, at, L"http://")) {
      schemeLength = 7;
    }
    // "xhttp://" is not an address that starts at the h.
    if (schemeLength == 0 || (at > 0 && IsWordChar(text[at - 1]))) {
      ++at;
      continue;
    }
    const size_t end = EndOf(text, at, schemeLength);
    // A scheme with nothing after it opens nothing.
    if (end > at + schemeLength) {
      links.push_back(
          {at, end - at, text.substr(at, end - at), TitleBefore(text, at)});
    }
    at = std::max(end, at + schemeLength);
  }
  return links;
}

std::optional<Link> LinkAt(const std::wstring& text, size_t offset) {
  for (const Link& link : FindLinks(text)) {
    if (offset >= link.start && offset < link.start + link.length) return link;
  }
  return std::nullopt;
}

std::vector<Link> DistinctLinks(const std::wstring& text) {
  std::vector<Link> distinct;
  for (const Link& link : FindLinks(text)) {
    auto same = std::find_if(
        distinct.begin(), distinct.end(),
        [&link](const Link& seen) { return seen.url == link.url; });
    if (same == distinct.end()) {
      distinct.push_back(link);
    } else if (same->title.empty()) {
      same->title = link.title;
    }
  }
  return distinct;
}

}  // namespace model
