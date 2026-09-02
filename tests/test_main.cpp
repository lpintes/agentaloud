// Testy pre model/ a proto/.  Vlastny maly runner, ziadny Catch2 -- pri
// pocte testov, ktore tu su, by kniznica pridala zavislost za pohodlie.
//
// Tri druhy testov a kazdy ma iny ucel; pletenie dokopy je to, co robi
// testy neuzitocnymi:
//
//   1. ARITMETIKA (synteticke vstupy).  Overuje moje vlastne pocty --
//      mapu rozsahov, posuny pri zbaleni, navigaciu.  Vstup si tu vymysliet
//      SMIEM, lebo sa netestuje format, ale scitanie.
//
//   2. FIXTURY (tests/fixtures/*.jsonl).  Zachyteny realny stream, zbaveny
//      volatilnych poli, zafixovany v repozitari.  Vstup si tu vymysliet
//      NESMIEM: rucne napisana fixtura by testovala moju predstavu o formate.
//      Vyroba: python tools/make_fixtures.py
//
//   3. SOAK (sukromny korpus, mimo repozitara).  Netvrdi ocakavane hodnoty,
//      len invarianty, a hlasi neznama typy zaznamov.  Zapina sa premennou
//      CLAUDELENS_CORPUS a bezi rucne.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "model/transcript.h"
#include "model/utf.h"
#include "proto/events.h"
#include "proto/jsonl.h"

