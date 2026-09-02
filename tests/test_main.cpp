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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "model/bookmarks.h"
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
  CHECK_EQ(first.inserted, std::wstring(L"you: prvy prompt\n"));

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

void TestErrorNavigationAndFirstLine() {
  TEST("transcript: klavesa ! a riadok, na ktorom kurzor pristane");
  model::Transcript transcript;
  transcript.AppendUserPrompt(L"prompt");
  // Vysledok nastroja s is_error: chyba, ktora nie je vlastnym druhom bloku.
  proto::Json failed = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_1", "is_error": true,
       "content": "prvy riadok\ndruhy riadok\ntreti riadok"}
    ]}
  })");
  transcript.Append(proto::Classify(failed));
  proto::Json denied = proto::Json::parse(R"({
    "type": "system", "subtype": "permission_denied",
    "tool_name": "Bash", "tool_input": {"command": "rm -rf /"}
  })");
  transcript.Append(proto::Classify(denied));

  // Predikat, ktory pouziva ui::SessionPane::Navigate pre '!'.
  const model::Transcript::BlockPredicate trouble =
      [](const model::Block& block) {
        return block.isError ||
               block.kind == model::BlockKind::PermissionDenied;
      };
  CHECK_EQ(transcript.blocks().size(), size_t{3});
  const auto firstTrouble = transcript.NextWhere(0, trouble);
  CHECK(firstTrouble.has_value());
  CHECK_EQ(*firstTrouble, size_t{1});  // chybny vysledok, nie zamietnutie
  // Zamietnutie je druha chyba, hoci nema is_error -- prave preto je to
  // predikat a nie druh bloku.
  const auto secondTrouble =
      transcript.NextWhere(transcript.blocks()[1].start, trouble);
  CHECK(secondTrouble.has_value());
  CHECK_EQ(*secondTrouble, size_t{2});
  const auto lastTrouble =
      transcript.PreviousWhere(transcript.Text().size() - 1, trouble);
  CHECK(lastTrouble.has_value());
  CHECK_EQ(*lastTrouble, size_t{1});

  // FirstLine je presne to, co je v bufferi po zaciatok prveho zlomu -- to,
  // co by citatel pocul, keby na ten riadok prisiel sipkou sam.
  for (size_t i = 0; i < transcript.blocks().size(); ++i) {
    const std::wstring line = transcript.FirstLine(i);
    CHECK(line.find(L'\n') == std::wstring::npos);
    CHECK_EQ(transcript.Text().substr(transcript.blocks()[i].start,
                                      line.size()),
             line);
  }
  CHECK_EQ(transcript.FirstLine(0), std::wstring(L"you: prompt"));

  // Rozbalenim sa riadok zmeni, a FirstLine to musi ukazat -- inak by skok
  // ohlasil zbaleny tvar bloku, ktory je otvoreny.
  CHECK(transcript.FirstLine(1).find(L", rozbalené") == std::wstring::npos);
  transcript.SetCollapsed(1, false);
  CHECK(transcript.FirstLine(1).find(L", rozbalené") != std::wstring::npos);
}

void TestBookmarksSurviveCollapsing() {
  TEST("bookmarks: znacka prezije zbalenie bloku nad nou aj pod nou");
  model::Transcript transcript;
  transcript.AppendUserPrompt(L"prompt");
  proto::Json assistant = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "thinking", "thinking": "riadok jeden\nriadok dva\nriadok tri"},
      {"type": "text", "text": "odpoved na dvoch\nriadkoch"}
    ]}
  })");
  transcript.Append(proto::Classify(assistant));
  CHECK_EQ(transcript.blocks().size(), size_t{3});

  // Kurzor na druhom riadku odpovede, teda vnutri bloku a nie na jeho zaciatku.
  const size_t inside = transcript.blocks()[2].start +
                        transcript.FirstLine(2).size() + 1;
  const model::Mark mark = model::MarkAt(transcript, inside);
  CHECK(mark.set);
  CHECK_EQ(mark.block, size_t{2});
  const std::wstring line = transcript.LineAt(inside);
  CHECK_EQ(line, std::wstring(L"riadkoch"));

  // Rozbalenie premyslania nad nou posunie offsety o kus -- znacka na to nesmie
  // reagovat, lebo blok aj miesto v nom su tie iste.
  CHECK(!transcript.SetCollapsed(1, false).empty());
  const auto moved = model::OffsetOf(transcript, mark);
  CHECK(moved.has_value());
  CHECK(*moved != inside);  // offset sa posunul
  CHECK_EQ(transcript.LineAt(*moved), line);  // riadok je ten isty

  // A ked sa blok pod znackou zbali tak, ze do neho uz nesiaha, znacka ostane
  // v nom -- pristat o blok dalej by bolo horsie nez pristat na zlom riadku.
  const model::Mark deep = {true, 2, 10000};
  const auto clamped = model::OffsetOf(transcript, deep);
  CHECK(clamped.has_value());
  CHECK_EQ(model::MarkAt(transcript, *clamped).block, size_t{2});

  // Nenastaveny slot nema kam skocit.
  model::Bookmarks bookmarks;
  CHECK(!bookmarks.Get(5).set);
  CHECK(!model::OffsetOf(transcript, bookmarks.Get(5)).has_value());
  bookmarks.Set(5, mark);
  CHECK(bookmarks.Get(5).set);
  CHECK_EQ(bookmarks.Get(5).block, size_t{2});
}

