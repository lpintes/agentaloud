#include "i18n/i18n.h"

#include <atomic>

namespace i18n {
namespace {

// Atomic because proto/ makes text on the reader thread.  Written once at
// startup, so the ordering does not matter; the type only keeps that honest.
std::atomic<Lang> gLanguage{Lang::kSlovak};

struct Entry {
  const wchar_t* sk;
  const wchar_t* en;
};

struct PluralEntry {
  const wchar_t* sk[3];  // 1, 2-4, other (0 included)
  const wchar_t* en[2];  // 1, other
};

// The trailing empty entry keeps the array from being empty while the
// catalog is still being filled.
const Entry kStrings[] = {
#define S(id, sk, en) {sk, en},
#define P(id, sk1, sk2, sk5, en1, enN)
#include "i18n/strings.def"
#undef S
#undef P
    {nullptr, nullptr},
};

const PluralEntry kPlurals[] = {
#define S(id, sk, en)
#define P(id, sk1, sk2, sk5, en1, enN) {{sk1, sk2, sk5}, {en1, enN}},
#include "i18n/strings.def"
#undef S
#undef P
    {{nullptr, nullptr, nullptr}, {nullptr, nullptr}},
};

std::wstring Substitute(std::wstring_view pattern,
                        std::initializer_list<std::wstring_view> args) {
  std::wstring out;
  out.reserve(pattern.size());
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == L'{' && i + 2 < pattern.size() &&
        pattern[i + 1] >= L'0' && pattern[i + 1] <= L'9' &&
        pattern[i + 2] == L'}') {
      const size_t index = static_cast<size_t>(pattern[i + 1] - L'0');
      if (index < args.size()) {
        out += *(args.begin() + index);
        i += 2;
        continue;
      }
    }
    out.push_back(pattern[i]);
  }
  return out;
}

}  // namespace

void SetLanguage(Lang lang) { gLanguage.store(lang); }

Lang Language() { return gLanguage.load(); }

bool ParseLanguage(std::string_view code, Lang* lang) {
  if (code == "sk") {
    *lang = Lang::kSlovak;
    return true;
  }
  if (code == "en") {
    *lang = Lang::kEnglish;
    return true;
  }
  return false;
}

const char* LanguageCode(Lang lang) {
  return lang == Lang::kSlovak ? "sk" : "en";
}

const wchar_t* Text(Str id) {
  const Entry& entry = kStrings[static_cast<size_t>(id)];
  return Language() == Lang::kSlovak ? entry.sk : entry.en;
}

std::wstring Format(Str id, std::initializer_list<std::wstring_view> args) {
  return Substitute(Text(id), args);
}

std::wstring Count(Plural id, long long n) {
  const PluralEntry& entry = kPlurals[static_cast<size_t>(id)];
  const wchar_t* form;
  if (Language() == Lang::kSlovak) {
    form = n == 1 ? entry.sk[0] : (n >= 2 && n <= 4) ? entry.sk[1] : entry.sk[2];
  } else {
    form = n == 1 ? entry.en[0] : entry.en[1];
  }
  const std::wstring number = std::to_wstring(n);
  return Substitute(form, {number});
}

std::string Utf8(std::wstring_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char32_t code = text[i];
    if (code >= 0xD800 && code <= 0xDBFF && i + 1 < text.size()) {
      code = 0x10000 + ((code - 0xD800) << 10) + (text[i + 1] - 0xDC00);
      ++i;
    }
    if (code < 0x80) {
      out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (code >> 6)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (code >> 12)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (code >> 18)));
      out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }
  return out;
}

}  // namespace i18n