namespace {

int gChecks = 0;
int gFailures = 0;
const char* gCurrentTest = "";

void Fail(const char* file, int line, const std::string& message) {
  ++gFailures;
  std::printf("  ZLYHALO  %s\n    %s:%d  %s\n", gCurrentTest, file, line,
              message.c_str());
}

#define CHECK(condition)                                              \
  do {                                                                \
    ++gChecks;                                                        \
    if (!(condition)) Fail(__FILE__, __LINE__, #condition);           \
  } while (0)

#define CHECK_EQ(actual, expected)                                    \
  do {                                                                \
    ++gChecks;                                                        \
    if (!((actual) == (expected))) {                                  \
      Fail(__FILE__, __LINE__,                                        \
           std::string(#actual) + " != " + #expected);                \
    }                                                                 \
  } while (0)

#define TEST(name) gCurrentTest = name

// ---------------------------------------------------------------- pomocne

std::vector<proto::Json> ReadJsonl(const std::string& path, bool* ok) {
  std::vector<proto::Json> records;
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    *ok = false;
    return records;
  }
  *ok = true;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    proto::Json record;
    std::string error;
    if (proto::ParseLine(line, &record, &error)) records.push_back(record);
  }
  return records;
}

std::map<model::BlockKind, size_t> CountKinds(const model::Transcript& t) {
  std::map<model::BlockKind, size_t> counts;
  for (const model::Block& block : t.blocks()) ++counts[block.kind];
  return counts;
}

// Prehra cely zaznam a po KAZDOM zazname skontroluje invarianty -- rozbita
// mapa rozsahov sa tak pripne na konkretnu udalost, nie na koniec suboru.
bool Replay(const std::vector<proto::Json>& records, model::Transcript* into,
            std::string* problem) {
  for (const proto::Json& record : records) {
    into->Append(proto::Classify(record));
    if (!into->CheckInvariants(problem)) return false;
  }
  return true;
}

// ------------------------------------------------------------ 1. aritmetika

void TestUtfRoundTrip() {
  TEST("utf: tam a spat");
  const std::string cases[] = {
      "",
      "plain ascii",
      "prihláška, ďakujem, žltučký kôň",
      "\xF0\x9F\x94\x8A",              // U+1F50A, teda surogatovy par
      "pred\xFF" "po",                 // neplatny bajt -> U+FFFD
  };
  for (const std::string& text : cases) {
    const std::wstring wide = model::Utf16FromUtf8(text);
    const std::string back = model::Utf8FromUtf16(wide);
    if (text.find('\xFF') == std::string::npos) {
      CHECK_EQ(back, text);
    } else {
      // Neplatny bajt sa nahradi, takze rovnost neplati -- ale nesmie sa
      // stratit nic okolo neho.
      CHECK(back.find("pred") != std::string::npos);
      CHECK(back.find("po") != std::string::npos);
    }
  }
  CHECK_EQ(model::Utf16FromUtf8("\xF0\x9F\x94\x8A").size(), size_t{2});
}

void TestRangeMap() {
  TEST("transcript: mapa rozsahov pri zbaleni a rozbaleni");
  model::Transcript transcript;
  std::string problem;

  const model::Edit first = transcript.AppendUserPrompt(L"prvy prompt");
  CHECK_EQ(first.start, size_t{0});
  CHECK_EQ(first.removed, size_t{0});
  CHECK_EQ(first.inserted, std::wstring(L"prvy prompt\n"));

  proto::Json assistant = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "thinking", "thinking": "rozmyslam\nna dvoch riadkoch"},
      {"type": "text", "text": "odpoved"},
      {"type": "tool_use", "id": "toolu_1", "name": "Bash",
       "input": {"command": "echo ahoj"}}
    ]}
  })");
  transcript.Append(proto::Classify(assistant));
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(transcript.blocks().size(), size_t{4});

  // Mechanika je zbalena, obsah nie -- a nie podla dlzky.
  CHECK(transcript.blocks()[1].collapsed);   // thinking
  CHECK(!transcript.blocks()[2].collapsed);  // assistant text
  CHECK(transcript.blocks()[3].collapsed);   // tool_use

  const size_t lengthBefore = transcript.Text().size();
  const size_t startOfLast = transcript.blocks()[3].start;

  // Rozbalenie thinkingu posunie vsetko za nim presne o rozdiel.
  const model::Edit expand = transcript.SetCollapsed(1, false);
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(expand.start, transcript.blocks()[1].start);
  const ptrdiff_t shift = static_cast<ptrdiff_t>(expand.inserted.size()) -
                          static_cast<ptrdiff_t>(expand.removed);
  CHECK_EQ(transcript.Text().size(),
           static_cast<size_t>(static_cast<ptrdiff_t>(lengthBefore) + shift));
  CHECK_EQ(transcript.blocks()[3].start,
           static_cast<size_t>(static_cast<ptrdiff_t>(startOfLast) + shift));

  // A zbalenie spat vrati presne povodny stav.
  transcript.SetCollapsed(1, true);
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(transcript.Text().size(), lengthBefore);
  CHECK_EQ(transcript.blocks()[3].start, startOfLast);

  // Zbalenie uz zbaleneho nie je zmena, takze nesmie vyrobit ziadnu upravu.
  CHECK(transcript.SetCollapsed(1, true).empty());
}

void TestBlockAtAndNavigation() {
  TEST("transcript: BlockAt a navigacia po druhoch");
  model::Transcript transcript;
  transcript.AppendUserPrompt(L"prompt A");
  proto::Json assistant = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "text", "text": "odpoved A"},
      {"type": "tool_use", "id": "toolu_1", "name": "Read",
       "input": {"file_path": "a.txt"}}
    ]}
  })");
  transcript.Append(proto::Classify(assistant));
  transcript.AppendUserPrompt(L"prompt B");

  // Kazdy offset padne do nejakeho bloku a hranice sedia.
  for (size_t offset = 0; offset < transcript.Text().size(); ++offset) {
    const auto index = transcript.BlockAt(offset);
    CHECK(index.has_value());
    const model::Block& block = transcript.blocks()[*index];
    CHECK(offset >= block.start && offset < block.start + block.length);
  }

  const auto nextPrompt =
      transcript.NextOfKind(0, model::BlockKind::UserPrompt);
  CHECK(nextPrompt.has_value());
  CHECK_EQ(*nextPrompt, size_t{3});  // preskoci ten, v ktorom kurzor stoji

  const auto previousPrompt = transcript.PreviousOfKind(
      transcript.Text().size() - 1, model::BlockKind::UserPrompt);
  CHECK(previousPrompt.has_value());
  CHECK_EQ(*previousPrompt, size_t{0});

  // Ziadny dalsi nastroj za poslednym blokom.
  CHECK(!transcript
             .NextOfKind(transcript.Text().size() - 1,
                         model::BlockKind::ToolUse)
             .has_value());
}

