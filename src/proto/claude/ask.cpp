#include "proto/claude/ask.h"

namespace proto {
namespace {

std::string StringField(const Json& object, const char* name) {
  auto found = object.find(name);
  if (found == object.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

}  // namespace

const char kAskUserQuestionTool[] = "AskUserQuestion";

bool ParseAskUserQuestion(const Json& input, std::vector<AskQuestion>* out) {
  if (!input.is_object()) return false;
  auto questions = input.find("questions");
  if (questions == input.end() || !questions->is_array()) return false;

  std::vector<AskQuestion> parsed;
  for (const Json& item : *questions) {
    if (!item.is_object()) continue;
    AskQuestion question;
    question.question = StringField(item, "question");
    if (question.question.empty()) continue;
    question.header = StringField(item, "header");
    auto multi = item.find("multiSelect");
    question.multiSelect = multi != item.end() && multi->is_boolean() &&
                           multi->get<bool>();
    auto options = item.find("options");
    if (options != item.end() && options->is_array()) {
      for (const Json& entry : *options) {
        if (!entry.is_object()) continue;
        AskOption option;
        option.label = StringField(entry, "label");
        if (option.label.empty()) continue;
        option.description = StringField(entry, "description");
        question.options.push_back(std::move(option));
      }
    }
    // A question with nothing to pick from is not one this dialog can put:
    // the free-text kinds the CLI knows about ("text", "number") are gated
    // behind a flag and have never been seen on this stream.  Refusing the
    // whole input is the honest answer -- half a dialog would silently drop
    // the question it could not draw.
    if (question.options.empty()) return false;
    parsed.push_back(std::move(question));
  }
  if (parsed.empty()) return false;
  *out = std::move(parsed);
  return true;
}

Json MakeAskAnswers(const Json& input, const std::vector<AskQuestion>& questions,
                    const std::vector<std::vector<std::string>>& chosen) {
  // A copy of the whole input and not a fresh object: updatedInput REPLACES
  // the tool's arguments, so anything dropped here is dropped from the call.
  Json updated = input.is_object() ? input : Json::object();
  Json answers = Json::object();
  for (size_t i = 0; i < questions.size() && i < chosen.size(); ++i) {
    if (chosen[i].empty()) continue;
    if (questions[i].multiSelect) {
      answers[questions[i].question] = chosen[i];
    } else {
      answers[questions[i].question] = chosen[i].front();
    }
  }
  updated["answers"] = std::move(answers);
  return updated;
}

}  // namespace proto
