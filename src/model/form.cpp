#include "model/form.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "i18n/i18n.h"

namespace model {

namespace {

// The same reading proto::FormContent makes of the text, so that what passes
// here goes back as a number.  Leading space and what strtod takes beyond a
// JSON number -- inf, nan -- are refused: JSON has no word for them.
bool ReadNumber(const std::string& text, double* out) {
  if (text.empty() || std::isspace(static_cast<unsigned char>(text[0]))) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  *out = std::strtod(text.c_str(), &end);
  return errno == 0 && *end == '\0' && std::isfinite(*out);
}

bool ReadInteger(const std::string& text) {
  if (text.empty() || std::isspace(static_cast<unsigned char>(text[0]))) {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  std::strtoll(text.c_str(), &end, 10);
  return errno == 0 && *end == '\0';
}

// What the schema states as a bound, as the reader would type it: 120, 0.5.
std::string BoundText(double bound) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.15g", bound);
  return buffer;
}

// Characters, as the schema counts them, not UTF-8 bytes.
long long CharacterCount(const std::string& text) {
  long long count = 0;
  for (unsigned char byte : text) {
    if ((byte & 0xC0) != 0x80) ++count;
  }
  return count;
}

bool HasSpace(const std::string& text) {
  for (unsigned char c : text) {
    if (std::isspace(c)) return true;
  }
  return false;
}

// Enough to catch a typo, not RFC 5322: one @, something before it, and a
// dot in a domain that neither starts nor ends with it.
bool LooksLikeEmail(const std::string& text) {
  const size_t at = text.find('@');
  if (at == std::string::npos || at == 0 || HasSpace(text)) return false;
  if (text.find('@', at + 1) != std::string::npos) return false;
  const std::string domain = text.substr(at + 1);
  const size_t dot = domain.find('.');
  return dot != std::string::npos && dot != 0 && domain.back() != '.';
}

// A scheme (RFC 3986: a letter, then letters, digits, + - .), a colon and
// something after it.
bool LooksLikeUri(const std::string& text) {
  if (text.empty() || HasSpace(text) ||
      !std::isalpha(static_cast<unsigned char>(text[0]))) {
    return false;
  }
  const size_t colon = text.find(':');
  if (colon == std::string::npos || colon + 1 == text.size()) return false;
  for (size_t i = 1; i < colon; ++i) {
    const unsigned char c = text[i];
    if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') return false;
  }
  return true;
}

bool Digits(const std::string& text, size_t from, size_t count, int* out) {
  if (from + count > text.size()) return false;
  *out = 0;
  for (size_t i = from; i < from + count; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    *out = *out * 10 + (text[i] - '0');
  }
  return true;
}

// RFC 3339 full-date, YYYY-MM-DD, starting at text[0].
bool IsDate(const std::string& text) {
  int year = 0, month = 0, day = 0;
  if (text.size() < 10 || text[4] != '-' || text[7] != '-') return false;
  if (!Digits(text, 0, 4, &year) || !Digits(text, 5, 2, &month) ||
      !Digits(text, 8, 2, &day)) {
    return false;
  }
  static const int kDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12 || day < 1 || day > kDays[month - 1]) {
    return false;
  }
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return !(month == 2 && day == 29 && !leap);
}

// RFC 3339 date-time: a date, T, hh:mm:ss, an optional fraction, and Z or an
// offset.  The zone is required -- without it the time means nothing to a
// server elsewhere.
bool IsDateTime(const std::string& text) {
  if (text.size() < 20 || !IsDate(text.substr(0, 10))) return false;
  if (text[10] != 'T' && text[10] != 't') return false;
  int hour = 0, minute = 0, second = 0;
  if (!Digits(text, 11, 2, &hour) || text[13] != ':' ||
      !Digits(text, 14, 2, &minute) || text[16] != ':' ||
      !Digits(text, 17, 2, &second)) {
    return false;
  }
  if (hour > 23 || minute > 59 || second > 60) return false;
  size_t at = 19;
  if (at < text.size() && text[at] == '.') {
    size_t digits = 0;
    while (++at < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[at]))) {
      ++digits;
    }
    if (digits == 0) return false;
  }
  if (at == text.size()) return false;
  if (text[at] == 'Z' || text[at] == 'z') return at + 1 == text.size();
  if (text[at] != '+' && text[at] != '-') return false;
  int zoneHour = 0, zoneMinute = 0;
  return at + 6 == text.size() && Digits(text, at + 1, 2, &zoneHour) &&
         text[at + 3] == ':' && Digits(text, at + 4, 2, &zoneMinute) &&
         zoneHour <= 23 && zoneMinute <= 59;
}

bool IsOption(const agent::FormField& field, const std::string& value) {
  for (const agent::FormOption& option : field.options) {
    if (option.value == value) return true;
  }
  return false;
}