void TestSummariesAreOneLine() {
  TEST("transcript: zhrnutie je vzdy jeden riadok");
  model::Transcript transcript;
  proto::Json assistant = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "tool_use", "id": "toolu_1", "name": "Bash",
       "input": {"command": "prvy riadok\ndruhy riadok\ntreti"}}
    ]}
  })");
  transcript.Append(proto::Classify(assistant));
  for (const model::Block& block : transcript.blocks()) {
    CHECK(block.summary.find(L'\n') == std::wstring::npos);
  }
}

// -------------------------------------------------------------- 2. fixtury

void TestFixtureBasic(const std::string& dir) {
  TEST("fixtura basic: vsetky druhy blokov, ziadny neznamy zaznam");
  bool ok = false;
  const auto records = ReadJsonl(dir + "/basic.jsonl", &ok);
  if (!ok) {
    Fail(__FILE__, __LINE__,
         "chyba " + dir + "/basic.jsonl -- spusti tools/make_fixtures.py");
    return;
  }
  CHECK(records.size() > 20);

  model::Transcript transcript;
  std::string problem;
  if (!Replay(records, &transcript, &problem)) {
    Fail(__FILE__, __LINE__, "invariant: " + problem);
    return;
  }

  const auto counts = CountKinds(transcript);
  CHECK(counts.count(model::BlockKind::AssistantText) > 0);
  CHECK(counts.count(model::BlockKind::Thinking) > 0);
  CHECK(counts.count(model::BlockKind::ToolUse) > 0);
  CHECK(counts.count(model::BlockKind::ToolResult) > 0);

  // Stream nema typ, o ktorom by sme nevedeli.  Toto je tá kontrola, ktora
  // ohlasi, ze Anthropic pridal zaznam -- ale az po pregenerovani fixtury,
  // preto k nej patri soak nizsie.
  if (transcript.unknownCount() != 0) {
    std::string types;
    for (const std::string& type : transcript.unknownTypes()) {
      types += " " + type;
    }
    Fail(__FILE__, __LINE__, "neznamе typy zaznamov:" + types);
  }

  // Kazde volanie nastroja ma svoj vysledok a naopak.
  std::set<std::string> used;
  std::set<std::string> resulted;
  for (const model::Block& block : transcript.blocks()) {
    if (block.kind == model::BlockKind::ToolUse) used.insert(block.toolUseId);
    if (block.kind == model::BlockKind::ToolResult) {
      resulted.insert(block.toolUseId);
    }
  }
  CHECK(!used.empty());
  CHECK_EQ(used, resulted);

  // Aspon jeden vysledok je chybovy -- fixtura o to schvalne ziada
  // (`cat neexistujuci-subor.txt`).
  bool sawError = false;
  for (const model::Block& block : transcript.blocks()) {
    if (block.kind == model::BlockKind::ToolResult && block.isError) {
      sawError = true;
    }
  }
  CHECK(sawError);
}

void TestFixtureDenied(const std::string& dir) {
  TEST("fixtura denied: zamietnutie pravidlom je vlastny blok");
  bool ok = false;
  const auto records = ReadJsonl(dir + "/denied.jsonl", &ok);
  if (!ok) {
    Fail(__FILE__, __LINE__,
         "chyba " + dir + "/denied.jsonl -- spusti tools/make_fixtures.py");
    return;
  }

  model::Transcript transcript;
  std::string problem;
  if (!Replay(records, &transcript, &problem)) {
    Fail(__FILE__, __LINE__, "invariant: " + problem);
    return;
  }

  const auto counts = CountKinds(transcript);
  CHECK(counts.count(model::BlockKind::PermissionDenied) > 0);
  for (const model::Block& block : transcript.blocks()) {
    if (block.kind == model::BlockKind::PermissionDenied) {
      // Obsah, nie mechanika: zamietnutie sa nikdy nezbaluje.
      CHECK(!block.collapsed);
      CHECK(!model::IsMechanism(block.kind));
    }
  }
}

