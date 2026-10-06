// The lookup functions of one language, made from its file.  Included by
// i18n.cpp inside the language's namespace, with I18N_FILE naming the file:
//
//   namespace sk {
//   #define I18N_FILE "i18n/sk.def"
//   #include "i18n/language.inc"
//   #undef I18N_FILE
//   }
//
// A switch with no default, so that -Wswitch -- an error in i18n.cpp -- names
// every id the file leaves out; an id the file has and ids.def does not is an
// unknown enumerator, and one given twice a duplicate case.

const wchar_t* Text(Str id) {
  switch (id) {
#define S(id, text) \
  case Str::id:     \
    return text;
#define P(id, ...)
#include I18N_FILE
#undef S
#undef P
    case Str::kCount:
      break;
  }
  return L"";
}

Forms PluralForms(Plural id) {
  switch (id) {
#define S(id, text)
#define P(id, ...)                                         \
  case Plural::id: {                                       \
    static const wchar_t* const forms[] = {__VA_ARGS__};   \
    return {forms, sizeof(forms) / sizeof(forms[0])};      \
  }
#include I18N_FILE
#undef S
#undef P
    case Plural::kCount:
      break;
  }
  return {nullptr, 0};
}
