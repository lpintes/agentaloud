#include "proto/elicitation.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>

#include "i18n/i18n.h"

namespace proto {

namespace {

std::string StringField(const Json& object, const char* name) {
  if (!object.is_object()) return {};
  auto found = object.find(name);
  if (found == object.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

const Json& Field(const Json& object, const char* name) {
  static const Json null;
  if (!object.is_object()) return null;
  auto found = object.find(name);
  return found == object.end() ? null : *found;
}

std::optional<long long> CountField(const Json& object, const char* name) {
  const Json& value = Field(object, name);
  if (!value.is_number()) return std::nullopt;
  return value.get<long long>();
}

std::optional<double> NumberField(const Json& object, const char* name) {
  const Json& value = Field(object, name);
  if (!value.is_number()) return std::nullopt;
  return value.get<double>();
}

std::string ValueText(const Json& value) {
  if (value.is_string()) return value.get<std::string>();
  return value.dump();
}

// The three shapes a list of choices comes in: `enum` (with the legacy
// `enumNames` beside it for the labels), and `oneOf` or `anyOf` of
// {const, title}.
std::vector<agent::FormOption> ReadOptions(const Json& schema) {
  std::vector<agent::FormOption> options;
  const Json& values = Field(schema, "enum");
  if (values.is_array()) {
    const Json& names = Field(schema, "enumNames");
    for (size_t i = 0; i < values.size(); ++i) {
      agent::FormOption option;
      option.value = ValueText(values[i]);
      if (names.is_array() && i < names.size() && names[i].is_string()) {
        option.label = names[i].get<std::string>();
      }
      if (option.label.empty()) option.label = option.value;
      options.push_back(std::move(option));
    }
    return options;
  }
  for (const char* key : {"oneOf", "anyOf"}) {
    const Json& list = Field(schema, key);
    if (!list.is_array()) continue;
    for (const Json& each : list) {
      const Json& constant = Field(each, "const");
      if (constant.is_null()) continue;
      agent::FormOption option;
      option.value = ValueText(constant);
      option.label = StringField(each, "title");
      if (option.label.empty()) option.label = option.value;
      options.push_back(std::move(option));
    }
    return options;
  }
  return options;
}

bool HasOptions(const Json& schema) {
  return Field(schema, "enum").is_array() || Field(schema, "oneOf").is_array() ||
         Field(schema, "anyOf").is_array();
}

agent::FormField ReadField(const std::string& name, const Json& schema) {
  agent::FormField field;
  field.name = name;
  field.title = StringField(schema, "title");
  field.description = StringField(schema, "description");
  const std::string type = StringField(schema, "type");
  if (type == "array") {
    field.type = agent::FieldType::Choices;
    field.options = ReadOptions(Field(schema, "items"));
    field.minItems = CountField(schema, "minItems");
    field.maxItems = CountField(schema, "maxItems");
  } else if (HasOptions(schema)) {
    field.type = agent::FieldType::Choice;
    field.options = ReadOptions(schema);
  } else if (type == "number" || type == "integer") {
    field.type = type == "number" ? agent::FieldType::Number
                                  : agent::FieldType::Integer;
    field.minimum = NumberField(schema, "minimum");
    field.maximum = NumberField(schema, "maximum");
  } else if (type == "boolean") {
    field.type = agent::FieldType::Boolean;
  } else {
    field.type = agent::FieldType::Text;
    field.minLength = CountField(schema, "minLength");
    field.maxLength = CountField(schema, "maxLength");
    field.format = StringField(schema, "format");
  }
  const Json& initial = Field(schema, "default");
  if (initial.is_array()) {
    for (const Json& each : initial) field.defaults.push_back(ValueText(each));
  } else if (!initial.is_null()) {
    field.defaults.push_back(ValueText(initial));
  }
  return field;
}

// The whole text as a number, or nothing: "5x" is not five.
bool ParseInteger(const std::string& text, long long* out) {
  if (text.empty()) return false;
  errno = 0;
  char* end = nullptr;
  *out = std::strtoll(text.c_str(), &end, 10);
  return errno == 0 && *end == '\0';
}

bool ParseNumber(const std::string& text, double* out) {
  if (text.empty()) return false;
  errno = 0;
  char* end = nullptr;
  *out = std::strtod(text.c_str(), &end);
  return errno == 0 && *end == '\0';
}

Json ValueJson(const agent::FormField& field,
               const std::vector<std::string>& values) {
  const std::string& text = values.front();
  switch (field.type) {
    case agent::FieldType::Integer: {
      long long whole = 0;
      if (ParseInteger(text, &whole)) return whole;
      break;
    }
    case agent::FieldType::Number: {
      long long whole = 0;
      if (ParseInteger(text, &whole)) return whole;
      double number = 0;
      if (ParseNumber(text, &number)) return number;
      break;
    }
    case agent::FieldType::Boolean:
      if (text == "true") return true;
      if (text == "false") return false;
      break;
    case agent::FieldType::Choices:
      return values;
    case agent::FieldType::Text:
    case agent::FieldType::Choice:
      break;
  }
  return text;
}

// A value as the reader saw it in the dialog: the option's label, yes or no.
std::string ShownValue(const agent::FormField& field, const std::string& value) {
  if (field.type == agent::FieldType::Boolean) {
    if (value == "true") return i18n::Utf8(i18n::Str::kFormYes);
    if (value == "false") return i18n::Utf8(i18n::Str::kFormNo);
  }
  for (const agent::FormOption& option : field.options) {
    if (option.value == value) return option.label;
  }
  return value;
}

}  // namespace

std::vector<agent::FormField> ReadFormFields(const Json& schema) {
  std::vector<agent::FormField> fields;
  const Json& properties = Field(schema, "properties");
  if (!properties.is_object()) return fields;
  for (const auto& property : properties.items()) {
    fields.push_back(ReadField(property.key(), property.value()));
  }
  const Json& required = Field(schema, "required");
  if (required.is_array()) {
    for (agent::FormField& field : fields) {
      for (const Json& name : required) {
        if (name.is_string() && name.get<std::string>() == field.name) {
          field.required = true;
        }
      }
    }
  }
  std::stable_sort(fields.begin(), fields.end(),
                   [](const agent::FormField& a, const agent::FormField& b) {
                     if (a.required != b.required) return a.required;
                     return a.name < b.name;
                   });
  return fields;
}

Json FormContent(const std::vector<agent::FormField>& fields,
                 const std::vector<std::vector<std::string>>& values) {
  Json content = Json::object();
  for (size_t i = 0; i < fields.size() && i < values.size(); ++i) {
    if (values[i].empty()) continue;
    content[fields[i].name] = ValueJson(fields[i], values[i]);
  }
  return content;
}

agent::ToolCall FormCall(const agent::FormRequest& request,
                         const std::string& callId) {
  agent::ToolCall call;
  call.id = callId;
  call.name = request.server;
  call.primary = request.url.empty() ? request.message : request.url;
  call.fields.push_back({"message", request.message});
  if (!request.url.empty()) call.fields.push_back({"url", request.url});
  for (const agent::FormField& field : request.fields) {
    call.fields.push_back(
        {field.name, field.title.empty() ? field.description : field.title});
  }
  return call;
}

agent::ToolResult FormResult(const std::string& callId,
                             const agent::FormRequest& request,
                             const agent::FormAnswer& answer) {
  agent::ToolResult result;
  result.callId = callId;
  switch (answer.action) {
    case agent::FormAnswer::Action::Decline:
      result.text = i18n::Utf8(i18n::Str::kToolDeclined);
      result.isError = true;
      return result;
    case agent::FormAnswer::Action::Cancel:
      result.text = i18n::Utf8(i18n::Str::kNoAnswer);
      result.isError = true;
      return result;
    case agent::FormAnswer::Action::Accept:
      break;
  }
  if (!request.url.empty()) {
    result.text = i18n::Utf8(i18n::Str::kFormLinkOpened);
    return result;
  }
  for (size_t i = 0; i < request.fields.size() && i < answer.values.size();
       ++i) {
    if (answer.values[i].empty()) continue;
    const agent::FormField& field = request.fields[i];
    if (!result.text.empty()) result.text.push_back('\n');
    result.text += (field.title.empty() ? field.name : field.title) + ": ";
    for (size_t j = 0; j < answer.values[i].size(); ++j) {
      if (j != 0) result.text += ", ";
      result.text += ShownValue(field, answer.values[i][j]);
    }
  }
  if (result.text.empty()) result.text = i18n::Utf8(i18n::Str::kFormSentEmpty);
  return result;
}

}  // namespace proto