// ----------------------------------------------------------------- 3. soak

// Zaznam session na disku NIE JE ten isty format ako stream: ma navyse
// attachment, queue-operation, atis-latch a last-prompt, a nema system/init,
// result ani rate_limit_event.  Spolocne su assistant a user, teda prave tie,
// z ktorych sa robia bloky.  Toto je zoznam typov, o ktorych vieme, ze do
// transkriptu nepatria -- cokolvek mimo neho je novinka a stoji za pozretie.
//
// Zoznam vznikol behom soaku nad 133 realnymi session (26 981 blokov), nie
// citanim dokumentacie -- polovicu z neho by som si nevymyslel.  Dva z nich
// stoja za pozornost v kroku 7: `mode` a `permission-mode` su zmeny stavu
// session, takze obnovenie session, ktore ich ignoruje, obnovi transkript
// spravne, ale rezim nie.
const char* const kKnownDiskOnlyTypes[] = {
    "attachment", "queue-operation", "atis-latch", "last-prompt",
    "summary", "file-history-snapshot", "system",
    "agent-name", "agent-setting", "ai-title", "artifact-autoreact-ledger",
    "artifact-comment-monitor", "bridge-session", "cost-state",
    "file-history-delta", "frame-link", "mode", "permission-mode",
};

void SoakOverCorpus(const std::string& root) {
  TEST("soak: sukromny korpus");
  std::printf("\n  soak nad %s\n", root.c_str());

  // Ziadny <filesystem>: staci nam zoznam suborov, ktory si necha dodat
  // volajuci cez CLAUDELENS_CORPUS_LIST, alebo ho vyrobi shell.
  std::ifstream list(root);
  if (!list) {
    Fail(__FILE__, __LINE__, "nedá sa otvorit zoznam suborov: " + root);
    return;
  }

  std::set<std::string> unknown;
  size_t files = 0;
  size_t blocks = 0;
  std::string path;
  while (std::getline(list, path)) {
    if (!path.empty() && path.back() == '\r') path.pop_back();
    if (path.empty()) continue;
    bool ok = false;
    const auto records = ReadJsonl(path, &ok);
    if (!ok) continue;
    ++files;

    model::Transcript transcript;
    std::string problem;
    if (!Replay(records, &transcript, &problem)) {
      Fail(__FILE__, __LINE__, path + ": " + problem);
      continue;
    }
    blocks += transcript.blocks().size();
    for (const std::string& type : transcript.unknownTypes()) {
      bool known = false;
      for (const char* candidate : kKnownDiskOnlyTypes) {
        if (type == candidate) known = true;
      }
      if (!known) unknown.insert(type);
    }
  }

  std::printf("  %zu suborov, %zu blokov\n", files, blocks);
  CHECK(files > 0);
  if (!unknown.empty()) {
    std::string types;
    for (const std::string& type : unknown) types += " " + type;
    Fail(__FILE__, __LINE__, "neocakavane typy zaznamov:" + types);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string fixtures = argc > 1 ? argv[1] : "tests/fixtures";

  TestUtfRoundTrip();
  TestRangeMap();
  TestBlockAtAndNavigation();
  TestSummariesAreOneLine();
  TestFixtureBasic(fixtures);
  TestFixtureDenied(fixtures);

  if (const char* corpus = std::getenv("CLAUDELENS_CORPUS")) {
    SoakOverCorpus(corpus);
  }

  std::printf("\n%d kontrol, %d zlyhani\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