void TestNewlinesAreOneCharacter() {
  TEST("transcript: kazdy zlom riadku je prave jeden znak");
  // RichEdit pocita odstavcovy zlom ako jeden znak.  Keby text s "\r\n"
  // zostal dvojznakovy, mapa rozsahov by sa rozisla s widgetom o jeden znak
  // na kazdy riadok vystupu nastroja -- a to konci kurzorom na zlom mieste.
  model::Transcript transcript;
  transcript.AppendUserPrompt(L"prvy\r\ndruhy\rtreti\nstvrty");
  const std::wstring& text = transcript.Text();
  CHECK(text.find(L'\r') == std::wstring::npos);
  CHECK_EQ(std::count(text.begin(), text.end(), L'\n'), ptrdiff_t{4});

  proto::Json assistant = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_1",
       "content": "riadok\r\nriadok\r\n"}
    ]}
  })");
  transcript.Append(proto::Classify(assistant));
  transcript.SetCollapsed(transcript.blocks().size() - 1, false);
  CHECK(transcript.Text().find(L'\r') == std::wstring::npos);

  std::string problem;
  CHECK(transcript.CheckInvariants(&problem));
}

void TestEmptyBlocksAreDropped() {
  TEST("transcript: prazdny text a thinking nerobia blok");
  // Videne v prvom behu GUI: prisiel blok "premýšľanie (0 riadkov)".
  model::Transcript transcript;
  proto::Json assistant = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "thinking", "thinking": ""},
      {"type": "text", "text": ""},
      {"type": "text", "text": "toto zostava"}
    ]}
  })");
  transcript.Append(proto::Classify(assistant));
  CHECK_EQ(transcript.blocks().size(), size_t{1});

  // Prazdny vysledok nastroja je naopak odpoved a zostava.
  proto::Json result = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_1", "content": ""}
    ]}
  })");
  transcript.Append(proto::Classify(result));
  CHECK_EQ(transcript.blocks().size(), size_t{2});

  std::string problem;
  CHECK(transcript.CheckInvariants(&problem));
}

void TestSpeakerPrefix() {
  TEST("transcript: obsahove bloky maju prefix hovoriaceho");
  // Bez neho splyva prompt s odpovedou do jedneho prudu riadkov a citajuci
  // nema z coho poznat, kde jeden koncil.  Prefix je len vo vykresleni --
  // body zostava cisty, lebo z neho cita rec.
  model::Transcript transcript;
  transcript.AppendUserPrompt(L"otazka");
  proto::Json assistant = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "text", "text": "odpoved"},
      {"type": "tool_use", "id": "toolu_1", "name": "Read",
       "input": {"file_path": "a.txt"}}
    ]}
  })");
  transcript.Append(proto::Classify(assistant));

  CHECK_EQ(transcript.Text().substr(0, 5), std::wstring(L"you: "));
  CHECK_EQ(transcript.blocks()[0].body, std::wstring(L"otazka"));

  const model::Block& answer = transcript.blocks()[1];
  CHECK_EQ(transcript.Text().substr(answer.start, 8),
           std::wstring(L"claude: "));
  CHECK_EQ(answer.body, std::wstring(L"odpoved"));

  // Mechanika sa hlasi svojou hlavickou, prefix by bol druhy popis toho isteho.
  const model::Block& tool = transcript.blocks()[2];
  CHECK_EQ(transcript.Text().substr(tool.start, 5), std::wstring(L"Read:"));

  // A mapa rozsahov musi sediet aj po zbaleni obsahoveho bloku -- prefix je
  // v oboch tvaroch, takze rozdiel dlzok je len rozdiel tela a zhrnutia.
  std::string problem;
  transcript.SetCollapsed(1, true);
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(transcript.Text().substr(transcript.blocks()[1].start, 8),
           std::wstring(L"claude: "));
  transcript.SetCollapsed(0, true);
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(transcript.Text().substr(0, 5), std::wstring(L"you: "));
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
  TestErrorNavigationAndFirstLine();
  TestBookmarksSurviveCollapsing();
  TestNewlinesAreOneCharacter();
  TestEmptyBlocksAreDropped();
  TestSpeakerPrefix();
  TestSummariesAreOneLine();
  TestFixtureBasic(fixtures);
  TestFixtureDenied(fixtures);

  if (const char* corpus = std::getenv("CLAUDELENS_CORPUS")) {
    SoakOverCorpus(corpus);
  }

  std::printf("\n%d kontrol, %d zlyhani\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
