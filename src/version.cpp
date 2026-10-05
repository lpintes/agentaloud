#include "version.h"

namespace version {

std::optional<Number> Parse(std::wstring_view text) {
  if (text.starts_with(L'v')) text.remove_prefix(1);
  int parts[3] = {};
  for (int index = 0; index < 3; ++index) {
    if (index > 0) {
      if (!text.starts_with(L'.')) return std::nullopt;
      text.remove_prefix(1);
    }
    // Five digits are more than any part will ever need and few enough that
    // the int cannot overflow on a hostile tag.
    size_t digits = 0;
    int value = 0;
    while (digits < text.size() && text[digits] >= L'0' && text[digits] <= L'9') {
      if (digits == 5) return std::nullopt;
      value = value * 10 + (text[digits] - L'0');
      ++digits;
    }
    if (digits == 0) return std::nullopt;
    parts[index] = value;
    text.remove_prefix(digits);
  }
  if (!text.empty()) return std::nullopt;
  return Number{parts[0], parts[1], parts[2]};
}

}  // namespace version
