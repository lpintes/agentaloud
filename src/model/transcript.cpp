#include "model/transcript.h"

#include "model/utf.h"

namespace model {
namespace {

constexpr size_t kSummaryLimit = 100;
constexpr size_t kInlineResultLimit = 60;

std::wstring Widen(const std::string& text) { return Utf16FromUtf8(text); }

std::string StringField(const proto::Json& object, const char* name) {
  auto found = object.find(name);
  if (found == object.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

size_t CountLines(const std::wstring& text) {
  if (text.empty()) return 0;
  size_t lines = 1;
  for (wchar_t character : text) {
    if (character == L'\n') ++lines;
  }
  return lines;
}

// One line, no newlines, not longer than the limit.  Used for every summary,
// because a summary that wraps stops being a summary.
std::wstring OneLine(const std::wstring& text, size_t limit) {
  std::wstring flat;
  flat.reserve(text.size());
  bool space = false;
  for (wchar_t character : text) {
    if (character == L'\n' || character == L'\r' || character == L'\t') {
      space = true;
      continue;
    }
    if (space && !flat.empty()) flat.push_back(L' ');
    space = false;
    flat.push_back(character);
    if (flat.size() >= limit) {
      flat += L"...";
      return flat;
    }
  }
  return flat;
}

std::wstring Count(size_t lines) {
  // Slovak counts in three: 1 riadok, 2-4 riadky, 5+ riadkov.
  if (lines == 1) return L"1 riadok";
  const std::wstring number = std::to_wstring(lines);
  if (lines >= 2 && lines <= 4) return number + L" riadky";
  return number + L" riadkov";
}

// The field that says what a tool call actually does.  Falling back to the
// whole input would put a diff into a summary line.
std::wstring PrimaryInput(const std::string& toolName,
                          const proto::Json& input) {
  static const struct {
    const char* tool;
    const char* field;
  } kPrimary[] = {
      {"Bash", "command"},      {"PowerShell", "command"},
      {"Read", "file_path"},    {"Edit", "file_path"},
      {"Write", "file_path"},   {"NotebookEdit", "notebook_path"},
      {"Glob", "pattern"},      {"Grep", "pattern"},
      {"WebFetch", "url"},      {"Skill", "skill"},
  };
  for (const auto& entry : kPrimary) {
    if (toolName == entry.tool) {
      const std::string value = StringField(input, entry.field);
      if (!value.empty()) return Widen(value);
    }
  }
  const std::string description = StringField(input, "description");
  if (!description.empty()) return Widen(description);
  return Widen(input.dump());
}

std::wstring RenderToolInput(const proto::Json& input) {
  if (!input.is_object()) return Widen(input.dump(2));
  std::wstring body;
  for (auto it = input.begin(); it != input.end(); ++it) {
    if (!body.empty()) body.push_back(L'\n');
    body += Widen(it.key());
    body += L": ";
    body += it.value().is_string() ? Widen(it.value().get<std::string>())
                                   : Widen(it.value().dump());
  }
  return body;
}

// tool_result content is a string on the simple path and an array of blocks
// when the tool returned an image or several parts.
std::wstring RenderToolResult(const proto::Json& block) {
  auto content = block.find("content");
  if (content == block.end()) return {};
  if (content->is_string()) return Widen(content->get<std::string>());
  if (!content->is_array()) return Widen(content->dump());

  std::wstring body;
  for (const proto::Json& part : *content) {
    if (!part.is_object()) continue;
    const std::string type = StringField(part, "type");
    if (!body.empty()) body.push_back(L'\n');
    if (type == "text") {
      body += Widen(StringField(part, "text"));
    } else if (type == "image") {
      // Nothing useful can be done with it in a RichEdit; the view offers to
      // open it instead.  See claude-gui-lkk.5.
      body += L"[obrázok]";
    } else {
      body += Widen(part.dump());
    }
  }
  return body;
}

Block MakeThinking(const std::wstring& text) {
  Block block;
  block.kind = BlockKind::Thinking;
  block.body = text;
  block.summary = L"premýšľanie (" + Count(CountLines(text)) + L")";
  block.collapsed = true;
  return block;
}

Block MakeAssistantText(const std::wstring& text) {
  Block block;
  block.kind = BlockKind::AssistantText;
  block.body = text;
  block.summary = OneLine(text, kSummaryLimit);
  return block;
}

Block MakeToolUse(const proto::Json& blockJson) {
  const std::string name = StringField(blockJson, "name");
  auto input = blockJson.find("input");
  const proto::Json empty = proto::Json::object();
  const proto::Json& arguments = input != blockJson.end() ? *input : empty;

  Block block;
  block.kind = BlockKind::ToolUse;
  block.toolUseId = StringField(blockJson, "id");
  block.body = RenderToolInput(arguments);
  block.summary =
      Widen(name) + L": " + OneLine(PrimaryInput(name, arguments), kSummaryLimit);
  block.collapsed = true;
  return block;
}

Block MakeToolResult(const proto::Json& blockJson) {
  Block block;
  block.kind = BlockKind::ToolResult;
  block.toolUseId = StringField(blockJson, "tool_use_id");
  block.isError = blockJson.value("is_error", false);
  block.body = RenderToolResult(blockJson);
  block.collapsed = true;

  const std::wstring label = block.isError ? L"chyba" : L"výstup";
  const size_t lines = CountLines(block.body);
  if (lines <= 1 && block.body.size() <= kInlineResultLimit) {
    // A one-line result is shorter than the summary describing it, so it is
    // the summary.
    block.summary = label + (block.body.empty() ? L" (prázdny)"
                                                : L": " + block.body);
  } else {
    block.summary = label + L" (" + Count(lines) + L")";
  }
  return block;
}

}  // namespace

bool IsMechanism(BlockKind kind) {
  switch (kind) {
    case BlockKind::Thinking:
    case BlockKind::ToolUse:
    case BlockKind::ToolResult:
      return true;
    case BlockKind::UserPrompt:
    case BlockKind::AssistantText:
    case BlockKind::PermissionDenied:
      return false;
  }
  return false;
}

const wchar_t* KindLabel(BlockKind kind) {
  switch (kind) {
    case BlockKind::UserPrompt: return L"prompt";
    case BlockKind::AssistantText: return L"odpoveď";
    case BlockKind::PermissionDenied: return L"zamietnuté";
    case BlockKind::Thinking: return L"premýšľanie";
    case BlockKind::ToolUse: return L"nástroj";
    case BlockKind::ToolResult: return L"výstup";
  }
  return L"?";
}

std::wstring Transcript::Render(const Block& block) const {
  return (block.collapsed ? block.summary : block.body) + L'\n';
}

Edit Transcript::AppendBlocks(std::vector<Block> blocks) {
  Edit edit;
  edit.start = text_.size();
  if (blocks.empty()) return edit;

  for (Block& block : blocks) {
    const std::wstring rendered = Render(block);
    block.start = text_.size() + edit.inserted.size();
    block.length = rendered.size();
    edit.inserted += rendered;
    blocks_.push_back(std::move(block));
  }
  text_ += edit.inserted;
  return edit;
}

Edit Transcript::AppendUserPrompt(const std::wstring& text) {
  Block block;
  block.kind = BlockKind::UserPrompt;
  block.body = text;
  block.summary = OneLine(text, kSummaryLimit);
  return AppendBlocks({std::move(block)});
}

void Transcript::NoteUnknown(const proto::Event& event) {
  ++unknownCount_;
  const std::string type = event.raw.value("type", std::string("<no type>"));
  for (const std::string& seen : unknownTypes_) {
    if (seen == type) return;
  }
  unknownTypes_.push_back(type);
}

Edit Transcript::Append(const proto::Event& event) {
  std::vector<Block> made;

  switch (event.kind) {
    case proto::EventKind::Assistant: {
      auto message = event.raw.find("message");
      if (message == event.raw.end()) break;
      auto content = message->find("content");
      if (content == message->end() || !content->is_array()) break;
      for (const proto::Json& item : *content) {
        if (!item.is_object()) continue;
        const std::string type = StringField(item, "type");
        if (type == "thinking") {
          made.push_back(MakeThinking(Widen(StringField(item, "thinking"))));
        } else if (type == "text") {
          made.push_back(MakeAssistantText(Widen(StringField(item, "text"))));
        } else if (type == "tool_use") {
          made.push_back(MakeToolUse(item));
        }
      }
      break;
    }
    case proto::EventKind::User: {
      // Only tool results.  Our own prompts are added by AppendUserPrompt, and
      // the CLI's synthetic nudges ("your previous response had no visible
      // output") are machinery the user did not write and should not read.
      auto message = event.raw.find("message");
      if (message == event.raw.end()) break;
      auto content = message->find("content");
      if (content == message->end() || !content->is_array()) break;
      for (const proto::Json& item : *content) {
        if (!item.is_object()) continue;
        if (StringField(item, "type") == "tool_result") {
          made.push_back(MakeToolResult(item));
        }
      }
      break;
    }
    case proto::EventKind::SystemPermissionDenied: {
      Block block;
      block.kind = BlockKind::PermissionDenied;
      const std::wstring message = Widen(StringField(event.raw, "message"));
      const std::wstring tool = Widen(StringField(event.raw, "tool_name"));
      block.body = message.empty() ? L"Nástroj " + tool + L" bol zamietnutý."
                                   : message;
      block.summary = OneLine(block.body, kSummaryLimit);
      made.push_back(std::move(block));
      break;
    }
    case proto::EventKind::Unknown:
      NoteUnknown(event);
      break;
    // Deliberately not in the transcript: they belong in the status bar, which
    // a screen reader reads on request and not on every change.  That is the
    // whole reason this application exists.
    case proto::EventKind::SystemInit:
    case proto::EventKind::SystemHook:
    case proto::EventKind::SystemOther:
    case proto::EventKind::RateLimit:
    case proto::EventKind::Result:
    case proto::EventKind::ControlRequest:
    case proto::EventKind::ControlResponse:
      break;
  }

  return AppendBlocks(std::move(made));
}

Edit Transcript::SetCollapsed(size_t index, bool collapsed) {
  Edit edit;
  if (index >= blocks_.size()) return edit;
  Block& block = blocks_[index];
  if (block.collapsed == collapsed) return edit;

  block.collapsed = collapsed;
  const std::wstring rendered = Render(block);
  edit.start = block.start;
  edit.removed = block.length;
  edit.inserted = rendered;

  text_.replace(block.start, block.length, rendered);
  const size_t oldLength = block.length;
  block.length = rendered.size();

  // Everything after it moves by the difference.  Signed arithmetic on
  // purpose: collapsing shrinks the buffer and unsigned would wrap.
  const ptrdiff_t shift =
      static_cast<ptrdiff_t>(rendered.size()) - static_cast<ptrdiff_t>(oldLength);
  for (size_t i = index + 1; i < blocks_.size(); ++i) {
    blocks_[i].start = static_cast<size_t>(
        static_cast<ptrdiff_t>(blocks_[i].start) + shift);
  }
  return edit;
}

std::optional<size_t> Transcript::BlockAt(size_t offset) const {
  if (blocks_.empty()) return std::nullopt;
  if (offset >= text_.size()) return blocks_.size() - 1;
  for (size_t i = 0; i < blocks_.size(); ++i) {
    if (offset < blocks_[i].start + blocks_[i].length) return i;
  }
  return blocks_.size() - 1;
}

std::optional<size_t> Transcript::NextOfKind(size_t offset,
                                             BlockKind kind) const {
  const std::optional<size_t> here = BlockAt(offset);
  const size_t from = here.has_value() ? *here + 1 : 0;
  for (size_t i = from; i < blocks_.size(); ++i) {
    if (blocks_[i].kind == kind) return i;
  }
  return std::nullopt;
}

std::optional<size_t> Transcript::PreviousOfKind(size_t offset,
                                                 BlockKind kind) const {
  const std::optional<size_t> here = BlockAt(offset);
  if (!here.has_value() || *here == 0) return std::nullopt;
  for (size_t i = *here; i-- > 0;) {
    if (blocks_[i].kind == kind) return i;
  }
  return std::nullopt;
}

bool Transcript::CheckInvariants(std::string* problem) const {
  size_t expected = 0;
  for (size_t i = 0; i < blocks_.size(); ++i) {
    const Block& block = blocks_[i];
    if (block.start != expected) {
      if (problem) {
        *problem = "blok " + std::to_string(i) + " zacina na " +
                   std::to_string(block.start) + ", cakalo sa " +
                   std::to_string(expected);
      }
      return false;
    }
    if (block.length == 0) {
      if (problem) *problem = "blok " + std::to_string(i) + " ma nulovu dlzku";
      return false;
    }
    expected += block.length;
  }
  if (expected != text_.size()) {
    if (problem) {
      *problem = "sucet dlzok blokov je " + std::to_string(expected) +
                 ", buffer ma " + std::to_string(text_.size());
    }
    return false;
  }
  return true;
}

}  // namespace model
