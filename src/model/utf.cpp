#include "model/utf.h"

namespace model {
namespace {

constexpr char32_t kReplacement = 0xFFFD;

void AppendUtf16(std::wstring* out, char32_t code) {
  if (code < 0x10000) {
    out->push_back(static_cast<wchar_t>(code));
    return;
  }
  code -= 0x10000;
  out->push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
  out->push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
}

bool IsContinuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

}  // namespace

std::wstring Utf16FromUtf8(std::string_view text) {
  std::wstring out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    size_t extra = 0;
    char32_t code = 0;
    if (lead < 0x80) {
      code = lead;
    } else if ((lead & 0xE0) == 0xC0) {
      extra = 1;
      code = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
      extra = 2;
      code = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
      extra = 3;
      code = lead & 0x07;
    } else {
      AppendUtf16(&out, kReplacement);
      ++i;
      continue;
    }
    if (i + extra >= text.size()) {
      AppendUtf16(&out, kReplacement);
      break;
    }
    bool valid = true;
    for (size_t k = 1; k <= extra; ++k) {
      const unsigned char next = static_cast<unsigned char>(text[i + k]);
      if (!IsContinuation(next)) {
        valid = false;
        break;
      }
      code = (code << 6) | (next & 0x3F);
    }
    if (!valid || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
      AppendUtf16(&out, kReplacement);
      ++i;
      continue;
    }
    AppendUtf16(&out, code);
    i += extra + 1;
  }
  return out;
}

std::string Utf8FromUtf16(std::wstring_view text) {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    char32_t code = static_cast<char32_t>(
        static_cast<unsigned short>(text[i]));
    if (code >= 0xD800 && code <= 0xDBFF && i + 1 < text.size()) {
      const char32_t low =
          static_cast<char32_t>(static_cast<unsigned short>(text[i + 1]));
      if (low >= 0xDC00 && low <= 0xDFFF) {
        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
        ++i;
      } else {
        code = kReplacement;
      }
    } else if (code >= 0xD800 && code <= 0xDFFF) {
      code = kReplacement;
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

}  // namespace model