std::string CheckText(const agent::FormField& field, const std::string& text) {
  const long long length = CharacterCount(text);
  if (field.minLength && length < *field.minLength) {
    return i18n::Utf8(i18n::Str::kFormTooShort,
                      {std::to_string(*field.minLength)});
  }
  if (field.maxLength && length > *field.maxLength) {
    return i18n::Utf8(i18n::Str::kFormTooLong,
                      {std::to_string(*field.maxLength)});
  }
  if (field.format == "email" && !LooksLikeEmail(text)) {
    return i18n::Utf8(i18n::Str::kFormNotEmail);
  }
  if (field.format == "uri" && !LooksLikeUri(text)) {
    return i18n::Utf8(i18n::Str::kFormNotUri);
  }
  if (field.format == "date" && (text.size() != 10 || !IsDate(text))) {
    return i18n::Utf8(i18n::Str::kFormNotDate);
  }
  if (field.format == "date-time" && !IsDateTime(text)) {
    return i18n::Utf8(i18n::Str::kFormNotDateTime);
  }
  return {};
}

std::string CheckNumber(const agent::FormField& field,
                        const std::string& text) {
  double number = 0;
  if (!ReadNumber(text, &number)) {
    return i18n::Utf8(i18n::Str::kFormNotNumber);
  }
  if (field.type == agent::FieldType::Integer && !ReadInteger(text)) {
    return i18n::Utf8(i18n::Str::kFormNotInteger);
  }
  if (field.minimum && number < *field.minimum) {
    return i18n::Utf8(i18n::Str::kFormBelowMinimum,
                      {BoundText(*field.minimum)});
  }
  if (field.maximum && number > *field.maximum) {
    return i18n::Utf8(i18n::Str::kFormAboveMaximum,
                      {BoundText(*field.maximum)});
  }
  return {};
}

}  // namespace

std::string CheckFormField(const agent::FormField& field,
                           const std::vector<std::string>& values) {
  const bool blank =
      values.empty() || (values.size() == 1 && values.front().empty());
  if (blank) {
    return field.required ? i18n::Utf8(i18n::Str::kFormMissing)
                          : std::string();
  }
  switch (field.type) {
    case agent::FieldType::Text:
      return CheckText(field, values.front());
    case agent::FieldType::Number:
    case agent::FieldType::Integer:
      return CheckNumber(field, values.front());
    case agent::FieldType::Boolean:
      if (values.front() == "true" || values.front() == "false") return {};
      return i18n::Utf8(i18n::Str::kFormNotAnOption);
    case agent::FieldType::Choice:
      if (IsOption(field, values.front())) return {};
      return i18n::Utf8(i18n::Str::kFormNotAnOption);
    case agent::FieldType::Choices:
      break;
  }
  for (const std::string& value : values) {
    if (!IsOption(field, value)) {
      return i18n::Utf8(i18n::Str::kFormNotAnOption);
    }
  }
  const long long count = static_cast<long long>(values.size());
  if (field.minItems && count < *field.minItems) {
    return i18n::Utf8(i18n::Str::kFormTooFew,
                      {std::to_string(*field.minItems)});
  }
  if (field.maxItems && count > *field.maxItems) {
    return i18n::Utf8(i18n::Str::kFormTooMany,
                      {std::to_string(*field.maxItems)});
  }
  return {};
}

std::vector<std::string> FormFieldRules(const agent::FormField& field) {
  using i18n::Str;
  std::vector<std::string> rules;
  if (field.required) rules.push_back(i18n::Utf8(Str::kFormMissing));
  switch (field.type) {
    case agent::FieldType::Text:
      if (field.minLength && *field.minLength > 0) {
        rules.push_back(i18n::Utf8(Str::kFormTooShort,
                                   {std::to_string(*field.minLength)}));
      }
      if (field.maxLength) {
        rules.push_back(i18n::Utf8(Str::kFormTooLong,
                                   {std::to_string(*field.maxLength)}));
      }
      if (field.format == "email") rules.push_back(i18n::Utf8(Str::kFormNotEmail));
      if (field.format == "uri") rules.push_back(i18n::Utf8(Str::kFormNotUri));
      if (field.format == "date") rules.push_back(i18n::Utf8(Str::kFormNotDate));
      if (field.format == "date-time") {
        rules.push_back(i18n::Utf8(Str::kFormNotDateTime));
      }
      break;
    case agent::FieldType::Number:
    case agent::FieldType::Integer:
      rules.push_back(i18n::Utf8(field.type == agent::FieldType::Integer
                                     ? Str::kFormNotInteger
                                     : Str::kFormNotNumber));
      if (field.minimum) {
        rules.push_back(
            i18n::Utf8(Str::kFormBelowMinimum, {BoundText(*field.minimum)}));
      }
      if (field.maximum) {
        rules.push_back(
            i18n::Utf8(Str::kFormAboveMaximum, {BoundText(*field.maximum)}));
      }
      break;
    case agent::FieldType::Choices:
      if (field.minItems && *field.minItems > 0) {
        rules.push_back(i18n::Utf8(Str::kFormTooFew,
                                   {std::to_string(*field.minItems)}));
      }
      if (field.maxItems) {
        rules.push_back(i18n::Utf8(Str::kFormTooMany,
                                   {std::to_string(*field.maxItems)}));
      }
      break;
    case agent::FieldType::Boolean:
    case agent::FieldType::Choice:
      break;
  }
  return rules;
}

}  // namespace model
