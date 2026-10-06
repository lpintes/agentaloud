#include "i18n/i18n.h"

#include <atomic>

// A language file that leaves an id out is a hole the reader would hear as
// silence; language.inc's switches make it a warning, and this makes it stop
// the build -- locally too, not only in CI.
#pragma GCC diagnostic error "-Wswitch"

namespace i18n {
namespace {

// Atomic because proto/ makes text on the reader thread.  Written once at
// startup, so the ordering does not matter; the type only keeps that honest.
std::atomic<Lang> gLanguage{Lang::kSlovak};

struct Forms {
  const wchar_t* const* forms;
  size_t count;
};

namespace sk {
#define I18N_FILE "i18n/sk.def"
#include "i18n/language.inc"
#undef I18N_FILE

// 1 riadok, 2-4 riadky, anything else riadkov -- 0 and 22 included.
size_t PluralIndex(long long n) {
  if (n == 1) return 0;
  if (n >= 2 && n <= 4) return 1;
  return 2;
}
}  // namespace sk

namespace en {
#define I18N_FILE "i18n/en.def"
#include "i18n/language.inc"
#undef I18N_FILE

size_t PluralIndex(long long n) { return n == 1 ? 0 : 1; }
}  // namespace en

struct Entry {
  Lang lang;
  const char* code;
  const wchar_t* (*text)(Str);
  Forms (*plural)(Plural);
  size_t (*pluralIndex)(long long);
  size_t pluralForms;
};

// A new language is a line here, a value in Lang, a namespace above and its
// file.
const Entry kLanguages[] = {
    {Lang::kSlovak, "sk", sk::Text, sk::PluralForms, sk::PluralIndex, 3},
    {Lang::kEnglish, "en", en::Text, en::PluralForms, en::PluralIndex, 2},
};

// Looked up, not indexed, so that the order here and in Lang cannot drift.
const Entry& Of(Lang lang) {
  for (const Entry& language : kLanguages) {
    if (language.lang == lang) return language;
  }
  return kLanguages[0];
}

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

std::vector<Lang> Languages() {
  std::vector<Lang> all;
  for (const Entry& language : kLanguages) all.push_back(language.lang);
  return all;
}

bool ParseLanguage(std::string_view code, Lang* lang) {
  for (const Entry& language : kLanguages) {
    if (code == language.code) {
      *lang = language.lang;
      return true;
    }
  }
  return false;
}

const char* LanguageCode(Lang lang) { return Of(lang).code; }

const wchar_t* Text(Str id) { return Of(Language()).text(id); }

std::wstring Format(Str id, std::initializer_list<std::wstring_view> args) {
  return Substitute(Text(id), args);
}

std::wstring Count(Plural id, long long n) {
  const Entry& language = Of(Language());
  const Forms forms = language.plural(id);
  const std::wstring number = std::to_wstring(n);
  if (forms.count == 0) return number;
  // A file with too few forms is caught by a test; should one slip through,
  // the last form is closer to right than nothing.
  size_t index = language.pluralIndex(n);
  if (index >= forms.count) index = forms.count - 1;
  return Substitute(forms.forms[index], {number});
}

size_t PluralFormCount(Lang lang) { return Of(lang).pluralForms; }

size_t PluralFormsGiven(Lang lang, Plural id) { return Of(lang).plural(id).count; }

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

std::string Utf8(Str id) { return Utf8(Text(id)); }

}  // namespace i18n
