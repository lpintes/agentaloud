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
//      Jedna z nich, thinking.jsonl, je ZAMRZNUTA -- make_fixtures.py ju
//      nepise a pregenerovat sa uz neda, lebo CLI prestalo posielat text
//      premyslania.  Viz TestFixtureThinking.
//
//      Jedna z nich, disk.jsonl, nie je stream: je to SUBOR SESSION, ktory
//      CLI vedie v ~/.claude/projects, teda ten format, z ktoreho sa obnovuje
//      historia po --resume.  Su to dva rozne formaty (viz kKnownDiskOnlyTypes
//      nizsie) a bez nej by tu cestu testoval len soak -- teda nikto, kto ho
//      nepusta.
//
//   3. SOAK (sukromny korpus, mimo repozitara).  Netvrdi ocakavane hodnoty,
//      len invarianty, a hlasi neznama typy zaznamov.  Zapina sa premennou
//      AGENTALOUD_CORPUS a bezi rucne.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "app_name.h"
#include "i18n/i18n.h"
#include "model/bookmarks.h"
#include "model/history.h"
#include "model/transcript.h"
#include "model/utf.h"
#include "proto/claude/ask.h"
#include "proto/claude/control.h"
#include "proto/claude/events.h"
#include "proto/jsonl.h"
#include "proto/claude/sessions.h"
#include "proto/claude/translate.h"
#include "proto/codex/translate.h"
#include "arguments.h"
#include "session_names.h"
#include "settings.h"
#include "update.h"
#include "version.h"

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
    into->Append(proto::TranslateRecord(record));
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

// Katalog textov.  Jazyk je globalny stav, takze test ho na konci vrati na
// slovencinu -- na nej stoja kontroly textu vo vsetkych testoch za nim.
void TestCatalog() {
  TEST("i18n: mnozne cislo, jazyk, UTF-8");
  i18n::SetLanguage(i18n::Lang::kSlovak);
  CHECK(i18n::Count(i18n::Plural::kLines, 0) == L"0 riadkov");
  CHECK(i18n::Count(i18n::Plural::kLines, 1) == L"1 riadok");
  CHECK(i18n::Count(i18n::Plural::kLines, 2) == L"2 riadky");
  CHECK(i18n::Count(i18n::Plural::kLines, 4) == L"4 riadky");
  CHECK(i18n::Count(i18n::Plural::kLines, 5) == L"5 riadkov");
  // Slovencina sa riadi celym cislom, nie poslednou cifrou: 22 riadkov.
  CHECK(i18n::Count(i18n::Plural::kLines, 22) == L"22 riadkov");

  i18n::SetLanguage(i18n::Lang::kEnglish);
  CHECK(i18n::Count(i18n::Plural::kLines, 0) == L"0 lines");
  CHECK(i18n::Count(i18n::Plural::kLines, 1) == L"1 line");
  CHECK(i18n::Count(i18n::Plural::kLines, 3) == L"3 lines");

  i18n::Lang lang = i18n::Lang::kSlovak;
  CHECK(i18n::ParseLanguage("en", &lang) && lang == i18n::Lang::kEnglish);
  CHECK(i18n::ParseLanguage("sk", &lang) && lang == i18n::Lang::kSlovak);
  CHECK(!i18n::ParseLanguage("SK", &lang));
  CHECK(!i18n::ParseLanguage("", &lang));
  CHECK_EQ(std::string(i18n::LanguageCode(i18n::Lang::kEnglish)), "en");

  // Pocet tvarov kazdeho cisla musi sediet s pravidlom jazyka.  Prekladac to
  // nevynuti: P je variadicke a tvar navyse alebo menej sa prelozi.
  for (const i18n::Lang lang : i18n::Languages()) {
    for (size_t id = 0; id < static_cast<size_t>(i18n::Plural::kCount); ++id) {
      CHECK_EQ(i18n::PluralFormsGiven(lang, static_cast<i18n::Plural>(id)),
               i18n::PluralFormCount(lang));
    }
  }

  // Katalog pre proto/ ide do UTF-8 vlastnym kodovacom; musi sa zhodovat
  // s tym, ktorym model/ cita protokol.
  const std::wstring wide = L"plánovanie — „x“ \U0001F50A";
  CHECK_EQ(i18n::Utf8(wide), model::Utf8FromUtf16(wide));

  // Nazov rezimu je aj kluc: F1 vyhadzuje rezim, ktoreho nazov uz vypisal,
  // takze default a manual (ten isty rezim pod dvoma slovami CLI) musia mat
  // rovnaky nazov v kazdom jazyku.  Adapter sklada nazvy v jazyku, ktory
  // plati v case volania.
  for (const i18n::Lang lang : i18n::Languages()) {
    i18n::SetLanguage(lang);
    const agent::Capabilities claude = proto::ClaudeCapabilities();
    const agent::Mode* normal = agent::FindMode(claude, "default");
    const agent::Mode* manual = agent::FindMode(claude, "manual");
    CHECK(normal != nullptr && manual != nullptr &&
          normal->label == manual->label);
  }
  i18n::SetLanguage(i18n::Lang::kEnglish);
  CHECK_EQ(agent::FindMode(proto::ClaudeCapabilities(), "plan")->label,
           std::string("planning"));
  CHECK_EQ(agent::FindMode(proto::codex::CodexCapabilities(), "read-only")
               ->label,
           std::string("read only"));

  i18n::SetLanguage(i18n::Lang::kSlovak);
}

void TestRangeMap() {
  TEST("transcript: mapa rozsahov pri zbaleni a rozbaleni");
  model::Transcript transcript;
  std::string problem;

  const model::Edit first = transcript.AppendUserPrompt(L"prvy prompt");
  CHECK_EQ(first.start, size_t{0});
  CHECK_EQ(first.removed, size_t{0});
  CHECK_EQ(first.inserted, std::wstring(L"ja: prvy prompt\n"));

  proto::Json assistant = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "thinking", "thinking": "rozmyslam\nna dvoch riadkoch"},
      {"type": "text", "text": "odpoved"},
      {"type": "tool_use", "id": "toolu_1", "name": "Bash",
       "input": {"command": "echo ahoj"}}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(assistant));
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
  transcript.Append(proto::TranslateRecord(assistant));
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

  // Dozadu z vnutra bloku sa ide na zaciatok toho bloku, nie za neho: kurzor
  // tam este nebol, takze je to pohyb dozadu. Preskocit ho by znamenalo, ze
  // posledny blok sa z konca prepisu neda dosiahnut vobec.
  const auto previousPrompt = transcript.PreviousOfKind(
      transcript.Text().size() - 1, model::BlockKind::UserPrompt);
  CHECK(previousPrompt.has_value());
  CHECK_EQ(*previousPrompt, size_t{3});
  // Az zo zaciatku toho isteho bloku sa ide dalej -- inak by klavesa vracala
  // donekonecna to iste miesto.
  const auto beforeThat = transcript.PreviousOfKind(
      transcript.blocks()[3].start, model::BlockKind::UserPrompt);
  CHECK(beforeThat.has_value());
  CHECK_EQ(*beforeThat, size_t{0});

  // Ctrl+End a potom 'A': kurzor stoji za vsetkym, takze predchadzajuca
  // odpoved je ta posledna, nie predposledna.
  const auto lastPrompt = transcript.PreviousOfKind(
      transcript.Text().size(), model::BlockKind::UserPrompt);
  CHECK(lastPrompt.has_value());
  CHECK_EQ(*lastPrompt, size_t{3});

  // Ziadny dalsi nastroj za poslednym blokom.
  CHECK(!transcript
             .NextOfKind(transcript.Text().size() - 1,
                         model::BlockKind::ToolUse)
             .has_value());
}

void TestControlCharactersNeverReachTheBuffer() {
  TEST("transcript: NUL a spol. sa do bufferu nedostanu");
  // Odmerane 6. 9. 2026 na skutocnej session: `grep -a` nad .exe vratil
  // vysledok, v ktorom bolo 19 NULov -- retazce v binarke su UTF-16LE, takze
  // kazdy druhy bajt je nula. Prepis ide do RichEditu cez EM_REPLACESEL, teda
  // ako retazec ukonceny nulou: widget vzal prvych 47 znakov z 502 a od toho
  // miesta prestala platit mapa rozsahov. Titulok to povedal, ale az potom,
  // co uz bola navigacia mimo.
  //
  // JSON escapy, nie surove bajty v zdrojaku: JSON riadiaci znak v retazci
  // nedovoli, takze surovy NUL by neprosiel uz parserom -- a v diffe ho nikto
  // neuvidi.
  model::Transcript transcript;
  proto::Json binary = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_bin",
       "content": "command-name\u0000$\u0000\u0000 function kge\u000b\u001c koniec"}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(binary));

  std::string problem;
  CHECK(transcript.CheckInvariants(&problem));
  const std::wstring& text = transcript.Text();
  bool clean = true;
  for (wchar_t character : text) {
    if (character == L'\n' || character == L'\t') continue;
    if (character < 0x20 || character == 0x7F) clean = false;
  }
  CHECK(clean);
  // Nezahodili sa: su vidiet, a teda aj pocut -- "\x00" precita citacka ako
  // 'spatna lomka x nula nula', samotny znak ako nic. Tichy vypadok znakov
  // z vystupu je ta ista trieda chyby ako to, co sa opravovalo.
  CHECK(text.find(L"\\x00") != std::wstring::npos);
  CHECK(text.find(L"\\x0b") != std::wstring::npos);
  CHECK(text.find(L"\\x1c") != std::wstring::npos);
  // A obsah okolo nich zostal.
  CHECK(text.find(L"command-name") != std::wstring::npos);
  CHECK(text.find(L"koniec") != std::wstring::npos);
  // A po prvom NULe uz text nekonci -- prave v tom bola ta chyba: retazec
  // ukonceny nulou je kratsi nez retazec, ktoreho dlzku drzi model.
  CHECK_EQ(std::wstring(text.c_str()).size(), text.size());

  // To iste pre prompt: do editacneho pola sa da vlozit text odkial-kolvek.
  // Znaky sa skladaju po jednom a nie escapmi, aby bolo v zdrojaku vidiet,
  // ktore to su.
  model::Transcript typed;
  std::wstring pasted = L"prvy";
  pasted.push_back(0x0D);  // osamotene CR: zlom riadku, nie znak na zahodenie
  pasted += L"druhy";
  pasted.push_back(0x0B);  // VT: pre RichEdit zlom riadku, pre model nie
  pasted += L" treti";
  typed.AppendUserPrompt(pasted);
  CHECK(typed.CheckInvariants(&problem));
  CHECK_EQ(typed.Text().find(static_cast<wchar_t>(0x0B)), std::wstring::npos);
  CHECK(typed.Text().find(L"\\x0b") != std::wstring::npos);
  // Poradie NormalizeNewlines a EscapeControls rozhoduje prave o tomto: keby
  // sa riadiace znaky vypisovali skor, osamotene CR by skoncilo ako "\x0d"
  // namiesto toho, aby sa stalo zlomom riadku.
  CHECK(typed.Text().find(L"prvy\ndruhy") != std::wstring::npos);
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
  transcript.Append(proto::TranslateRecord(failed));
  proto::Json denied = proto::Json::parse(R"({
    "type": "system", "subtype": "permission_denied",
    "tool_name": "Bash", "tool_input": {"command": "rm -rf /"}
  })");
  transcript.Append(proto::TranslateRecord(denied));

  // Predikat, ktory pouziva ui::SessionPane::Navigate pre 'e' / 'E'.
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
  // Z konca prepisu je najblizsia chyba dozadu to zamietnutie, na ktorom
  // kurzor stoji -- kurzor je za nim, nie na jeho zaciatku.
  const auto lastTrouble =
      transcript.PreviousWhere(transcript.Text().size(), trouble);
  CHECK(lastTrouble.has_value());
  CHECK_EQ(*lastTrouble, size_t{2});
  const auto troubleBefore =
      transcript.PreviousWhere(transcript.blocks()[2].start, trouble);
  CHECK(troubleBefore.has_value());
  CHECK_EQ(*troubleBefore, size_t{1});

  // FirstLine je presne to, co je v bufferi po zaciatok prveho zlomu -- to,
  // co by citatel pocul, keby na ten riadok prisiel sipkou sam.
  for (size_t i = 0; i < transcript.blocks().size(); ++i) {
    const std::wstring line = transcript.FirstLine(i);
    CHECK(line.find(L'\n') == std::wstring::npos);
    CHECK_EQ(transcript.Text().substr(transcript.blocks()[i].start,
                                      line.size()),
             line);
  }
  CHECK_EQ(transcript.FirstLine(0), std::wstring(L"ja: prompt"));

  // Rozbalenim sa riadok zmeni, a FirstLine to musi ukazat -- inak by skok
  // ohlasil zbaleny tvar bloku, ktory je otvoreny.
  CHECK(transcript.FirstLine(1).find(L", rozbalené") == std::wstring::npos);
  transcript.SetCollapsed(1, false);
  CHECK(transcript.FirstLine(1).find(L", rozbalené") != std::wstring::npos);
}

void TestInterruptLeavesAMark() {
  TEST("transcript: prerusenie zanecha stopu, ktoru najde navigacia na chybu");
  model::Transcript transcript;
  std::string problem;
  transcript.AppendUserPrompt(L"nieco dlhe");
  proto::Json partial = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [{"type": "text", "text": "zacal som odpoved"}]}
  })");
  transcript.Append(proto::TranslateRecord(partial));
  transcript.AppendInterrupted();
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(transcript.blocks().size(), size_t{3});

  const model::Block& mark = transcript.blocks()[2];
  CHECK(mark.kind == model::BlockKind::Interrupted);
  // Jednoriadkova; nie je za nou nic, co by sa dalo rozbalit.
  CHECK(!mark.collapsible);
  CHECK(mark.summary.find(L'\n') == std::wstring::npos);

  // Klavesa E ju musi najst -- prave tam sa praca zastavila.
  const model::Transcript::BlockPredicate trouble =
      [](const model::Block& block) {
        return block.isError ||
               block.kind == model::BlockKind::PermissionDenied;
      };
  const auto found = transcript.NextWhere(0, trouble);
  CHECK(found.has_value());
  CHECK_EQ(*found, size_t{2});

  // Ziadna predpona hovoriaceho: prerusenie nepovedal ani jeden z nich.
  CHECK_EQ(transcript.FirstLine(2), std::wstring(L"Prerušené používateľom."));
}

void TestSessionEndedKeepsStderr() {
  TEST("transcript: koniec CLI zanecha blok s jeho chybovym vystupom");
  model::Transcript transcript;
  std::string problem;
  transcript.AppendUserPrompt(L"ahoj");
  // Farba a CRLF, ako ich pise node na stderr; do bloku nesmie vojst ani
  // jedno (invarianty 4 a 8).
  transcript.Append({agent::SessionEnded{
      true, 1, "\x1b[31mError: Session ID x is already in use.\x1b[0m\r\n"}});
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(transcript.blocks().size(), size_t{2});
  const model::Block& ended = transcript.blocks()[1];
  CHECK(ended.kind == model::BlockKind::SessionEnded);
  CHECK(ended.isError);
  // Obsah, nie mechanizmus: neda sa zbalit, cita sa cely.
  CHECK(!ended.collapsible);
  CHECK(!ended.collapsed);
  CHECK_EQ(ended.summary, std::wstring(L"Agent skončil s kódom 1."));
  CHECK_EQ(ended.body, std::wstring(L"Agent skončil s kódom 1.\n"
                                    L"Error: Session ID x is already in use."));
  CHECK_EQ(transcript.FirstLine(1), std::wstring(L"Agent skončil s kódom 1."));

  // Pad: NTSTATUS sa cita len hexa.  Prazdny stderr sa povie, inak by blok
  // vyzeral useknuty.
  model::Transcript crashed;
  crashed.Append({agent::SessionEnded{true, 0xC0000005, ""}});
  CHECK_EQ(crashed.blocks().size(), size_t{1});
  CHECK_EQ(crashed.blocks()[0].body,
           std::wstring(L"Agent skončil s kódom 0xC0000005.\n"
                        L"Na chybový výstup nenapísal nič."));

  model::Transcript lingering;
  lingering.Append({agent::SessionEnded{false, 0, "x"}});
  CHECK_EQ(lingering.blocks()[0].summary, std::wstring(L"Agent skončil."));
}

void TestToolResultsSitBehindTheirCall() {
  TEST("transcript: vysledok stoji za svojim volanim, nie na konci");
  model::Transcript transcript;
  std::string problem;
  transcript.AppendUserPrompt(L"sprav dve veci naraz");

  // Dve volania v jednej sprave -- tak Claude spusta nastroje paralelne.
  proto::Json calls = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "tool_use", "id": "toolu_A", "name": "Bash",
       "input": {"command": "prve"}},
      {"type": "tool_use", "id": "toolu_B", "name": "Read",
       "input": {"file_path": "druhy.txt"}}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(calls));
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(transcript.blocks().size(), size_t{3});

  // A vysledky pridu v opacnom poradi, lebo druhy nastroj skoncil skor.
  proto::Json second = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_B",
       "content": "obsah suboru\nna dvoch riadkoch"}
    ]}
  })");
  const std::vector<model::Edit> edits =
      transcript.Append(proto::TranslateRecord(second));
  CHECK_EQ(edits.size(), size_t{1});
  CHECK(transcript.CheckInvariants(&problem));

  // Vlozilo sa DOPROSTRED: uprava nezacina na konci bufferu.
  CHECK(edits[0].start < transcript.Text().size() - edits[0].inserted.size() +
                             1);
  CHECK_EQ(edits[0].removed, size_t{0});

  proto::Json first = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_A", "content": "hotovo"}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(first));
  CHECK(transcript.CheckInvariants(&problem));

  // Ziadane poradie: prompt, volanie A, vysledok A, volanie B, vysledok B.
  const std::vector<model::Block>& blocks = transcript.blocks();
  CHECK_EQ(blocks.size(), size_t{5});
  CHECK_EQ(blocks[1].toolUseId, std::string("toolu_A"));
  CHECK_EQ(blocks[2].toolUseId, std::string("toolu_A"));
  CHECK(blocks[2].kind == model::BlockKind::ToolResult);
  CHECK_EQ(blocks[3].toolUseId, std::string("toolu_B"));
  CHECK_EQ(blocks[4].toolUseId, std::string("toolu_B"));
  CHECK(blocks[4].kind == model::BlockKind::ToolResult);

  // Id sa pridelili v poradi vzniku, teda NIE v poradi, v akom bloky stoja.
  // Prave to drzi zalozky a 'bloky tohto tahu' na mieste pri vkladani.
  CHECK(blocks[4].id < blocks[2].id);
  CHECK_EQ(transcript.IndexOfId(blocks[2].id).value(), size_t{2});
  CHECK_EQ(transcript.IndexOfId(blocks[4].id).value(), size_t{4});

  // Vysledok bez zodpovedajuceho volania nema kam patrit a ide na koniec.
  proto::Json orphan = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_NEEXISTUJE",
       "content": "sirota"}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(orphan));
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(transcript.blocks().size(), size_t{6});
  CHECK_EQ(transcript.blocks()[5].toolUseId,
           std::string("toolu_NEEXISTUJE"));
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
  transcript.Append(proto::TranslateRecord(assistant));
  CHECK_EQ(transcript.blocks().size(), size_t{3});

  // Kurzor na druhom riadku odpovede, teda vnutri bloku a nie na jeho zaciatku.
  const size_t inside = transcript.blocks()[2].start +
                        transcript.FirstLine(2).size() + 1;
  const model::Mark mark = model::MarkAt(transcript, inside);
  CHECK(mark.set);
  CHECK_EQ(mark.blockId, transcript.blocks()[2].id);
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
  const model::Mark deep = {true, transcript.blocks()[2].id, 10000};
  const auto clamped = model::OffsetOf(transcript, deep);
  CHECK(clamped.has_value());
  CHECK_EQ(model::MarkAt(transcript, *clamped).blockId,
           transcript.blocks()[2].id);

  // Nenastaveny slot nema kam skocit.
  model::Bookmarks bookmarks;
  CHECK(!bookmarks.Get(5).set);
  CHECK(!model::OffsetOf(transcript, bookmarks.Get(5)).has_value());
  bookmarks.Set(5, mark);
  CHECK(bookmarks.Get(5).set);
  CHECK_EQ(bookmarks.Get(5).blockId, transcript.blocks()[2].id);
}

void TestBookmarkIntoCollapsedBlock() {
  TEST("bookmarks: znacka vo vnutri zbaleneho bloku ho rozbali (lkk.5.30)");
  model::Transcript transcript;
  transcript.AppendUserPrompt(L"prompt");
  proto::Json assistant = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "thinking", "thinking": "riadok jeden\nriadok dva\nriadok tri"}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(assistant));
  CHECK_EQ(transcript.blocks().size(), size_t{2});
  CHECK(transcript.blocks()[1].collapsed);
  std::string problem;

  // Rozbaleny blok, znacka na riadku "riadok dva", potom sa blok zbali.
  transcript.SetCollapsed(1, false);
  const std::wstring& text = transcript.Text();
  const size_t start = transcript.blocks()[1].start;
  const size_t two = text.find(L"riadok dva", start);
  const model::Mark deep = model::MarkAt(transcript, two);
  CHECK(!deep.collapsed);
  const model::Mark heading = model::MarkAt(transcript, start + 2);
  transcript.SetCollapsed(1, true);

  // Hlboka znacka chce blok rozbalit a po rozbaleni pristane na svojom riadku.
  const auto expand = model::BlockToExpand(transcript, deep);
  CHECK(expand.has_value());
  CHECK_EQ(*expand, size_t{1});
  // Bez rozbalenia nie je kam inam nez na zaciatok bloku.
  CHECK_EQ(*model::OffsetOf(transcript, deep), start);
  transcript.SetCollapsed(1, false);
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(transcript.LineAt(*model::OffsetOf(transcript, deep)),
           std::wstring(L"riadok dva"));
  CHECK(!model::BlockToExpand(transcript, deep).has_value());

  // Znacka na nadpise rozbaleneho bloku nic nerozbaluje: prvy riadok je
  // suhrn v oboch tvaroch.
  transcript.SetCollapsed(1, true);
  CHECK(!model::BlockToExpand(transcript, heading).has_value());
  CHECK_EQ(*model::OffsetOf(transcript, heading), start);

  // Znacka na zbalenom suhrne po rozbaleni pristane na nadpise, nie o kus
  // nizsie -- jej odstup bol merany v inom texte.
  const model::Mark onSummary = model::MarkAt(transcript, start + 5);
  CHECK(onSummary.collapsed);
  transcript.SetCollapsed(1, false);
  CHECK(!model::BlockToExpand(transcript, onSummary).has_value());
  CHECK_EQ(*model::OffsetOf(transcript, onSummary), start);
}

void TestFindSearchesCollapsedBlocks() {
  TEST("transcript: hladanie vidi aj do zbaleneho bloku a pretoci sa");
  model::Transcript transcript;
  transcript.AppendUserPrompt(L"prompt");
  proto::Json assistant = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "thinking", "thinking": "riadok jeden\nIHLA v kope\nriadok tri"},
      {"type": "text", "text": "odpoved bez nej\na ihla na konci"}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(assistant));
  CHECK_EQ(transcript.blocks().size(), size_t{3});
  CHECK(transcript.blocks()[1].collapsed);

  // Model nesmie tahat windows.h, takze test sklada velkost pismen sam.
  const model::Transcript::Fold fold = [](const std::wstring& text) {
    std::wstring folded = text;
    for (wchar_t& c : folded) c = static_cast<wchar_t>(std::towlower(c));
    return folded;
  };
  auto offsetOf = [&transcript](const model::SearchHit& hit) {
    return transcript.blocks()[hit.index].start + hit.offset;
  };

  // Vyskyt je za zbalenym riadkom: najde sa a po rozbaleni sedi offset.
  auto hit = transcript.Find(L"ihla", 0, false, fold);
  CHECK(hit.has_value());
  CHECK_EQ(hit->index, size_t{1});
  CHECK(!hit->wrapped);
  std::string problem;
  CHECK(!transcript.SetCollapsed(1, false).empty());
  CHECK(transcript.CheckInvariants(&problem));
  const size_t first = offsetOf(*hit);
  CHECK_EQ(transcript.LineAt(first), std::wstring(L"IHLA v kope"));

  // Dalej od nalezu, nie od zaciatku bloku.
  hit = transcript.Find(L"ihla", first, false, fold);
  CHECK(hit.has_value());
  CHECK_EQ(hit->index, size_t{2});
  CHECK(!hit->wrapped);
  const size_t second = offsetOf(*hit);
  CHECK_EQ(transcript.LineAt(second), std::wstring(L"a ihla na konci"));

  // Za poslednym sa pretoci a musi to povedat.
  hit = transcript.Find(L"ihla", second, false, fold);
  CHECK(hit.has_value());
  CHECK(hit->wrapped);
  CHECK_EQ(offsetOf(*hit), first);

  // Dozadu to iste zrkadlovo.
  hit = transcript.Find(L"ihla", second, true, fold);
  CHECK(hit.has_value());
  CHECK(!hit->wrapped);
  CHECK_EQ(offsetOf(*hit), first);
  hit = transcript.Find(L"ihla", first, true, fold);
  CHECK(hit.has_value());
  CHECK(hit->wrapped);
  CHECK_EQ(offsetOf(*hit), second);

  // Jediny vyskyt sa najde znova, s pretocenim -- inak by F3 znelo ako nic.
  CHECK(!transcript.SetCollapsed(1, true).empty());
  hit = transcript.Find(L"kope", transcript.blocks()[1].start, false, fold);
  CHECK(hit.has_value());
  CHECK_EQ(hit->index, size_t{1});
  hit = transcript.Find(L"konci", offsetOf(*transcript.Find(L"konci", 0, false,
                                                             fold)),
                        false, fold);
  CHECK(hit.has_value());
  CHECK(hit->wrapped);
  CHECK_EQ(hit->index, size_t{2});

  CHECK(!transcript.Find(L"seno", 0, false, fold).has_value());
  CHECK(!transcript.Find(L"", 0, false, fold).has_value());
}

void TestRateLimitParsing() {
  TEST("events: rate_limit_event");
  // Tvar NIE JE vymysleny a nie je ani odpozorovany: v korpuse ziadny
  // rate_limit_event nie je -- limit sa neda vyrobit na poziadanie. Polia su
  // odpisane zo schemy vo vnutri CLI (grep cez binarku, viz bd memory
  // 'ked-treba-zistit-ako-sa-claude-cli-sprava'): status je povinny, vsetko
  // ostatne volitelne. Preto sa tu testuje hlavne to, ze chybajuce pole nic
  // nerozbije.
  proto::Json full = proto::Json::parse(R"({
    "type": "rate_limit_event",
    "rate_limit_info": {
      "status": "allowed_warning",
      "rateLimitType": "five_hour",
      "utilization": 0.42,
      "resetsAt": 1700000000,
      "unifiedWindows": {
        "five_hour": {"utilization": 0.42, "resetsAt": 1700000000},
        "seven_day": {"utilization": 0.13, "resetsAt": 1700400000}
      }
    }
  })");
  proto::RateLimit limit;
  CHECK(proto::ParseRateLimit(full, &limit));
  CHECK_EQ(limit.status, std::string("allowed_warning"));
  CHECK_EQ(limit.limitType, std::string("five_hour"));
  CHECK_EQ(limit.resetsAt, 1700000000LL);
  CHECK(limit.fiveHourUtilization > 0.41 && limit.fiveHourUtilization < 0.43);
  CHECK(limit.sevenDayUtilization > 0.12 && limit.sevenDayUtilization < 0.14);

  // Holy zaznam: len status. Vsetko ostatne musi zostat na 'nepovedali'.
  proto::Json bare = proto::Json::parse(R"({
    "type": "rate_limit_event",
    "rate_limit_info": {"status": "rejected"}
  })");
  proto::RateLimit minimal;
  CHECK(proto::ParseRateLimit(bare, &minimal));
  CHECK_EQ(minimal.status, std::string("rejected"));
  CHECK(minimal.utilization < 0);
  CHECK(minimal.fiveHourUtilization < 0);
  CHECK_EQ(minimal.resetsAt, 0LL);

  // Iny typ zaznamu a zaznam bez info nie su chyba, len nie su rate limit.
  proto::RateLimit ignored;
  CHECK(!proto::ParseRateLimit(proto::Json::parse(R"({"type": "result"})"),
                               &ignored));
  CHECK(!proto::ParseRateLimit(
      proto::Json::parse(R"({"type": "rate_limit_event"})"), &ignored));
  CHECK(!proto::ParseRateLimit(
      proto::Json::parse(
          R"({"type": "rate_limit_event", "rate_limit_info": {}})"),
      &ignored));
}

void TestInitializeResponseParsing() {
  TEST("control: rezim povoleni sa vie uz z odpovede na initialize");
  // Odpozorovane cez tools/probe_init.py: CLI odpovie na 'initialize' este
  // pred prvym promptom a v odpovedi je 'current_permission_mode'. Model tam
  // NIE JE -- pole 'models' je katalog toho, co sa da poslat do --model,
  // a polozka "default" sa na ucte s "model": "opus" v settings.json rozvinie
  // na claude-sonnet-5. Citat model odtial by teda zobrazilo nespravny az do
  // prveho system/init.
  proto::Json record = proto::Json::parse(R"({
    "type": "control_response",
    "response": {
      "subtype": "success",
      "request_id": "init-1",
      "response": {
        "current_permission_mode": "acceptEdits",
        "pid": 9900,
        "session_state": "idle"
      }
    }
  })");
  proto::InitializeInfo info;
  CHECK(proto::ParseInitializeResponse(record, &info));
  CHECK_EQ(info.requestId, std::string("init-1"));
  CHECK_EQ(info.permissionMode, std::string("acceptEdits"));

  // Ucet je v tej istej odpovedi, teda sa vie tiez pred prvym tahom.
  // Odpozorovane cez tools/probe_dialog.py 2026-09-05. 'organization' sa
  // zamerne necita: na osobnom ucte je vyrobena z adresy ("<email>'s
  // Organization"), takze by to bol ten isty udaj druhy raz.
  proto::Json withAccount = proto::Json::parse(R"({
    "type": "control_response",
    "response": {
      "subtype": "success",
      "request_id": "init-2",
      "response": {
        "current_permission_mode": "default",
        "account": {
          "email": "niekto@example.com",
          "organization": "niekto@example.com's Organization",
          "subscriptionType": "Claude Pro",
          "apiProvider": "firstParty"
        }
      }
    }
  })");
  proto::InitializeInfo account;
  CHECK(proto::ParseInitializeResponse(withAccount, &account));
  CHECK_EQ(account.accountEmail, std::string("niekto@example.com"));
  CHECK_EQ(account.subscriptionType, std::string("Claude Pro"));
  CHECK_EQ(account.apiProvider, std::string("firstParty"));

  // Odpoved bez uctu je stale platna odpoved: rezim je to jedine, co o nej
  // rozhoduje. Keby chybajuci ucet parsovanie zhodil, prestal by sa vediet aj
  // rezim -- a ten sa vie vzdy.
  CHECK_EQ(info.accountEmail, std::string());
  CHECK_EQ(info.subscriptionType, std::string());

  // Zoznam slash prikazov je v tej istej odpovedi a je to JEDINY zdroj, ktory
  // nesie popis aj argumentHint -- 'slash_commands' v system/init su hole mena.
  // Tvar odmerany 2026-09-06 cez tools/probe_commands.py: 79 poloziek, z toho
  // 22 s neprazdnym argumentHint a 25 pluginovych s 'aliases'.
  proto::Json withCommands = proto::Json::parse(R"({
    "type": "control_response",
    "response": {
      "subtype": "success",
      "request_id": "init-3",
      "response": {
        "current_permission_mode": "default",
        "commands": [
          {"name": "code-review",
           "description": "Review the current diff.",
           "argumentHint": "[low|medium|high] [<pr#>]"},
          {"name": "mattpocock-skills:tdd",
           "description": "Test-driven development.",
           "argumentHint": "",
           "aliases": ["tdd"]},
          {"description": "Polozka bez mena sa zahadzuje"},
          "toto nie je objekt"
        ]
      }
    }
  })");
  proto::InitializeInfo withList;
  CHECK(proto::ParseInitializeResponse(withCommands, &withList));
  CHECK_EQ(withList.commands.size(), size_t{2});
  CHECK_EQ(withList.commands[0].name, std::string("code-review"));
  CHECK_EQ(withList.commands[0].argumentHint,
           std::string("[low|medium|high] [<pr#>]"));
  CHECK(withList.commands[0].aliases.empty());
  CHECK_EQ(withList.commands[1].aliases.size(), size_t{1});
  CHECK_EQ(withList.commands[1].aliases[0], std::string("tdd"));
  // Odpoved bez prikazov je stale platna odpoved, presne ako odpoved bez uctu.
  CHECK(info.commands.empty());

  // Odpoved na nieco ine, chybova odpoved a zaznam bez modu nie su chyba, len
  // sa z nich rezim dozvediet neda.
  proto::InitializeInfo ignored;
  CHECK(!proto::ParseInitializeResponse(
      proto::Json::parse(R"({"type": "control_request"})"), &ignored));
  CHECK(!proto::ParseInitializeResponse(
      proto::Json::parse(
          R"({"type": "control_response",
              "response": {"subtype": "error", "request_id": "init-1"}})"),
      &ignored));
  CHECK(!proto::ParseInitializeResponse(
      proto::Json::parse(
          R"({"type": "control_response",
              "response": {"subtype": "success", "request_id": "init-1",
                           "response": {"pid": 1}}})"),
      &ignored));
}

// Cyklus Shift+Tab sa presunul z proto::NextPermissionMode do portu
// (agent::NextMode nad zoznamom rezimov adaptera).  Tu sa drzi proti tomu
// staremu pre kazdy rezim, aj neznamy -- prave tam sa dva cykly najlahsie
// rozidu a nikto by si to nevsimol.
void TestPortModeCycleMatchesClaude() {
  TEST("port: cyklus rezimov Claude je ten isty ako predtym");
  const agent::Capabilities claude = proto::ClaudeCapabilities();
  for (const char* mode : {"default", "acceptEdits", "plan", "auto",
                           "bypassPermissions", "dontAsk", "manual", "vymysleny"}) {
    CHECK_EQ(agent::NextMode(claude, mode), proto::NextPermissionMode(mode));
  }
  // Neznamy rezim (CLI este neodpovedalo) sa necykluje -- klavesa to povie.
  CHECK_EQ(agent::NextMode(claude, ""), std::string());
  // Bezny rezim sa v stavovom riadku nepise.
  const agent::Mode* ordinary = agent::FindMode(claude, "default");
  CHECK(ordinary != nullptr && ordinary->ordinary);
  const agent::Mode* plan = agent::FindMode(claude, "plan");
  CHECK(plan != nullptr && !plan->ordinary);
  // manual je ina meno pre default (CLI 2.1.288, tools/probe_cli_args.py):
  // zname, bezne, ale mimo cyklu, aby na nom Shift+Tab nikdy nepristal.
  const agent::Mode* manual = agent::FindMode(claude, "manual");
  CHECK(manual != nullptr && manual->ordinary && !manual->inCycle);
  CHECK(agent::FindMode(claude, "vymysleny") == nullptr);
  // Agent bez rezimov nema kam cyklovat.
  CHECK_EQ(agent::NextMode(agent::Capabilities(), "default"), std::string());
}

// Dve sessions v tom istom priecinku mali rovnake meno v titulku, v menu Okno
// aj v tom, co NVDA povie pri Ctrl+Tab.  Cislo dostanu len tie, ktore ho
// potrebuju, a po zatvoreni sa precisluju bez dier.
void TestSessionOrdinals() {
  TEST("sessions: poradie v jednom priecinku");
  using W = std::vector<std::wstring>;
  using I = std::vector<int>;
  CHECK(app::SessionOrdinals(W{}) == I{});
  CHECK(app::SessionOrdinals(W{L"a"}) == I{0});
  CHECK(app::SessionOrdinals(W{L"a", L"b"}) == (I{0, 0}));
  CHECK(app::SessionOrdinals(W{L"a", L"b", L"a"}) == (I{1, 0, 2}));
  CHECK(app::SessionOrdinals(W{L"a", L"a", L"a"}) == (I{1, 2, 3}));
  // Druha z troch zatvorena: tretia je teraz druha.
  CHECK(app::SessionOrdinals(W{L"a", L"a"}) == (I{1, 2}));
  // Zostala jedna: cislo zmizne.
  CHECK(app::SessionOrdinals(W{L"b", L"a"}) == (I{0, 0}));
}

// Prikazovy riadok (invariant 16 a claude-gui-lkk.44.7).  Kazde z tychto
// pravidiel zlyhava ticho: volba, ktora sa stane priecinkom, alebo session,
// ktora sa spusti a mlci.
void TestArguments() {
  TEST("argumenty: oddelovac --, --backend a rezim proti backendu");
  using W = std::vector<std::wstring>;

  app::Arguments plain = app::Parse(W{L".", L"--model", L"haiku"});
  CHECK_EQ(plain.project, std::wstring(L"."));
  CHECK_EQ(plain.model, std::wstring(L"haiku"));
  CHECK(plain.error.empty());
  CHECK(plain.cliArgs.empty());

  // Neznama volba sa neprepošle a nestane sa z nej priecinok.
  app::Arguments unknown = app::Parse(W{L"--fork-session", L"."});
  CHECK(unknown.error.find(L"--fork-session") != std::wstring::npos);
  CHECK_EQ(unknown.project, std::wstring(L"."));

  // Volba s hodnotou na konci riadku nezje priecinok o slovo dalej.
  app::Arguments dangling = app::Parse(W{L"--model"});
  CHECK(!dangling.error.empty());
  CHECK(dangling.project.empty());

  // Za -- ide vsetko do CLI, aj to, co vyzera ako priecinok alebo nase volby.
  app::Arguments forwarded = app::Parse(
      W{L"C:\\p", L"--", L"--chrome", L"--add-dir", L"D:\\x", L"--help"});
  CHECK(forwarded.error.empty());
  CHECK(!forwarded.help);
  CHECK_EQ(forwarded.project, std::wstring(L"C:\\p"));
  CHECK_EQ(forwarded.cliArgs.size(), size_t{4});
  if (forwarded.cliArgs.size() == 4) {
    CHECK_EQ(forwarded.cliArgs[0], std::wstring(L"--chrome"));
    CHECK_EQ(forwarded.cliArgs[2], std::wstring(L"D:\\x"));
  }
  app::Arguments onlyCli = app::Parse(W{L"--", L"."});
  CHECK(onlyCli.project.empty());
  CHECK_EQ(onlyCli.cliArgs.size(), size_t{1});

  // --help vyhra aj nad preklepom pred nim; main ho obsluzi prvy.
  app::Arguments help = app::Parse(W{L"--bogus", L"--help"});
  CHECK(help.help);
  // --version rovnako: okno nevznikne a kontroly backendu mlcia.
  app::Arguments version = app::Parse(W{L".", L"--version", L"--bogus"});
  CHECK(version.version);
  CHECK(version.error.empty());
  app::Arguments versionBackend = app::Parse(W{L"--backend", L"x", L"--version"});
  app::CheckBackend(&versionBackend, {"claude"});
  CHECK(versionBackend.error.empty());

  // Hole --resume: na konci aj pred oddelovacom.
  app::Arguments bare = app::Parse(W{L".", L"--resume"});
  CHECK(bare.resume);
  CHECK(bare.resumeId.empty());
  app::Arguments bareBeforeCli = app::Parse(W{L".", L"--resume", L"--", L"-x"});
  CHECK(bareBeforeCli.resume);
  CHECK(bareBeforeCli.resumeId.empty());
  CHECK_EQ(bareBeforeCli.cliArgs.size(), size_t{1});
  app::Arguments both = app::Parse(W{L"-c", L"--resume", L"abc", L"."});
  CHECK(both.continueLatest);
  CHECK_EQ(both.resumeId, std::wstring(L"abc"));

  // Backend: bez volby predvoleny, neznamy sa odmietne a vymenuju sa zname.
  const std::vector<std::string> known = {"claude"};
  app::Arguments defaulted = app::Parse(W{L"."});
  app::CheckBackend(&defaulted, known);
  CHECK_EQ(defaulted.backend, std::string("claude"));
  CHECK(defaulted.error.empty());
  app::Arguments wrong = app::Parse(W{L"--backend", L"gemini", L"."});
  app::CheckBackend(&wrong, known);
  CHECK(wrong.error.find(L"gemini") != std::wstring::npos);
  CHECK(wrong.error.find(L"claude") != std::wstring::npos);

  // Rezim sa overuje proti rezimom zvoleneho backendu a pri preklepe sa
  // vymenuju platne.
  const agent::Capabilities claude = proto::ClaudeCapabilities();
  app::Arguments plan = app::Parse(W{L"--permission-mode", L"plan", L"."});
  app::CheckMode(&plan, claude);
  CHECK(plan.error.empty());
  // Slovo, ktore CLI samo ponuka vo svojom --help, sa odmietnut nesmie.
  app::Arguments manual = app::Parse(W{L"--permission-mode", L"manual", L"."});
  app::CheckMode(&manual, claude);
  CHECK(manual.error.empty());
  app::Arguments typo = app::Parse(W{L"--permission-mode", L"plna", L"."});
  app::CheckMode(&typo, claude);
  CHECK(typo.error.find(L"plna") != std::wstring::npos);
  CHECK(typo.error.find(L"acceptEdits") != std::wstring::npos);
  // Prva chyba sa nepreplaca druhou.
  app::Arguments first = app::Parse(W{L"--bogus", L"--permission-mode", L"x"});
  app::CheckMode(&first, claude);
  CHECK(first.error.find(L"--bogus") != std::wstring::npos);

  // Napoveda hovori pravdu: kazdy rezim aj kazdy backend v nej je -- v kazdom
  // jazyku, lebo text je v katalogu a nie vedla Parse.
  for (const i18n::Lang lang : i18n::Languages()) {
    i18n::SetLanguage(lang);
    const std::wstring text = app::HelpText(
        L"2026.10.1", known, claude, L"C:\\nastavenia\\settings.txt");
    // Kde sa subor nastaveni hlada, nikto neuhadne; napoveda to musi povedat.
    CHECK(text.find(L"C:\\nastavenia\\settings.txt") != std::wstring::npos);
    CHECK(text.find(L"" APP_NAME L" 2026.10.1 ") == 0);
    for (const wchar_t* option :
         {L"--backend", L"--permission-mode", L"--model", L"--resume",
          L"--continue", L"--help", L"--version", L"language=", L"F1"}) {
      CHECK(text.find(option) != std::wstring::npos);
    }
    for (const std::string& name : known) {
      CHECK(text.find(model::Utf16FromUtf8(name)) != std::wstring::npos);
    }
    for (const agent::Mode& mode : claude.modes) {
      CHECK(text.find(model::Utf16FromUtf8(mode.id)) != std::wstring::npos);
    }
    // Znacka bez argumentu by v texte zostala doslova.
    CHECK(text.find(L"{") == std::wstring::npos);
  }
  i18n::SetLanguage(i18n::Lang::kSlovak);
}

// Subor nastaveni (claude-gui-lkk.55).  Zly riadok sa odmieta, nie preskakuje:
// preskoceny preklep vyzera presne ako nastavenie, ktore nikto neurobil.
void TestSettings() {
  TEST("nastavenia: format, kontrola proti backendom a prednost");
  using W = std::vector<std::wstring>;
  std::map<std::string, agent::Capabilities> backends;
  backends["claude"] = proto::ClaudeCapabilities();
  backends["codex"] = proto::codex::CodexCapabilities();

  // BOM z Notepadu, CRLF, poznamky, medzery okolo = a prazdna hodnota.
  app::Settings good = app::Settings::Parse(
      "\xef\xbb\xbf# moje\r\n\r\nbackend = claude\r\n"
      "claude.permission-mode=auto\r\nclaude.model=\r\n"
      "codex.model=gpt-6-luna \xc4\x8d\r\n");
  good.Check(backends);
  CHECK(good.error().empty());
  CHECK_EQ(good.Get("backend"), std::string("claude"));
  CHECK_EQ(good.Get("claude.permission-mode"), std::string("auto"));
  CHECK_EQ(good.Get("claude.model"), std::string());
  CHECK_EQ(good.Get("codex.model"), std::string("gpt-6-luna \xc4\x8d"));
  CHECK_EQ(good.Get("chyba"), std::string());
  CHECK_EQ(good.entries().at("claude.permission-mode").line, 4);

  auto errorOf = [&backends](const char* text) {
    app::Settings settings = app::Settings::Parse(text);
    settings.Check(backends);
    return settings.error();
  };
  // Preklep v kluci, aj ked je hodnota prazdna.
  CHECK(errorOf("claude.permision-mode=auto\n").find(L"riadok 1") == 0);
  CHECK(!errorOf("claude.permision-mode=\n").empty());
  CHECK(!errorOf("gemini.model=x\n").empty());
  CHECK(!errorOf("permission-mode=auto\n").empty());
  CHECK(!errorOf("bez rovnasa\n").empty());
  CHECK(!errorOf("=hodnota\n").empty());
  // Rezim sa overuje proti backendu, ktoremu riadok patri -- aj ked bezi iny.
  const std::wstring mode = errorOf("backend=claude\ncodex.permission-mode=acceptEdits\n");
  CHECK(mode.find(L"riadok 2") == 0);
  CHECK(mode.find(L"acceptEdits") != std::wstring::npos);
  CHECK(errorOf("codex.permission-mode=plan\nclaude.permission-mode=plan\n").empty());
  CHECK(errorOf("backend=gemini\n").find(L"claude") != std::wstring::npos);
  // Dvakrat ten isty kluc: hlasi sa druhy riadok a meno prveho.
  const std::wstring twice = errorOf("claude.model=a\n# x\nclaude.model=b\n");
  CHECK(twice.find(L"riadok 3") == 0);
  CHECK(twice.find(L"riadku 1") != std::wstring::npos);
  // Prva chyba sa nepreplaca dalsou.
  CHECK(errorOf("x\ny\n").find(L"riadok 1") == 0);

  // Prednost: prikazovy riadok > subor > zabudovane.  Rezim a model sa beru
  // pre backend, ktory naozaj pobezi, nech ho povedal ktokolvek.
  const app::Settings file = app::Settings::Parse(
      "claude.permission-mode=auto\nclaude.model=opus\n"
      "codex.permission-mode=plan\n");
  app::Arguments none = app::Parse(W{L"."});
  app::ApplySettings(&none, file);
  CHECK(none.backend.empty());  // predvoleny dopise CheckBackend
  CHECK_EQ(none.permissionMode, std::wstring(L"auto"));
  CHECK_EQ(none.model, std::wstring(L"opus"));
  app::Arguments typed =
      app::Parse(W{L"--permission-mode", L"plan", L"--model", L"haiku", L"."});
  app::ApplySettings(&typed, file);
  CHECK_EQ(typed.permissionMode, std::wstring(L"plan"));
  CHECK_EQ(typed.model, std::wstring(L"haiku"));
  app::Arguments codex = app::Parse(W{L"--backend", L"codex", L"."});
  app::ApplySettings(&codex, file);
  CHECK_EQ(codex.permissionMode, std::wstring(L"plan"));
  CHECK(codex.model.empty());
  app::Arguments chosen = app::Parse(W{L"."});
  app::ApplySettings(&chosen,
                     app::Settings::Parse("backend=codex\ncodex.permission-mode=plan\n"));
  CHECK_EQ(chosen.backend, std::string("codex"));
  CHECK_EQ(chosen.permissionMode, std::wstring(L"plan"));
  // Rezim zo suboru prejde tou istou kontrolou ako napisany.
  app::CheckBackend(&chosen, {"claude", "codex"});
  app::CheckMode(&chosen, backends["codex"]);
  CHECK(chosen.error.empty());
}

// Zapis do suboru nastaveni (invariant 22): co zapis nemeni, zostane bajt po
// bajte, lebo subor pise aj clovek.
void TestSettingsWrite() {
  TEST("nastavenia: zapis meni len svoj riadok");
  const std::string original =
      "\xef\xbb\xbf# moje poznamky\r\nclaude.permission-mode=auto\r\n\r\n"
      "last-update-check=2026-10-01\r\n";
  app::Settings same = app::Settings::Parse(original);
  CHECK_EQ(same.Serialize(), original);
  // Bez posledneho noveho riadku aj prazdny subor.
  CHECK_EQ(app::Settings::Parse("a=b").Serialize(), std::string("a=b"));
  CHECK_EQ(app::Settings::Parse("").Serialize(), std::string());

  app::Settings changed = app::Settings::Parse(original);
  changed.Set("last-update-check", "2026-10-05");
  changed.Set("skipped-version", "2026.10.2");
  CHECK_EQ(changed.Serialize(),
           std::string("\xef\xbb\xbf# moje poznamky\r\nclaude.permission-mode=auto\r\n"
                       "\r\nlast-update-check=2026-10-05\r\n"
                       "skipped-version=2026.10.2\r\n"));
  CHECK_EQ(changed.Get("skipped-version"), std::string("2026.10.2"));
  // Druhy zapis toho isteho kluca ide na jeho riadok, nie na koniec.
  changed.Set("skipped-version", "2026.10.3");
  const app::Settings reread = app::Settings::Parse(changed.Serialize());
  CHECK_EQ(reread.Get("skipped-version"), std::string("2026.10.3"));
  CHECK_EQ(reread.entries().size(), size_t{3});

  // Posledny riadok bez noveho riadku sa s pridanym nezlepi.
  app::Settings glued = app::Settings::Parse("a=b");
  glued.Set("c", "d");
  CHECK_EQ(glued.Serialize(), std::string("a=b\nc=d\n"));
  app::Settings fresh = app::Settings::Parse("");
  fresh.Set("check-updates", "0");
  CHECK_EQ(fresh.Serialize(), std::string("check-updates=0\n"));

  // Vypinac: zapnuty, kym subor vyslovne nepovie 0.
  CHECK(app::Settings::Parse("").CheckUpdates());
  CHECK(app::Settings::Parse("check-updates=1\n").CheckUpdates());
  CHECK(!app::Settings::Parse("check-updates=0\n").CheckUpdates());
  std::map<std::string, agent::Capabilities> backends;
  backends["claude"] = proto::ClaudeCapabilities();
  app::Settings updates = app::Settings::Parse(
      "check-updates=1\nlast-update-check=hocico\nskipped-version=\n");
  updates.Check(backends);
  CHECK(updates.error().empty());
  app::Settings yes = app::Settings::Parse("check-updates=ano\n");
  yes.Check(backends);
  CHECK(yes.error().find(L"ano") != std::wstring::npos);

  // Jazyk: sk, en alebo prazdny riadok-sablona; nic ine.
  app::Settings language = app::Settings::Parse("language=en\n");
  language.Check(backends);
  CHECK(language.error().empty());
  CHECK_EQ(language.Get(app::kLanguageKey), std::string("en"));
  app::Settings template_ = app::Settings::Parse("language=\n");
  template_.Check(backends);
  CHECK(template_.error().empty());
  app::Settings german = app::Settings::Parse("language=de\n");
  german.Check(backends);
  CHECK(german.error().find(L"de") != std::wstring::npos);
}

// Pravidla aktualizacii bez siete (claude-gui-lkk.53).
void TestUpdateRules() {
  TEST("aktualizacie: verzia, znacka, sucet, ponuka, plan vymeny");
  using version::Number;
  // Ako cisla, nie ako text.
  CHECK(*version::Parse(L"2026.9.5") < *version::Parse(L"2026.10.1"));
  CHECK(version::Parse(L"v2026.10.1") == (Number{2026, 10, 1}));
  CHECK(!version::Parse(L"2026.10.1-5-gabc1234"));
  CHECK(!version::Parse(L"2026.10.1-dirty"));
  CHECK(!version::Parse(L"0.0.0-6ce7657"));
  CHECK(!version::Parse(L"2026.10"));
  CHECK(!version::Parse(L"2026.10.123456"));
  CHECK(!version::Parse(L""));

  CHECK(update::TagFromLocation(
            L"https://github.com/lpintes/agentaloud/releases/tag/v2026.10.2") ==
        std::optional<std::wstring>(L"v2026.10.2"));
  CHECK(!update::TagFromLocation(L"https://github.com/lpintes/agentaloud/releases"));
  CHECK(!update::TagFromLocation(L"https://github.com/x/releases/tag/nightly"));

  const std::string sums =
      "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef  AgentAloud.zip\r\n"
      "1111111111111111111111111111111111111111111111111111111111111111 *SHA.txt\n";
  CHECK(update::HashFor(sums, "AgentAloud.zip") ==
        std::optional<std::string>(
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  CHECK(update::HashFor(sums, "SHA.txt").has_value());
  CHECK(!update::HashFor(sums, "AgentAloud-2026.10.1.zip"));

  const Number current{2026, 10, 1};
  CHECK(update::ShouldOffer(current, L"v2026.10.2", L""));
  CHECK(!update::ShouldOffer(current, L"v2026.10.1", L""));
  CHECK(!update::ShouldOffer(current, L"v2026.9.9", L""));
  CHECK(!update::ShouldOffer(std::nullopt, L"v2026.10.2", L""));  // vyvojove
  CHECK(!update::ShouldOffer(current, L"v2026.10.2", L"2026.10.2"));
  CHECK(update::ShouldOffer(current, L"v2026.10.3", L"2026.10.2"));
  CHECK(update::CheckDue(L"2026-10-04", L"2026-10-05"));
  CHECK(update::CheckDue(L"", L"2026-10-05"));
  CHECK(!update::CheckDue(L"2026-10-05", L"2026-10-05"));

  // Plan: vrchny priecinok sa zreze, config/ sa vynecha.
  using W = std::vector<std::wstring>;
  const update::Plan plan = update::PlanReplace(
      W{L"AgentAloud-2026.10.2/agentaloud.exe",
        L"AgentAloud-2026.10.2\\nvdaControllerClient.dll",
        L"AgentAloud-2026.10.2/Config/settings.txt",
        L"AgentAloud-2026.10.2/prism/a.dll"},
      L"agentaloud.exe");
  CHECK(plan.error.empty());
  CHECK_EQ(plan.files.size(), size_t{3});
  if (plan.files.size() == 3) {
    CHECK_EQ(plan.files[0].to, std::wstring(L"agentaloud.exe"));
    CHECK_EQ(plan.files[1].to, std::wstring(L"nvdaControllerClient.dll"));
    CHECK_EQ(plan.files[2].to, std::wstring(L"prism\\a.dll"));
    CHECK_EQ(plan.files[2].from, std::wstring(L"AgentAloud-2026.10.2/prism/a.dll"));
  }
  // Bez vrchneho priecinka sa nic nezreze.
  const update::Plan flat =
      update::PlanReplace(W{L"agentaloud.exe", L"x/y.dll"}, L"agentaloud.exe");
  CHECK(flat.error.empty());
  CHECK_EQ(flat.files.size(), size_t{2});
  // Cesta mimo priecinka appky odmietne cely balik.
  CHECK(!update::PlanReplace(W{L"agentaloud.exe", L"../x.dll"}, L"agentaloud.exe")
             .error.empty());
  CHECK(!update::PlanReplace(W{L"agentaloud.exe", L"C:\\x.dll"}, L"agentaloud.exe")
             .error.empty());
  CHECK(!update::PlanReplace(W{L"agentaloud.exe", L"\\x.dll"}, L"agentaloud.exe")
             .error.empty());
  CHECK(!update::PlanReplace(W{L"agentaloud.exe:zlo"}, L"agentaloud.exe")
             .error.empty());
  // Bez EXE to nie je aktualizacia; EXE v podpriecinku sa neratá.
  const update::Plan noExe =
      update::PlanReplace(W{L"top/sub/agentaloud.exe", L"top/a.dll"}, L"agentaloud.exe");
  CHECK(!noExe.error.empty());
  CHECK(noExe.files.empty());
  CHECK(!update::PlanReplace(W{}, L"agentaloud.exe").error.empty());
}

void TestPermissionModeSwitch() {
  TEST("control: Shift+Tab meni rezim za behu");

  // Poradie cyklu je terminalove -- taky zoznam ozve aj TUI hint v binarke:
  // default -> acceptEdits -> plan -> auto -> default. bypassPermissions
  // (stdio host ho nezapne), dontAsk, neznamy a prazdny zacnu cyklus nanovo na
  // acceptEdits, aby klavesa vzdy pohla.
  CHECK_EQ(proto::NextPermissionMode("default"), std::string("acceptEdits"));
  CHECK_EQ(proto::NextPermissionMode("acceptEdits"), std::string("plan"));
  CHECK_EQ(proto::NextPermissionMode("plan"), std::string("auto"));
  CHECK_EQ(proto::NextPermissionMode("auto"), std::string("default"));
  CHECK_EQ(proto::NextPermissionMode("bypassPermissions"),
           std::string("acceptEdits"));
  CHECK_EQ(proto::NextPermissionMode("dontAsk"), std::string("acceptEdits"));
  CHECK_EQ(proto::NextPermissionMode(""), std::string("acceptEdits"));
  CHECK_EQ(proto::NextPermissionMode("nieco"), std::string("acceptEdits"));

  // Poziadavka: subtype set_permission_mode a holy mod.
  const proto::Json request = proto::MakeSetPermissionMode("mode-3", "plan");
  CHECK_EQ(request.value("type", std::string()), std::string("control_request"));
  CHECK_EQ(request.value("request_id", std::string()), std::string("mode-3"));
  CHECK_EQ(request["request"].value("subtype", std::string()),
           std::string("set_permission_mode"));
  CHECK_EQ(request["request"].value("mode", std::string()), std::string("plan"));

  // Odpoved: headless CLI ozve mod spat pod dvojitym `response` (rovnake
  // hniezdenie ako odpoved na initialize). Odmerane 2026-09-05, lkk.6.1.
  std::string requestId;
  std::string mode;
  CHECK(proto::ParseSetPermissionModeResponse(
      proto::Json::parse(R"({
        "type": "control_response",
        "response": {
          "subtype": "success",
          "request_id": "mode-3",
          "response": {"mode": "plan"}
        }
      })"),
      &requestId, &mode));
  CHECK_EQ(requestId, std::string("mode-3"));
  CHECK_EQ(mode, std::string("plan"));

  // Hostitel, ktory nie je headless, smie potvrdit prazdnym objektom -- stale
  // je to uspech, len sa mod neozval a volajuci si necha ten, o ktory ziadal.
  std::string ackId;
  std::string ackMode;
  CHECK(proto::ParseSetPermissionModeResponse(
      proto::Json::parse(R"({
        "type": "control_response",
        "response": {"subtype": "success", "request_id": "mode-4"}
      })"),
      &ackId, &ackMode));
  CHECK_EQ(ackId, std::string("mode-4"));
  CHECK_EQ(ackMode, std::string());

  // Chybova odpoved a odpoved na nieco ine nie su potvrdenie.
  std::string dummy;
  std::string dummyMode;
  CHECK(!proto::ParseSetPermissionModeResponse(
      proto::Json::parse(R"({
        "type": "control_response",
        "response": {"subtype": "error", "request_id": "mode-3",
                     "error": "unrecognized mode"}
      })"),
      &dummy, &dummyMode));
  CHECK(!proto::ParseSetPermissionModeResponse(
      proto::Json::parse(R"({"type": "control_request"})"), &dummy, &dummyMode));
}

// Zaznamy v tvare, v akom ich CLI 2.1.267 naozaj poslalo sonde 2026-09-11
// (--permission-mode plan --model opusplan, potom set_permission_mode auto
// a plan). Pomocniky, aby test citat ako poradie udalosti, nie ako JSON.
proto::Json ModeStatus(const char* mode) {
  proto::Json record = proto::Json::parse(
      R"({"type": "system", "subtype": "status", "status": null})");
  record["permissionMode"] = mode;
  return record;
}

proto::Json ModeAnswer(const char* id, const char* echoed) {
  proto::Json record = proto::Json::parse(R"({
    "type": "control_response",
    "response": {"subtype": "success", "response": {}}
  })");
  record["response"]["request_id"] = id;
  if (echoed != nullptr) record["response"]["response"]["mode"] = echoed;
  return record;
}

proto::Json ModeRefusal(const char* id) {
  proto::Json record = proto::Json::parse(
      R"({"type": "control_response", "response": {"subtype": "error"}})");
  record["response"]["request_id"] = id;
  return record;
}

void TestPermissionModeReports() {
  TEST("control: system/status hlasi kazdu zmenu rezimu, aj tu od CLI");

  // Presne tento zaznam prisiel za potvrdenim set_permission_mode. Posiela ho
  // CLI pri KAZDEJ zmene (onPermissionModeChanged), teda aj po schvaleni
  // ExitPlanMode -- a appka ho do claude-gui-lkk.41 necitala.
  std::string mode;
  CHECK(proto::ParsePermissionModeReport(
      proto::Json::parse(R"({"type": "system", "subtype": "status",
                             "status": null, "permissionMode": "auto",
                             "uuid": "u", "session_id": "s"})"),
      &mode));
  CHECK_EQ(mode, std::string("auto"));

  // system/init nesie rezim na zaciatku kazdeho tahu.
  std::string atInit;
  CHECK(proto::ParsePermissionModeReport(
      proto::Json::parse(R"({"type": "system", "subtype": "init",
                             "model": "claude-sonnet-5",
                             "permissionMode": "plan"})"),
      &atInit));
  CHECK_EQ(atInit, std::string("plan"));

  // Status bez rezimu (poziadavka na API, kompaktovanie) nie je hlasenie.
  std::string none;
  CHECK(!proto::ParsePermissionModeReport(
      proto::Json::parse(
          R"({"type": "system", "subtype": "status", "status": "requesting"})"),
      &none));
  CHECK(!proto::ParsePermissionModeReport(
      proto::Json::parse(R"({"type": "system", "subtype": "thinking_tokens",
                             "permissionMode": "plan"})"),
      &none));
  CHECK(none.empty());
}

void TestPermissionModeTracker() {
  TEST("control: rezim sleduje CLI, nie len nas Shift+Tab");

  // Pred handshakom: rezim z prikazoveho riadku. Bez neho sa prvy Shift+Tab
  // v projekte s bd prime hookom (handshake ~20 s) cykloval od prazdna, teda
  // z plan do acceptEdits.
  proto::PermissionModeTracker tracker;
  tracker.Seed("plan");
  CHECK_EQ(tracker.current(), std::string("plan"));
  CHECK_EQ(proto::NextPermissionMode(tracker.current()), std::string("auto"));

  // Bez --permission-mode a bez handshaku sa rezim nevie -- zavisi od settings.
  proto::PermissionModeTracker unknown;
  unknown.Seed("");
  CHECK(unknown.current().empty());
  unknown.Observe(proto::Json::parse(R"({
    "type": "control_response",
    "response": {"subtype": "success", "request_id": "init-1",
                 "response": {"current_permission_mode": "auto"}}
  })"));
  CHECK_EQ(unknown.current(), std::string("auto"));

  // Shift+Tab: hned novy rezim. Hlasenie, ktore pride kym odpoved nie je --
  // system/init tahu, ktory CLI zacalo pred nasou poziadavkou -- ho nesmie
  // vratit, inak by citatel pocul "auto" a bar by hovoril plan.
  tracker.Requested("mode-2", "auto");
  CHECK_EQ(tracker.current(), std::string("auto"));
  tracker.Observe(proto::Json::parse(
      R"({"type": "system", "subtype": "init", "permissionMode": "plan"})"));
  CHECK_EQ(tracker.current(), std::string("auto"));
  tracker.Observe(ModeAnswer("mode-2", "auto"));
  tracker.Observe(ModeStatus("auto"));
  CHECK_EQ(tracker.current(), std::string("auto"));

  // Zmena, ktoru urobilo CLI samo: schvalene ExitPlanMode, auto zhodene
  // branou. Pride len ako system/status.
  tracker.Observe(ModeStatus("default"));
  CHECK_EQ(tracker.current(), std::string("default"));
  CHECK_EQ(proto::NextPermissionMode(tracker.current()),
           std::string("acceptEdits"));

  // Odmietnutie: spat na posledne slovo CLI.
  tracker.Requested("mode-3", "acceptEdits");
  tracker.Observe(ModeRefusal("mode-3"));
  CHECK_EQ(tracker.current(), std::string("default"));

  // Dve rychle stlacenia, obe odmietnute. Stara verzia sa po druhom vracala na
  // optimisticku hodnotu prveho, ktore tiez neprislo.
  tracker.Requested("mode-4", "acceptEdits");
  tracker.Requested("mode-5", "plan");
  tracker.Observe(ModeRefusal("mode-4"));
  CHECK_EQ(tracker.current(), std::string("plan"));  // stale novsie stoji
  tracker.Observe(ModeRefusal("mode-5"));
  CHECK_EQ(tracker.current(), std::string("default"));

  // Dve rychle stlacenia, prve prejde a druhe nie: vysledok je prve.
  tracker.Requested("mode-6", "acceptEdits");
  tracker.Requested("mode-7", "plan");
  tracker.Observe(ModeAnswer("mode-6", "acceptEdits"));
  tracker.Observe(ModeStatus("acceptEdits"));
  CHECK_EQ(tracker.current(), std::string("plan"));
  tracker.Observe(ModeRefusal("mode-7"));
  CHECK_EQ(tracker.current(), std::string("acceptEdits"));

  // Hostitel, ktory potvrdi prazdnym objektom: plati to, o co sa ziadalo.
  tracker.Requested("mode-8", "plan");
  tracker.Observe(ModeAnswer("mode-8", nullptr));
  CHECK_EQ(tracker.current(), std::string("plan"));

  // Odpoved na nieco ine nez zmenu rezimu -- napr. na prerusenie -- rezim
  // nehybe, ani ked je uspesna.
  tracker.Requested("mode-9", "auto");
  tracker.Observe(ModeAnswer("stop-10", nullptr));
  CHECK_EQ(tracker.current(), std::string("auto"));
  tracker.Observe(ModeRefusal("stop-11"));
  CHECK_EQ(tracker.current(), std::string("auto"));
}

void TestAnsweringModel() {
  TEST("events: model je ten, ktory odpoveda, nie ten zo system/init");

  // Pri --model opusplan v rezime plan hovori system/init claude-sonnet-5
  // a vsetky assistant zaznamy toho isteho tahu claude-opus-5 (odmerane
  // 2026-09-11, claude-gui-lkk.40). Stream posiela parent_tool_use_id: null.
  std::string model;
  CHECK(proto::ParseAnsweringModel(
      proto::Json::parse(R"({"type": "assistant", "parent_tool_use_id": null,
                             "message": {"model": "claude-opus-5",
                                         "content": []}})"),
      &model));
  CHECK_EQ(model, std::string("claude-opus-5"));

  // Zaznam z disku (--resume) pole nema vobec a je to stale hlavna
  // konverzacia -- sidechainy zahodil uz ReadSessionRecords.
  std::string fromDisk;
  CHECK(proto::ParseAnsweringModel(
      proto::Json::parse(R"({"type": "assistant", "isSidechain": false,
                             "message": {"model": "claude-sonnet-5"}})"),
      &fromDisk));
  CHECK_EQ(fromDisk, std::string("claude-sonnet-5"));

  // Subagent moze bezat na inom modeli a session tym modelom nie je.
  std::string untouched = "claude-opus-5";
  CHECK(!proto::ParseAnsweringModel(
      proto::Json::parse(R"({"type": "assistant",
                             "parent_tool_use_id": "toolu_01",
                             "message": {"model": "claude-haiku-4-5"}})"),
      &untouched));
  // Nahradna sprava, ktoru CLI vyrobilo samo (chyba API, preruseny tah).
  // V korpuse je ako model "<synthetic>".
  CHECK(!proto::ParseAnsweringModel(
      proto::Json::parse(R"({"type": "assistant", "parent_tool_use_id": null,
                             "message": {"model": "<synthetic>"}})"),
      &untouched));
  CHECK(!proto::ParseAnsweringModel(
      proto::Json::parse(R"({"type": "user", "message": {"model": "x"}})"),
      &untouched));
  CHECK_EQ(untouched, std::string("claude-opus-5"));
}

// Kolko udalosti druhu T je v davke.
template <typename T>
size_t CountOf(const std::vector<agent::Event>& events) {
  size_t count = 0;
  for (const agent::Event& event : events) {
    if (std::holds_alternative<T>(event)) ++count;
  }
  return count;
}

// Posledny ModelChanged v davke, alebo prazdny retazec.
std::string ModelIn(const std::vector<agent::Event>& events) {
  std::string model;
  for (const agent::Event& event : events) {
    if (const auto* changed = std::get_if<agent::ModelChanged>(&event)) {
      model = changed->model;
    }
  }
  return model;
}

void TestTranslatorModelAndTurn() {
  TEST("translate: model, koniec tahu a limit -- co predtym citala pane sama");

  // Invariant 21 na zaznamoch: opusplan v plan.  system/init hovori Sonnet,
  // odpoveda Opus, a dalsi system/init ho uz nesmie vratit.
  proto::Translator translator;
  translator.SeedModel("opusplan");
  const auto init = proto::Json::parse(R"({"type": "system",
      "subtype": "init", "cwd": "C:/p", "model": "claude-sonnet-5"})");
  const auto opus = proto::Json::parse(R"({"type": "assistant",
      "parent_tool_use_id": null,
      "message": {"model": "claude-opus-5", "content": []}})");

  std::vector<agent::Event> events = translator.Translate(init);
  CHECK_EQ(CountOf<agent::WorkingDirectory>(events), size_t{1});
  CHECK_EQ(ModelIn(events), std::string("claude-sonnet-5"));
  CHECK_EQ(ModelIn(translator.Translate(opus)), std::string("claude-opus-5"));
  // Druhy tah: init znova so Sonnetom -- uz nie.
  CHECK_EQ(CountOf<agent::ModelChanged>(translator.Translate(init)), size_t{0});
  // Ten isty model znova nie je zmena.
  CHECK_EQ(CountOf<agent::ModelChanged>(translator.Translate(opus)), size_t{0});
  // Subagent na inom modeli session nemeni.
  CHECK_EQ(CountOf<agent::ModelChanged>(translator.Translate(
               proto::Json::parse(R"({"type": "assistant",
                   "parent_tool_use_id": "toolu_01",
                   "message": {"model": "claude-haiku-4-5", "content": []}})"))),
           size_t{0});
  // Po schvaleni ExitPlanMode ten isty tah pokracuje na Sonnete, a to uz
  // povedal assistant, nie system/init.
  CHECK_EQ(ModelIn(translator.Translate(proto::Json::parse(
               R"({"type": "assistant", "parent_tool_use_id": null,
                   "message": {"model": "claude-sonnet-5", "content": []}})"))),
           std::string("claude-sonnet-5"));

  // Premyslanie je znak zivota, nie blok.
  CHECK_EQ(CountOf<agent::ThinkingTick>(translator.Translate(proto::Json::parse(
               R"({"type": "system", "subtype": "thinking_tokens"})"))),
           size_t{1});

  // Koniec tahu: usage PRED TurnEnded, aby koniec tahu hovoril o tahu, ktoreho
  // cisla uz su znama.  Usage sa cita pre model, ktory bezi -- tu Sonnet.
  events = translator.Translate(proto::Json::parse(R"({"type": "result",
      "subtype": "success", "total_cost_usd": 0,
      "modelUsage": {
        "claude-sonnet-5": {"costUSD": 0.5, "inputTokens": 10,
                            "outputTokens": 20, "contextWindow": 200000},
        "claude-opus-5": {"costUSD": 0.1, "inputTokens": 1,
                          "outputTokens": 2, "contextWindow": 500000}}})"));
  CHECK_EQ(events.size(), size_t{2});
  const auto* usage = std::get_if<agent::UsageChanged>(&events[0]);
  CHECK(usage != nullptr);
  const auto* ended = std::get_if<agent::TurnEnded>(&events[1]);
  CHECK(ended != nullptr);
  if (usage != nullptr) {
    CHECK_EQ(usage->usage.contextWindow, 200000LL);
    CHECK_EQ(usage->usage.outputTokens, 22LL);
    // Na predplatnom je uctovana cena skutocna nula, nie chybajuce cislo.
    CHECK(usage->usage.billedUsd.has_value());
    CHECK(usage->usage.listUsd.has_value());
  }
  if (ended != nullptr) {
    CHECK(ended->outcome == agent::TurnOutcome::Completed);
  }

  // Preruseny tah (odmerane: error_during_execution + aborted_streaming).
  events = translator.Translate(proto::Json::parse(R"({"type": "result",
      "subtype": "error_during_execution",
      "terminal_reason": "aborted_streaming"})"));
  CHECK_EQ(CountOf<agent::TurnEnded>(events), size_t{1});
  if (!events.empty()) {
    const auto* aborted = std::get_if<agent::TurnEnded>(&events.back());
    CHECK(aborted != nullptr &&
          aborted->outcome == agent::TurnOutcome::Interrupted);
  }

  // Limit: okna podla dlzky, nie podla slova CLI.
  events = translator.Translate(proto::Json::parse(R"({
    "type": "rate_limit_event",
    "rate_limit_info": {
      "status": "allowed_warning", "rateLimitType": "five_hour",
      "utilization": 0.42, "resetsAt": 1700000000,
      "unifiedWindows": {
        "five_hour": {"utilization": 0.42, "resetsAt": 1700000000},
        "seven_day": {"utilization": 0.13, "resetsAt": 1700400000}}}})"));
  CHECK_EQ(events.size(), size_t{1});
  const auto* limit = events.empty()
                          ? nullptr
                          : std::get_if<agent::RateLimitChanged>(&events[0]);
  CHECK(limit != nullptr);
  if (limit != nullptr) {
    CHECK(limit->state == agent::LimitState::Warning);
    CHECK_EQ(limit->windows.size(), size_t{2});
    if (limit->windows.size() == 2) {
      CHECK_EQ(limit->windows[0].minutes, 300);
      CHECK_EQ(limit->windows[1].minutes, 7 * 24 * 60);
    }
    CHECK_EQ(limit->resetsAt, 1700000000LL);
  }
}

void TestUsageParsing() {
  TEST("events: cena a tokeny sa citaju z modelUsage, nie z usage");
  // Tvar je odpozorovany z tests/fixtures/basic.jsonl, nie vymysleny. Podstatne
  // je, ze modelUsage je KUMULATIVNE za celu session (outputTokens ide 348,
  // 598, 863, 1198), kym top-level usage patri poslednej sprave (348, 250,
  // 265, 335). Keby sa scitavali zaznamy, session by sa zapocitala tolkokrat,
  // kolko mala tahov -- a tichym vysledkom by bola prilis vysoka cena.
  proto::Json result = proto::Json::parse(R"({
    "type": "result",
    "total_cost_usd": 0,
    "usage": {"input_tokens": 18, "output_tokens": 335},
    "modelUsage": {
      "claude-haiku-4-5-20251001": {
        "costUSD": 0.045, "inputTokens": 72, "outputTokens": 1198,
        "cacheReadInputTokens": 97063, "cacheCreationInputTokens": 14781,
        "thinkingTokens": 756, "contextWindow": 200000
      },
      "claude-opus-5": {
        "costUSD": 0.1, "inputTokens": 8, "outputTokens": 2,
        "cacheReadInputTokens": 0, "cacheCreationInputTokens": 0,
        "thinkingTokens": 0, "contextWindow": 500000
      }
    }
  })");
  proto::Usage usage;
  CHECK(proto::ParseUsage(result, "claude-haiku-4-5-20251001", &usage));
  // Scitane cez modely: session sa da prepnut a potom ani jeden zaznam nie je
  // cela pravda.
  CHECK(usage.listUsd > 0.1449 && usage.listUsd < 0.1451);
  CHECK_EQ(usage.outputTokens, 1200LL);
  CHECK_EQ(usage.inputTokens, 80LL);
  CHECK_EQ(usage.cacheReadTokens, 97063LL);
  CHECK_EQ(usage.cacheCreationTokens, 14781LL);
  CHECK_EQ(usage.thinkingTokens, 756LL);
  // Uctovana cena je na predplatnom nula, a nesmie sa tvarit, ze je to cena
  // z cennika -- preto su to dve polia a nie jedno.
  CHECK(usage.billedUsd == 0);
  // Okno sa NESCITAVA: je to strop, nie mnozstvo. Pomenovany model rozhoduje,
  // aj ked ten druhy ma vacsie -- inak by session s podagentom hlasila cudzie
  // okno.
  CHECK_EQ(usage.contextWindow, 200000LL);

  // Bez mena modelu zostava najvacsie okno: pri jednom modeli je to to iste
  // cislo, pri dvoch aspon nie vymyslene.
  proto::Usage unnamed;
  CHECK(proto::ParseUsage(result, "", &unnamed));
  CHECK_EQ(unnamed.contextWindow, 500000LL);

  // Zaznam bez modelUsage: cena z total_cost_usd sama o sebe staci.
  proto::Usage billed;
  CHECK(proto::ParseUsage(
      proto::Json::parse(R"({"type": "result", "total_cost_usd": 0.5})"), "",
      &billed));
  CHECK(billed.billedUsd > 0.49 && billed.billedUsd < 0.51);
  CHECK_EQ(billed.outputTokens, 0LL);
  CHECK_EQ(billed.contextWindow, 0LL);

  // Iny typ zaznamu nie je chyba, len nie je result.
  proto::Usage ignored;
  CHECK(!proto::ParseUsage(proto::Json::parse(R"({"type": "assistant"})"), "",
                           &ignored));
  CHECK(!proto::ParseUsage(proto::Json::parse(R"({"type": "result"})"), "",
                           &ignored));
}

void TestContextTokensParsing() {
  TEST("events: kolko kontextu je zabrate, zo spravy asistenta");
  // Ako plne je okno, nehovori ziadny zaznam priamo. Cisla v 'result' su sucty
  // za celu session, takze na to nie su. Poslednej sprave asistenta sa vsak
  // poslalo vsetko, co je v kontexte, a jej 'usage' to rozpisuje na tri
  // polozky. Tvar odpozorovany z tests/fixtures/basic.jsonl.
  long long tokens = 0;
  CHECK(proto::ParseContextTokens(proto::Json::parse(R"({
    "type": "assistant",
    "message": {"usage": {
      "input_tokens": 8,
      "cache_read_input_tokens": 14461,
      "cache_creation_input_tokens": 320,
      "output_tokens": 2
    }}
  })"), &tokens));
  // Vystup sa NEPOCITA: to, co model napisal, zabera okno az ked sa mu posle
  // spat, a vtedy uz je v niektorej z tych troch poloziek.
  CHECK_EQ(tokens, 14789LL);

  // Zaznam bez usage a zaznam ineho typu nie su chyba.
  long long ignored = -1;
  CHECK(!proto::ParseContextTokens(
      proto::Json::parse(R"({"type": "assistant", "message": {}})"), &ignored));
  CHECK(!proto::ParseContextTokens(proto::Json::parse(R"({"type": "result"})"),
                                   &ignored));
  CHECK_EQ(ignored, -1LL);
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
  transcript.Append(proto::TranslateRecord(assistant));
  transcript.SetCollapsed(transcript.blocks().size() - 1, false);
  CHECK(transcript.Text().find(L'\r') == std::wstring::npos);

  std::string problem;
  CHECK(transcript.CheckInvariants(&problem));
}

void TestAnsiEscapesAreStripped() {
  TEST("transcript: ANSI sekvencie z vystupu nastroja sa zahadzuju");
  // Vystup nastrojov je vystup terminalovych programov.  V korpuse 147
  // session su farby (\x1b[36;1m) aj kurzorove riadenie z progress barov
  // (\x1b[2K, \x1b[1A, \x1b[G).  Ponechane by ich NVDA citala znak po znaku.
  model::Transcript transcript;
  proto::Json result = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_1",
       "content": "\u001b[31;1mchyba\u001b[0m\n\u001b[2K\u001b[1Ghotovo"}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(result));
  transcript.SetCollapsed(transcript.blocks().size() - 1, false);
  const std::wstring& text = transcript.Text();
  CHECK(text.find(L'\x1b') == std::wstring::npos);
  CHECK(text.find(L"[31;1m") == std::wstring::npos);
  // Text medzi sekvenciami zostava, aj ked su nalepene na nom.
  CHECK(text.find(L"chyba") != std::wstring::npos);
  CHECK(text.find(L"hotovo") != std::wstring::npos);

  std::string problem;
  CHECK(transcript.CheckInvariants(&problem));

  // Osamoteny ESC zahodi seba, nie znak za sebou -- v korpuse su za nim
  // obycajne znaky, nie dvojznakove sekvencie.
  model::Transcript lone;
  proto::Json odd = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [{"type": "text", "text": "pred\u001b\"po"}]}
  })");
  lone.Append(proto::TranslateRecord(odd));
  CHECK(lone.Text().find(L"pred\"po") != std::wstring::npos);
}

void TestToolPathsAreShortened() {
  TEST("transcript: cesta v zhrnuti nastroja sa kráti od zaciatku, nie od konca");
  // Pocute pri testovani cez NVDA: zhrnutie zaznelo ako "Write: C:, Users,
  // pintes, AppData, Local, Temp, claude, C--vcs-..., f3a7ea6b-38b1-4427-88"
  // a tam skoncilo -- limit odrezal nazov suboru, cize jedine, co citatel
  // chcel vediet.
  model::Transcript transcript;
  proto::Json init = proto::Json::parse(R"({
    "type": "system", "subtype": "init", "cwd": "C:/projekt/appka"
  })");
  transcript.Append(proto::TranslateRecord(init));

  proto::Json call = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "tool_use", "id": "toolu_1", "name": "Write",
       "input": {"file_path": "C:\\projekt\\appka\\src\\model\\transcript.cpp"}}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(call));
  // Opacne lomky proti lomkam a velke pismena proti malym: to iste miesto.
  CHECK_EQ(transcript.blocks().back().summary,
           std::wstring(L"Write: src\\model\\transcript.cpp"));

  // Mimo projektu sa odreze zaciatok a nazov suboru zostane.
  proto::Json outside = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "tool_use", "id": "toolu_2", "name": "Read",
       "input": {"file_path": "C:\\Users\\niekto\\AppData\\Local\\Temp\\hlboko\\este\\hlbsie\\a\\b\\c\\dolezity_subor.txt"}}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(outside));
  const std::wstring& summary = transcript.blocks().back().summary;
  CHECK(summary.find(L"dolezity_subor.txt") != std::wstring::npos);
  CHECK(summary.find(L"...") == size_t{6});  // hned za "Read: "
}

// Pokyn, ktory adapter posle modelu pri zamietnuti, vrati CLI v streame ako
// text vysledku nastroja -- a prepis by citatelovi ukazal anglicku vetu pre
// model.  Tvar zaznamu je odmerany sondou (CLI 2.1.288, 6. 10. 2026,
// claude-gui-lkk.52): obycajny retazec, is_error true, bez obalu a bez
// system/permission_denied.  Iny text chyby zostava, ako prisiel.
void TestOwnInstructionsStayOutOfTranscript() {
  TEST("transcript: vlastny pokyn modelu sa citatelovi neukaze");
  const auto result = [](const std::string& id, const std::string& text) {
    proto::Json block = {{"type", "tool_result"},
                         {"content", text},
                         {"is_error", true},
                         {"tool_use_id", id}};
    proto::Json record = {{"type", "user"}};
    record["message"] = {{"role", "user"},
                         {"content", proto::Json::array({block})}};
    return record;
  };
  model::Transcript transcript;
  transcript.Append(proto::TranslateRecord(proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "tool_use", "id": "toolu_B", "name": "Bash",
       "input": {"command": "echo hello > hello.txt"}},
      {"type": "tool_use", "id": "toolu_Q", "name": "AskUserQuestion",
       "input": {"questions": []}},
      {"type": "tool_use", "id": "toolu_X", "name": "Bash",
       "input": {"command": "ls"}}
    ]}
  })")));
  transcript.Append(
      proto::TranslateRecord(result("toolu_B", proto::kDeniedInstruction)));
  transcript.Append(proto::TranslateRecord(
      result("toolu_Q", proto::kQuestionDeclinedInstruction)));
  transcript.Append(proto::TranslateRecord(
      result("toolu_X", "Permission to use Bash has been denied.")));
  std::vector<std::wstring> results;
  for (const model::Block& block : transcript.blocks()) {
    if (block.kind == model::BlockKind::ToolResult) {
      results.push_back(block.summary);
    }
  }
  CHECK_EQ(results.size(), size_t{3});
  if (results.size() == 3) {
    CHECK_EQ(results[0], std::wstring(L"chyba: zamietnuté"));
    CHECK_EQ(results[1], std::wstring(L"chyba: bez odpovede"));
    CHECK(results[2].find(L"denied") != std::wstring::npos);
  }
}

void TestEditAndWriteSayWhatChanged() {
  TEST("transcript: Edit a Write hovoria zmenu, nie vypis poli");
  model::Transcript transcript;
  proto::Json calls = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "tool_use", "id": "toolu_E", "name": "Edit",
       "input": {"file_path": "src/x.cpp", "replace_all": false,
                 "old_string": "stary", "new_string": "novy\nriadok"}},
      {"type": "tool_use", "id": "toolu_W", "name": "Write",
       "input": {"file_path": "src/y.cpp", "content": "a\nb\nc"}}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(calls));
  const std::vector<model::Block>& blocks = transcript.blocks();
  CHECK_EQ(blocks.size(), size_t{2});

  // Zhrnutie povie velkost zmeny, aby sa kvoli nej nemuselo rozbalovat.
  CHECK_EQ(blocks[0].summary,
           std::wstring(L"Edit: src/x.cpp, 1 riadok na 2 riadky"));
  CHECK_EQ(blocks[1].summary, std::wstring(L"Write: src/y.cpp, 3 riadky"));

  // Telo je zmena, nie dump JSON poli.
  // Cesta je v nom cela -- dialog povolenia zhrnutie nema a bez nej by sa
  // pytal na zapis bez toho, aby povedal kam.
  CHECK_EQ(blocks[0].body,
           std::wstring(L"súbor: src/x.cpp\npôvodné:\nstary\nnové:\nnovy\n"
                        L"riadok"));
  CHECK(blocks[0].body.find(L"old_string") == std::wstring::npos);
  CHECK_EQ(blocks[1].body, std::wstring(L"súbor: src/y.cpp\nobsah:\na\nb\nc"));

  // Uspesny vysledok je jedno slovo, ktore nezopakuje cestu, a nie je co
  // rozbalovat -- povodna veta CLI bola dlhsia nez kInlineResultLimit, takze
  // sa zbalila do "vystup (1 riadok)".
  proto::Json results = proto::Json::parse(R"J({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_E",
       "content": "The file src/x.cpp has been updated successfully. (file state is current in your context - no need to Read it back)"},
      {"type": "tool_result", "tool_use_id": "toolu_W",
       "content": "File created successfully at: src/y.cpp"}
    ]}
  })J");
  transcript.Append(proto::TranslateRecord(results));
  CHECK_EQ(blocks.size(), size_t{4});
  CHECK_EQ(blocks[1].summary, std::wstring(L"zapísané"));
  CHECK_EQ(blocks[1].body, std::wstring(L"zapísané"));
  CHECK(!blocks[1].collapsible);
  CHECK_EQ(blocks[3].summary, std::wstring(L"vytvorené"));
  CHECK_EQ(blocks[3].toolName, std::wstring(L"Write"));

  // Ten isty prepis po anglicky: skladanie viet aj mnozne cislo idu cez
  // katalog, nie cez slovenske lepenie.
  i18n::SetLanguage(i18n::Lang::kEnglish);
  model::Transcript english;
  english.Append(proto::TranslateRecord(calls));
  english.Append(proto::TranslateRecord(results));
  const std::vector<model::Block>& en = english.blocks();
  CHECK_EQ(en[0].summary, std::wstring(L"Edit: src/x.cpp, 1 line to 2 lines"));
  CHECK_EQ(en[0].body, std::wstring(L"file: src/x.cpp\nbefore:\nstary\nafter:\n"
                                    L"novy\nriadok"));
  CHECK_EQ(en[2].body, std::wstring(L"file: src/y.cpp\ncontent:\na\nb\nc"));
  CHECK_EQ(en[1].summary, std::wstring(L"written"));
  CHECK_EQ(en[3].summary, std::wstring(L"created"));
  i18n::SetLanguage(i18n::Lang::kSlovak);

  // Bash sa nemeni: prikaz JE svojimi argumentmi a vystup je cely obsah.
  model::Transcript other;
  proto::Json bash = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "tool_use", "id": "toolu_B", "name": "Bash",
       "input": {"command": "echo ahoj"}}
    ]}
  })");
  other.Append(proto::TranslateRecord(bash));
  CHECK_EQ(other.blocks()[0].summary, std::wstring(L"Bash: echo ahoj"));
  CHECK_EQ(other.blocks()[0].body, std::wstring(L"command: echo ahoj"));
}

// Dialog povolenia ukazuje volanie skor, nez sa spusti, prepis ho drzi potom.
// Ked by to boli dva rozne texty, citatel povoli jedno a precita si druhe --
// preto to obe strany beru z RenderToolCall a preto to kontroluje test.
void TestPermissionTextMatchesTranscript() {
  TEST("povolenie: dialog ukazuje ten isty text ako prepis");
  const std::string command =
      "git commit -m \"prva veta\n\ndruhy odstavec\"";
  proto::Json input = proto::Json::object();
  input["command"] = command;
  input["description"] = "Run the requested git commit command";

  // To iste volanie, raz ako blok prepisu.
  proto::Json call = proto::Json::object();
  call["type"] = "assistant";
  call["message"]["content"] = proto::Json::array();
  proto::Json use = proto::Json::object();
  use["type"] = "tool_use";
  use["id"] = "toolu_C";
  use["name"] = "Bash";
  use["input"] = input;
  call["message"]["content"].push_back(use);

  model::Transcript transcript;
  transcript.Append(proto::TranslateRecord(call));
  CHECK_EQ(transcript.blocks().size(), size_t{1});
  // Volanie tak, ako ho dostane dialog: zo ziadosti o povolenie, nie z bloku.
  const agent::ToolCall asked =
      proto::ToolCallFromInput("Bash", "toolu_C", input);
  CHECK_EQ(model::RenderToolCall(asked), transcript.blocks()[0].body);

  // A hlavne: zlomy riadkov su zlomy riadkov.  MessageBox pred tymto krokom
  // ukazoval input.dump(2), kde je viacriadkova commit sprava jeden riadok
  // s "\n" v nom -- nahlas "spatna lomka en" a po riadkoch sa neda prejst.
  const std::wstring shown = model::RenderToolCall(asked);
  CHECK(shown.find(L"command: git commit") != std::wstring::npos);
  CHECK(shown.find(L"prva veta\n\ndruhy odstavec") != std::wstring::npos);
  CHECK(shown.find(L"\\n") == std::wstring::npos);
  CHECK(shown.find(L'\r') == std::wstring::npos);
}

void TestFailedToolResultReadsLikeAnError() {
  TEST("transcript: neuspesny nastroj povie, co sa pokazilo");
  model::Transcript transcript;
  proto::Json call = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "tool_use", "id": "toolu_E", "name": "Edit",
       "input": {"file_path": "src/x.cpp", "old_string": "a", "new_string": "b"}}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(call));

  proto::Json failed = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_E", "is_error": true,
       "content": "<tool_use_error>String to replace not found in file.\nString: a\nb</tool_use_error>"}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(failed));
  const model::Block& result = transcript.blocks()[1];
  CHECK(result.isError);
  // Chybny vysledok NEDOSTANE slovo "zapisane" -- ten nastroj nezapisal nic.
  CHECK(result.summary.rfind(L"chyba: ", 0) == 0);
  CHECK(result.summary.find(L"String to replace not found in file.") !=
        std::wstring::npos);
  // Protokolovy obal do prepisu nepatri, ani do zhrnutia, ani do tela.
  CHECK(result.summary.find(L"tool_use_error") == std::wstring::npos);
  CHECK(result.body.find(L"tool_use_error") == std::wstring::npos);
  CHECK_EQ(result.body.substr(0, 6), std::wstring(L"String"));

  // Obal sam o sebe staci: keby stream pole is_error nikdy neposlal, blok je
  // stale chyba a klavesa e ho najde.
  proto::Json noField = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_X",
       "content": "<tool_use_error>File has not been read yet.</tool_use_error>"}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(noField));
  const model::Block& second = transcript.blocks().back();
  CHECK(second.isError);
  CHECK_EQ(second.summary, std::wstring(L"chyba: File has not been read yet."));
  // Slovo "chyba" musi byt aj na riadku prepisu, nielen v reci: jednoriadkova
  // chyba je nezbalitelna, a kym sa kreslilo len telo, znela pri citani po
  // riadkoch rovnako ako uspesny vystup (claude-gui-lzv).
  const size_t last = transcript.blocks().size() - 1;
  CHECK_EQ(transcript.FirstLine(last),
           std::wstring(L"chyba: File has not been read yet."));

  // Uspesny jednoriadkovy vystup slovo nedostane -- bolo by pri kazdom prikaze.
  agent::ToolResult fine;
  fine.callId = "toolu_ok";
  fine.text = "hello";
  transcript.Append({agent::ToolCallFinished{fine}});
  CHECK_EQ(transcript.FirstLine(transcript.blocks().size() - 1),
           std::wstring(L"hello"));

  // Prazdna chyba nie je prazdny riadok.
  agent::ToolResult blank;
  blank.callId = "toolu_blank";
  blank.isError = true;
  transcript.Append({agent::ToolCallFinished{blank}});
  CHECK_EQ(transcript.FirstLine(transcript.blocks().size() - 1),
           std::wstring(L"chyba (prázdna)"));
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
  transcript.Append(proto::TranslateRecord(assistant));
  CHECK_EQ(transcript.blocks().size(), size_t{1});

  // Prazdny vysledok nastroja je naopak odpoved a zostava.
  proto::Json result = proto::Json::parse(R"({
    "type": "user",
    "message": {"content": [
      {"type": "tool_result", "tool_use_id": "toolu_1", "content": ""}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(result));
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
  // Meno dava backend (Capabilities::agentName), model ho sam nevie.
  CHECK(transcript.SetAgentName(L"claude"));
  transcript.AppendUserPrompt(L"otazka");
  // Po prvom bloku uz nie: prefix je v texte a mapa rozsahov by nesedela.
  CHECK(!transcript.SetAgentName(L"codex"));
  proto::Json assistant = proto::Json::parse(R"({
    "type": "assistant",
    "message": {"content": [
      {"type": "text", "text": "odpoved"},
      {"type": "tool_use", "id": "toolu_1", "name": "Read",
       "input": {"file_path": "a.txt"}}
    ]}
  })");
  transcript.Append(proto::TranslateRecord(assistant));

  CHECK_EQ(transcript.Text().substr(0, 4), std::wstring(L"ja: "));
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
  CHECK_EQ(transcript.Text().substr(0, 4), std::wstring(L"ja: "));

  // Codex sa v prepise aj v reci vola svojim menom, nie "claude".
  model::Transcript codex;
  CHECK(codex.SetAgentName(L"codex"));
  codex.Append({agent::AssistantText{"ahoj"}});
  CHECK_EQ(codex.Text(), std::wstring(L"codex: ahoj\n"));
  CHECK_EQ(codex.SpeakerPrefix(model::BlockKind::AssistantText),
           std::wstring(L"codex: "));
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
  transcript.Append(proto::TranslateRecord(assistant));
  for (const model::Block& block : transcript.blocks()) {
    CHECK(block.summary.find(L'\n') == std::wstring::npos);
  }
}

// -------------------------------------------------------------- 2. fixtury

void TestAskUserQuestionRoundTrip() {
  TEST("ask: otazka s moznostami je can_use_tool a odpoved ide v updatedInput");
  // Zaznam je odpozorovany naostro cez tools/probe_ask.py (2026-09-05), nie
  // vymysleny -- a prave to je na nom to podstatne. Navrh (claude-gui-lkk.15)
  // predpokladal subtyp 'request_user_dialog'; v skutocnosti pride obycajny
  // 'can_use_tool' pre nastroj AskUserQuestion, s 'requires_user_interaction'.
  proto::Json record = proto::Json::parse(R"J({
    "type": "control_request",
    "request_id": "574ca6a9-5914-4049-90c0-3dab76a80dcc",
    "request": {
      "subtype": "can_use_tool",
      "tool_name": "AskUserQuestion",
      "display_name": "AskUserQuestion",
      "input": {
        "questions": [
          {
            "question": "Čo piješ radšej?",
            "header": "Nápoj",
            "options": [
              {"label": "Čaj", "description": "Horúci čaj."},
              {"label": "Káva", "description": "Espresso alebo filtrovaná."},
              {"label": "Voda", "description": "Obyčajná alebo perlivá."}
            ],
            "multiSelect": false
          }
        ]
      },
      "tool_use_id": "toolu_01BtCsVpXnUEpU3vD2DbphcQ",
      "requires_user_interaction": true
    }
  })J");

  proto::PermissionRequest request;
  CHECK(proto::ParsePermissionRequest(record, &request));
  CHECK_EQ(request.toolName, std::string("AskUserQuestion"));
  CHECK(request.requiresUserInteraction);

  std::vector<proto::AskQuestion> questions;
  CHECK(proto::ParseAskUserQuestion(request.input, &questions));
  CHECK_EQ(questions.size(), size_t{1});
  CHECK_EQ(questions[0].question, std::string("Čo piješ radšej?"));
  CHECK_EQ(questions[0].header, std::string("Nápoj"));
  CHECK(!questions[0].multiSelect);
  CHECK_EQ(questions[0].options.size(), size_t{3});
  CHECK_EQ(questions[0].options[1].label, std::string("Káva"));
  CHECK_EQ(questions[0].options[2].description,
           std::string("Obyčajná alebo perlivá."));

  // Odpoved je klucovana TEXTOM OTAZKY, nie indexom ani hlavickou, a pre
  // jednovyberovu otazku je to retazec. Overene naostro: na tuto odpoved CLI
  // vratilo tool_result 'Your questions have been answered: ...="Čaj"'.
  const proto::Json updated = proto::MakeAskAnswers(
      request.input, questions, {{std::string("Čaj")}});
  CHECK_EQ(updated["answers"]["Čo piješ radšej?"].get<std::string>(),
           std::string("Čaj"));
  // updatedInput NAHRADZUJE argumenty nastroja, takze otazky v nom musia
  // zostat -- inak by sa volanie odoslalo bez toho, na co odpoveda.
  CHECK(updated.contains("questions"));
  CHECK_EQ(updated["questions"].size(), size_t{1});

  // Neoznacena otazka sa vynecha. Je to legalne: CLI vtedy povie modelu, nech
  // sa spyta znova. Dialog to nerobi -- Esc je zamietnutie celeho volania --
  // ale vrstva pod nim to musi zniest.
  const proto::Json none =
      proto::MakeAskAnswers(request.input, questions, {{}});
  CHECK(none["answers"].empty());

  // Viacnasobny vyber sa posiela ako pole, jednoduchy ako retazec. Rozdiel
  // nie je kozmeticky: validator CLI pole pri multiSelect=false neuzna a
  // vysledok potom znie inak.
  proto::Json multiInput = request.input;
  multiInput["questions"][0]["multiSelect"] = true;
  std::vector<proto::AskQuestion> multi;
  CHECK(proto::ParseAskUserQuestion(multiInput, &multi));
  CHECK(multi[0].multiSelect);
  const proto::Json both = proto::MakeAskAnswers(
      multiInput, multi, {{std::string("Čaj"), std::string("Voda")}});
  CHECK(both["answers"]["Čo piješ radšej?"].is_array());
  CHECK_EQ(both["answers"]["Čo piješ radšej?"].size(), size_t{2});

  // Co sa neda nakreslit, to sa neparsuje: otazka bez moznosti (kind "text"
  // a "number" su za prepinacom a na tomto streame sa nikdy neobjavili) by
  // znamenala dialog s prazdnym zoznamom. Vtedy je lepsi vseobecny prompt.
  std::vector<proto::AskQuestion> rejected;
  CHECK(!proto::ParseAskUserQuestion(
      proto::Json::parse(R"J({"questions": []})J"), &rejected));
  CHECK(!proto::ParseAskUserQuestion(
      proto::Json::parse(R"J({"questions": [{"question": "a?"}]})J"),
      &rejected));
  CHECK(!proto::ParseAskUserQuestion(proto::Json::parse(R"J({})J"), &rejected));

  // Bezny nastroj sa nezmenil: bez pola je priznak nepravdivy.
  proto::PermissionRequest bash;
  CHECK(proto::ParsePermissionRequest(
      proto::Json::parse(
          R"J({"type": "control_request", "request_id": "r1",
               "request": {"subtype": "can_use_tool", "tool_name": "Bash",
                           "input": {"command": "echo"}}})J"),
      &bash));
  CHECK(!bash.requiresUserInteraction);
}

void TestQuestionsReadAsText() {
  TEST("transcript: otazka s moznostami nie je v prepise dump JSON");
  model::Transcript transcript;
  proto::Json call = proto::Json::parse(R"J({
    "type": "assistant",
    "message": {"content": [
      {"type": "tool_use", "id": "toolu_Q", "name": "AskUserQuestion",
       "input": {"questions": [
         {"question": "Čo piješ radšej?", "header": "Nápoj",
          "multiSelect": false,
          "options": [{"label": "Čaj", "description": "Horúci."},
                      {"label": "Voda", "description": ""}]}
       ]}}
    ]}
  })J");
  transcript.Append(proto::TranslateRecord(call));
  const std::vector<model::Block>& blocks = transcript.blocks();
  CHECK_EQ(blocks.size(), size_t{1});

  // Zhrnutie je otazka, nie "{"questions":[{"question":...". Riadok, ktory
  // pocuje citatel pri prechode blokmi, musi povedat, na co sa islo pytat.
  CHECK_EQ(blocks[0].summary,
           std::wstring(L"AskUserQuestion: Čo piješ radšej?"));
  // Telo je otazka a jej moznosti pod nou -- prepis je jedine miesto, kde sa
  // da otazka precitat este raz, ked uz dialog nie je na obrazovke.
  CHECK_EQ(blocks[0].body,
           std::wstring(L"1. Čo piješ radšej?\n   Čaj — Horúci.\n   Voda"));
  CHECK(blocks[0].body.find(L"questions") == std::wstring::npos);
}

// Ktoru session vybere '-c'.  Su to testy prvej kategorie -- overuju MOJE
// pravidlo (najnovsia podla casu posledneho zaznamu, ktory cas nesie), nie
// format.  Zaznamy su preto minimalne, ale ich tvar vymysleny nie je: typy
// 'last-prompt' a 'atis-latch' bez pola 'timestamp' su odcitane zo skutocnych
// suborov v ~/.claude/projects (2026-09-06) a prave ony su dovod, preco sa
// nesmie triedit podla mtime.
void WriteFile(const std::filesystem::path& path, const std::string& text) {
  std::ofstream file(path, std::ios::binary);
  file << text;
}

void TestSessionPickIgnoresMtime() {
  TEST("sessions: najnovsia je podla casu zaznamu, nie podla mtime");
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "agentaloud-sessions-test";
  std::error_code code;
  fs::remove_all(root, code);
  const std::wstring project = L"C:\\demo\\projekt";
  const fs::path directory =
      root / "projects" / proto::ProjectKey(project);
  fs::create_directories(directory, code);
  CHECK(!code);
  // Cez CLAUDE_CONFIG_DIR, lebo ho CLI pozna a appka podla neho hlada -- takze
  // tento test overuje aj to.
  _wputenv_s(L"CLAUDE_CONFIG_DIR", root.wstring().c_str());

  // Starsia session, ale zapisana ako druha, takze ma NOVSI mtime.  Presne to
  // sa stane, ked sa dva rozhovory zavru tesne po sebe.
  WriteFile(directory / "novsia.jsonl",
            "{\"type\":\"user\",\"timestamp\":\"2026-09-02T09:00:00.000Z\","
            "\"message\":{\"content\":\"aku farbu ma obloha\"}}\n"
            "{\"type\":\"last-prompt\",\"lastPrompt\":\"aku farbu\"}\n");
  WriteFile(directory / "starsia.jsonl",
            "{\"type\":\"user\",\"timestamp\":\"2026-09-01T10:00:00.000Z\","
            "\"message\":{\"content\":\"nieco davne\"}}\n"
            "{\"type\":\"atis-latch\",\"atis\":{}}\n");
  // Nastavene rukou, nie ponechane na poradie zapisu: keby sa oba casy zmestili
  // do jedneho tiku hodin, test by presiel aj s triedenim podla mtime, cize
  // presne v pripade, ktory ma chytit.
  const auto now = fs::last_write_time(directory / "starsia.jsonl");
  fs::last_write_time(directory / "novsia.jsonl", now - std::chrono::hours(48));
  CHECK(fs::last_write_time(directory / "starsia.jsonl") >
        fs::last_write_time(directory / "novsia.jsonl"));

  proto::SessionSummary latest;
  CHECK(proto::LatestSession(project, &latest));
  CHECK_EQ(latest.id, std::wstring(L"novsia"));
  CHECK_EQ(latest.firstPrompt, std::string("aku farbu ma obloha"));
  CHECK_EQ(latest.lastStamp, std::string("2026-09-02T09:00:00.000Z"));

  // Projekt bez adresara nie je chyba, len nema co obnovit.
  CHECK(!proto::LatestSession(L"C:\\demo\\prazdny", &latest));

  fs::remove_all(root, code);
  _wputenv_s(L"CLAUDE_CONFIG_DIR", L"");
}

void TestSessionSummaryFindsTheHumanPrompt() {
  TEST("sessions: prvy prompt je to, co napisal clovek");
  namespace fs = std::filesystem;
  const fs::path file =
      fs::temp_directory_path() / "agentaloud-summary-test.jsonl";
  // Prve dva 'user' zaznamy nie su prompt: vypis slash prikazu (otvara sa
  // znackou) a vysledok nastroja (nema textovu cast).  Keby sa niektory z nich
  // ratal, session by sa v ohlaseni volala menom, ktore nikto nepovedal.
  WriteFile(file,
            "{\"type\":\"queue-operation\"}\n"
            "{\"type\":\"user\",\"timestamp\":\"2026-09-03T08:00:00.000Z\","
            "\"message\":{\"content\":[{\"type\":\"text\","
            "\"text\":\"<command-name>bd prime</command-name>\"}]}}\n"
            "{\"type\":\"user\",\"timestamp\":\"2026-09-03T08:00:01.000Z\","
            "\"message\":{\"content\":[{\"type\":\"tool_result\","
            "\"content\":\"vystup\"}]}}\n"
            "{\"type\":\"user\",\"timestamp\":\"2026-09-03T08:00:02.000Z\","
            "\"message\":{\"content\":\"prvy\\nriadok\\n\\n  a druhy\"}}\n"
            "{\"type\":\"assistant\",\"timestamp\":\"2026-09-03T08:00:03.000Z\","
            "\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"ano\"}]}}\n"
            "{\"type\":\"last-prompt\"}\n");
  proto::SessionSummary summary;
  CHECK(proto::ReadSessionSummary(file.wstring(), &summary));
  // Zlomy riadkov zliate: toto ide do jednej vety, ktora sa cita nahlas.
  CHECK_EQ(summary.firstPrompt, std::string("prvy riadok a druhy"));
  CHECK_EQ(summary.lastStamp, std::string("2026-09-03T08:00:03.000Z"));
  CHECK_EQ(summary.id, std::wstring(L"agentaloud-summary-test"));

  // Subor bez jedineho 'user' alebo 'assistant' zaznamu nie je rozhovor.
  WriteFile(file, "{\"type\":\"summary\",\"summary\":\"nic\"}\n");
  CHECK(!proto::ReadSessionSummary(file.wstring(), &summary));
  std::error_code code;
  std::filesystem::remove(file, code);
}

void TestHistoryRestore() {
  TEST("history: zo suboru na disku vzniknu tie iste bloky ako zo streamu");
  namespace fs = std::filesystem;
  const fs::path file =
      fs::temp_directory_path() / "agentaloud-history-test.jsonl";
  // Uroven 1: vstup je vymysleny, ale netestuje sa format -- ktore tvary na
  // disku su, je odmerane nad korpusom (191 suborov, 35 229 zaznamov) a stoji
  // v claude-gui-lkk.7.4.  Tu sa testuje delenie: co je prompt, co je
  // strojopis, co je prerusenie a co sa vyhodi este v proto/.
  WriteFile(
      file,
      // Typy, ktore ma disk navyse a stream ich nepozna.  Ked prejdu do
      // transkriptu, zaratuju sa ako neznamy zaznam -- a prave to je alarm,
      // ktory sleduje soak.  Musia sa odfiltrovat uz v proto/.
      "{\"type\":\"queue-operation\"}\n"
      "{\"type\":\"mode\",\"mode\":\"default\"}\n"
      // Periodicka sprava CLI o kontexte: isMeta, teda nie prompt.
      "{\"type\":\"user\",\"isMeta\":true,"
      "\"message\":{\"content\":\"## Context Usage\"}}\n"
      // Vypis slash prikazu: otvara sa znackou, teda tiez nie prompt.
      "{\"type\":\"user\",\"message\":{\"content\":[{\"type\":\"text\","
      "\"text\":\"<command-name>/bd</command-name>\"}]}}\n"
      "{\"type\":\"user\",\"message\":{\"content\":\"co robi tento subor\"}}\n"
      "{\"type\":\"assistant\",\"message\":{\"content\":[{"
      "\"type\":\"thinking\",\"thinking\":\"treba sa pozriet\"}]}}\n"
      "{\"type\":\"assistant\",\"message\":{\"content\":[{"
      "\"type\":\"text\",\"text\":\"Pozriem sa.\"}]}}\n"
      "{\"type\":\"assistant\",\"message\":{\"content\":[{"
      "\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"Bash\","
      "\"input\":{\"command\":\"ls\"}}]}}\n"
      "{\"type\":\"user\",\"message\":{\"content\":[{"
      "\"type\":\"tool_result\",\"tool_use_id\":\"toolu_1\","
      "\"content\":\"a.txt\"}]}}\n"
      // Znacka CLI, nie veta pouzivatela: ma z nej byt blok Interrupted, ten
      // isty, aky pise ziva cesta pri Esc.
      "{\"type\":\"user\",\"message\":{\"content\":"
      "\"[Request interrupted by user for tool use]\"}}\n"
      // Rozhovor subagenta.  Vlozeny medzi tieto by sa cital, akoby si Claude
      // odpovedal sam.
      "{\"type\":\"assistant\",\"isSidechain\":true,"
      "\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"subagent\"}]}}\n"
      "{\"type\":\"last-prompt\"}\n");

  std::vector<proto::Json> records;
  CHECK(proto::ReadSessionRecords(file.wstring(), &records));
  CHECK_EQ(records.size(), size_t{8});

  model::Transcript transcript;
  const model::HistoryCounts counts =
      model::RestoreHistory(proto::TranslateHistory(records), &transcript);
  CHECK_EQ(counts.prompts, size_t{1});
  CHECK_EQ(counts.blocks, size_t{6});

  std::string problem;
  CHECK(transcript.CheckInvariants(&problem));
  // Ziadny neznamy zaznam: disk ich ma vyse desat druhov a ani jeden sa
  // k transkriptu nedostal.
  CHECK_EQ(transcript.unknownCount(), size_t{0});

  const std::vector<model::Block>& blocks = transcript.blocks();
  CHECK_EQ(blocks.size(), size_t{6});
  CHECK(blocks[0].kind == model::BlockKind::UserPrompt);
  CHECK(blocks[1].kind == model::BlockKind::Thinking);
  CHECK(blocks[2].kind == model::BlockKind::AssistantText);
  CHECK(blocks[3].kind == model::BlockKind::ToolUse);
  // Za svojim volanim, nie na konci -- ta iste pravidlo ako naziva.
  CHECK(blocks[4].kind == model::BlockKind::ToolResult);
  CHECK_EQ(blocks[4].toolUseId, std::string("toolu_1"));
  CHECK(blocks[5].kind == model::BlockKind::Interrupted);
  CHECK_EQ(blocks[0].body, std::wstring(L"co robi tento subor"));

  // Subor bez jedineho zaznamu, z ktoreho by bol blok, nie je historia:
  // prazdny prepis potom nie je obnovena session, len prazdna.
  WriteFile(file, "{\"type\":\"summary\",\"summary\":\"nic\"}\n");
  CHECK(!proto::ReadSessionRecords(file.wstring(), &records));
  CHECK(!proto::ReadSessionRecords(L"C:\\demo\\niet-taketo.jsonl", &records));

  std::error_code code;
  fs::remove(file, code);
}

void TestSessionFilePath() {
  TEST("sessions: cesta k suboru rozhovoru");
  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "agentaloud-path-test";
  _wputenv_s(L"CLAUDE_CONFIG_DIR", root.wstring().c_str());
  CHECK_EQ(proto::SessionFilePath(L"C:\\b\\mluv", L"abc-123"),
           (root / "projects" / "C--b-mluv" / "abc-123.jsonl").wstring());
  // --resume berie aj titul session, nie len id.  Cesta z titulu je cesta,
  // ktora neexistuje, a to je odpoved volajuceho, nie chyba; prazdne id vsak
  // cestu nema vobec, inak by z neho vznikol adresar s ".jsonl" na konci.
  CHECK(proto::SessionFilePath(L"C:\\b\\mluv", L"").empty());
  _wputenv_s(L"CLAUDE_CONFIG_DIR", L"");
}

void TestProjectKeyAndTime() {
  TEST("sessions: meno adresara projektu a cas do vety");
  // Overene na skutocnom adresari: C:\vcs\github.com\lpintes\claude-gui ->
  // C--vcs-github-com-lpintes-claude-gui.
  CHECK_EQ(proto::ProjectKey(L"C:\\vcs\\github.com\\lpintes\\claude-gui"),
           std::wstring(L"C--vcs-github-com-lpintes-claude-gui"));
  // Lomka na konci by pridala pomlcku a poslala hladanie do neexistujuceho
  // adresara; koren si ju necha, lebo "C:" bez nej nie je cesta.
  CHECK_EQ(proto::ProjectKey(L"C:\\b\\mluv\\"), std::wstring(L"C--b-mluv"));
  CHECK_EQ(proto::ProjectKey(L"C:\\"), std::wstring(L"C--"));

  _putenv_s("TZ", "UTC0");
  _tzset();
  CHECK_EQ(proto::LocalTimeText("2026-09-06T12:34:56.789Z"),
           std::wstring(L"6. 9. 2026 12:34"));
  // Nezrozumitelny cas sa vrati tak, ako prisiel: prazdno na mieste casu by
  // vyzeralo ako session bez casu, nie ako pokazeny zaznam.
  CHECK_EQ(proto::LocalTimeText("neskoro"), std::wstring(L"neskoro"));
}

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

  // Kazdy assistant zaznam skutocneho streamu menuje model, ktory ho napisal
  // -- z toho zije model v stavovom riadku (claude-gui-lkk.40). Keby CLI
  // prestalo posielat parent_tool_use_id: null alebo message.model, zistilo by
  // sa to tu, nie tak, ze bar potichu zostane na system/init.
  size_t assistants = 0;
  size_t named = 0;
  for (const proto::Json& record : records) {
    if (record.value("type", std::string()) != "assistant") continue;
    ++assistants;
    std::string answered;
    if (proto::ParseAnsweringModel(record, &answered)) ++named;
  }
  CHECK(assistants > 0);
  CHECK_EQ(named, assistants);

  model::Transcript transcript;
  std::string problem;
  if (!Replay(records, &transcript, &problem)) {
    Fail(__FILE__, __LINE__, "invariant: " + problem);
    return;
  }

  const auto counts = CountKinds(transcript);
  CHECK(counts.count(model::BlockKind::AssistantText) > 0);
  CHECK(counts.count(model::BlockKind::ToolUse) > 0);
  CHECK(counts.count(model::BlockKind::ToolResult) > 0);
  // Thinking sa tu NEOCAKAVA, a nie je to zlava.  CLI od verzie 2.1.260
  // posiela bloky `thinking` s prazdnym textom -- ostane z nich len podpis --
  // takze Transcript z nich blok nespravi a spravit ani nema.  Fixtura, ktora
  // premyslanie este nesie, je thinking.jsonl a je zamrznuta; testuje ju
  // TestFixtureThinking.  Podrobnosti su v claude-gui-lkk.35.

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

void TestFixtureThinking(const std::string& dir) {
  TEST("fixtura thinking: blok premyslania s obsahom, zamrznuty");
  // ZAMRZNUTA FIXTURA, a jedina taka.  make_fixtures.py ju nikdy nezapisuje
  // a nesmie sa pregenerovat, lebo pregenerovat sa uz neda: CLI prestalo
  // posielat text premyslania.  Odmerane nad korpusom -- 197 suborov, cez
  // 6000 casti `thinking`, z toho text ma 32 a vsetkych 32 je z CLI 2.1.258
  // a modelu haiku (2. 9. 2026); na 2.1.260 a 2.1.263 uz ma haiku nulu, a
  // opus a sonnet nemali text ani raz, na ziadnej verzii.
  //
  // Preco to teda v repozitari zostava: `MakeThinking` a vykreslenie bloku
  // premyslania v appke stale su, a toto je jediny skutocny zaznam, na ktorom
  // sa daju overit.  Je to dokaz o tvare, nie zaruka, ze sa ten tvar vrati.
  bool ok = false;
  const auto records = ReadJsonl(dir + "/thinking.jsonl", &ok);
  if (!ok) {
    Fail(__FILE__, __LINE__,
         "chyba " + dir + "/thinking.jsonl -- NEGENERUJE sa, je zamrznuta; "
         "vrat ju z gitu");
    return;
  }

  model::Transcript transcript;
  std::string problem;
  if (!Replay(records, &transcript, &problem)) {
    Fail(__FILE__, __LINE__, "invariant: " + problem);
    return;
  }

  const auto counts = CountKinds(transcript);
  CHECK(counts.count(model::BlockKind::Thinking) > 0);
  // A s obsahom.  Prave prazdny text je to, co sa zmenilo, takze fixtura,
  // z ktorej by ostali same prazdne bloky, by uz nedokazovala nic.
  for (const model::Block& block : transcript.blocks()) {
    if (block.kind == model::BlockKind::Thinking) {
      CHECK(!block.body.empty());
    }
  }
}

void TestFixtureDisk(const std::string& dir) {
  TEST("fixtura disk: historia zo skutocneho suboru session");
  namespace fs = std::filesystem;
  const fs::path file = fs::path(dir) / "disk.jsonl";

  // Najprv surovo, aby bolo dokazane, ze je to naozaj DISKOVY subor a nie
  // odlozena kopia streamu: format disku ma vlastne typy zaznamov a prave
  // tie ma proto/ odfiltrovat.  Bez tejto kontroly by test presiel aj nad
  // suborom, v ktorom niet co filtrovat, a nedokazoval by nic.
  bool ok = false;
  const auto raw = ReadJsonl(file.string(), &ok);
  if (!ok || raw.empty()) {
    Fail(__FILE__, __LINE__,
         "chyba " + dir + "/disk.jsonl -- spusti tools/make_fixtures.py");
    return;
  }
  size_t diskOnly = 0;
  for (const proto::Json& record : raw) {
    const std::string type = record.value("type", std::string());
    if (type != "user" && type != "assistant") ++diskOnly;
  }
  CHECK(diskOnly > 0);

  std::vector<proto::Json> records;
  CHECK(proto::ReadSessionRecords(file.wstring(), &records));
  CHECK(records.size() < raw.size());
  CHECK(!records.empty());

  model::Transcript transcript;
  const model::HistoryCounts counts =
      model::RestoreHistory(proto::TranslateHistory(records), &transcript);
  // Prave tolko, kolko je promptov v TURNS (tools/make_fixtures.py).  Disk
  // pise do `user` zaznamov aj vysledky nastrojov a vlastne hlasky CLI, takze
  // toto cislo je kontrola, ze HumanPromptText ich odlisil na skutocnom
  // subore, nie len na vymyslenom v TestHistoryRestore.
  CHECK_EQ(counts.prompts, size_t{4});
  CHECK(counts.blocks > 0);

  std::string problem;
  CHECK(transcript.CheckInvariants(&problem));
  // Ziadny neznamy typ: co disk ma navyse, ostalo v proto/.  Keby to prislo
  // az sem, zvonil by alarm soaku pre uplne bezny pripad.
  CHECK_EQ(transcript.unknownCount(), size_t{0});

  const auto kinds = CountKinds(transcript);
  CHECK(kinds.count(model::BlockKind::UserPrompt) > 0);
  CHECK(kinds.count(model::BlockKind::AssistantText) > 0);
  CHECK(kinds.count(model::BlockKind::ToolUse) > 0);
  CHECK(kinds.count(model::BlockKind::ToolResult) > 0);

  // Vysledok za svojim volanim aj po prehrati z disku -- na disku su tie dva
  // zaznamy susedne len nahodou a vazbu drzi id, nie poradie.
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

  // Prvy prompt je prvy z TURNS a je to text, ktory napisal clovek -- nie
  // strojopis, ktory CLI zapisuje do tych istych zaznamov.
  for (const model::Block& block : transcript.blocks()) {
    if (block.kind == model::BlockKind::UserPrompt) {
      CHECK_EQ(block.body,
               std::wstring(L"Odpovedz jednou vetou: na co je subor "
                            L"README.md?"));
      break;
    }
  }

  // A este raz cez zhrnutie, ktorym sa vyberá najnovsia session: cas sa vo
  // fixture nahradzuje stabilnym, ale PLATNYM ISO 8601 prave preto, aby sa
  // tato cesta dala testovat nad realnym suborom (invariant 15).
  proto::SessionSummary summary;
  CHECK(proto::ReadSessionSummary(file.wstring(), &summary));
  CHECK(!summary.lastStamp.empty());
  CHECK_EQ(summary.firstPrompt,
           std::string("Odpovedz jednou vetou: na co je subor README.md?"));
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
    "pr-link",
};

void SoakOverCorpus(const std::string& root) {
  TEST("soak: sukromny korpus");
  std::printf("\n  soak nad %s\n", root.c_str());

  // Ziadny <filesystem>: staci nam zoznam suborov, ktory si necha dodat
  // volajuci cez AGENTALOUD_CORPUS_LIST, alebo ho vyrobi shell.
  std::ifstream list(root);
  if (!list) {
    Fail(__FILE__, __LINE__, "nedá sa otvorit zoznam suborov: " + root);
    return;
  }

  std::set<std::string> unknown;
  size_t files = 0;
  size_t blocks = 0;
  size_t restoredFiles = 0;
  size_t restoredBlocks = 0;
  size_t restoredPrompts = 0;
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
    // Ziadny riadiaci znak v bufferi -- nad skutocnymi datami, nie nad
    // vymyslenym vstupom. NUL by widgetu utrhol zvysok bloku (viz
    // EscapeControls), zvysok by NVDA precitala ako nic. Hlasi sa prvy vyskyt
    // a subor, lebo tie dva udaje staci na to, aby sa dal najst.
    for (wchar_t character : transcript.Text()) {
      if (character == L'\n' || character == L'\t') continue;
      if (character >= 0x20 && character != 0x7F) continue;
      char detail[64] = {};
      std::snprintf(detail, sizeof(detail), ": riadiaci znak 0x%02X v prepise",
                    static_cast<unsigned>(character));
      Fail(__FILE__, __LINE__, path + detail);
      break;
    }
    for (const std::string& type : transcript.unknownTypes()) {
      bool known = false;
      for (const char* candidate : kKnownDiskOnlyTypes) {
        if (type == candidate) known = true;
      }
      if (!known) unknown.insert(type);
    }

    // Druhy priechod, a je to ina otazka nez ten prvy.  Vyssie sa subor cita
    // tak, akoby prisiel po drote -- tym sa najdu nezname typy zaznamov.  Tu
    // sa cita tak, ako ho cita obnovenie session: cez proto::
    // ReadSessionRecords a model::RestoreHistory, cize aj s promptami, ktore
    // ziva cesta do transkriptu nedava.  Tvrdi sa to iste, co pri kazdej
    // davke zo streamu -- ze mapa rozsahov sedi a ze v bufferi nie je riadiaci
    // znak -- lebo prave to je jedine, co sa nad cudzimi datami tvrdit da.
    std::vector<proto::Json> shared;
    if (!proto::ReadSessionRecords(model::Utf16FromUtf8(path), &shared)) {
      continue;
    }
    model::Transcript restored;
    const model::HistoryCounts counts =
        model::RestoreHistory(proto::TranslateHistory(shared), &restored);
    ++restoredFiles;
    restoredBlocks += counts.blocks;
    restoredPrompts += counts.prompts;
    if (!restored.CheckInvariants(&problem)) {
      Fail(__FILE__, __LINE__, path + " (obnovenie): " + problem);
      continue;
    }
    // Ziadny neznamy zaznam: proto/ ma zo suboru prepustit prave tie typy,
    // z ktorych transkript vie robit bloky, a nic ine.
    if (restored.unknownCount() != 0) {
      Fail(__FILE__, __LINE__, path + " (obnovenie): neznamy zaznam presiel");
    }
    for (wchar_t character : restored.Text()) {
      if (character == L'\n' || character == L'\t') continue;
      if (character >= 0x20 && character != 0x7F) continue;
      char detail[80] = {};
      std::snprintf(detail, sizeof(detail),
                    " (obnovenie): riadiaci znak 0x%02X v prepise",
                    static_cast<unsigned>(character));
      Fail(__FILE__, __LINE__, path + detail);
      break;
    }
  }

  std::printf("  %zu suborov, %zu blokov\n", files, blocks);
  std::printf("  obnovenie: %zu suborov, %zu blokov, %zu promptov\n",
              restoredFiles, restoredBlocks, restoredPrompts);
  CHECK(files > 0);
  CHECK(restoredFiles > 0);
  // Prompty su prave to, co obnovenie vie a ziva cesta zo streamu nie -- keby
  // ich nebolo ani jeden, filter na strojopis by bral vsetko.
  CHECK(restoredPrompts > 0);
  if (!unknown.empty()) {
    std::string types;
    for (const std::string& type : unknown) types += " " + type;
    Fail(__FILE__, __LINE__, "neocakavane typy zaznamov:" + types);
  }
}

}  // namespace

// ------------------------------------------------------------- Codex

void TestCodexModes() {
  TEST("codex: rezimy su predvolby nad approval x sandbox x plan");
  using namespace proto::codex;
  const agent::Capabilities capabilities = CodexCapabilities();
  // Kazda predvolba sa da nastavit a z nastavenia sa precita spat ako ona.
  // Tvar sandboxPolicy je ten, ktory vracia thread/settings/updated
  // (odmerane, c:/b/codex-probe/logs/noturn.log).
  for (const char* mode :
       {kModeDefault, kModeAuto, kModeReadOnly, kModeFullAccess}) {
    ModeSettings settings;
    CHECK(SettingsForMode(mode, &settings));
    CHECK(settings.permissions);
    CHECK_EQ(ModeFromSettings(settings.approvalPolicy, settings.sandboxPolicy,
                              settings.reviewer, settings.collaboration),
             std::string(mode));
    CHECK(agent::FindMode(capabilities, mode) != nullptr);
    // Recenzent sa posiela vzdy: vynechany zostane z predosleho rezimu
    // (odmerane, never-settings.log), takze odchod z auto by ho vzal so sebou.
    CHECK(!settings.reviewer.empty());
  }
  // Auto sa od normalneho lisi len recenzentom.
  ModeSettings normal, automatic;
  CHECK(SettingsForMode(kModeDefault, &normal));
  CHECK(SettingsForMode(kModeAuto, &automatic));
  CHECK_EQ(normal.reviewer, std::string("user"));
  CHECK_EQ(automatic.reviewer, std::string("auto_review"));
  CHECK_EQ(automatic.approvalPolicy, normal.approvalPolicy);
  CHECK_EQ(automatic.sandbox, normal.sandbox);
  // Nehlaseny recenzent je predvoleny "user" -- tak to hovori schema.
  CHECK_EQ(ModeFromSettings("on-request", {{"type", "workspaceWrite"}}, "",
                            "default"),
           std::string(kModeDefault));
  // Recenzent pri read-only nie je ziadna predvolba.
  CHECK_EQ(ModeFromSettings("on-request", {{"type", "readOnly"}}, "auto_review",
                            "default"),
           std::string(kModeCustom));
  ModeSettings plan;
  CHECK(SettingsForMode(kModePlan, &plan));
  CHECK(!plan.permissions);
  CHECK_EQ(plan.collaboration, std::string("plan"));
  // Plan prebije ostatne osi: je to sposob prace, nie povolenie.
  CHECK_EQ(ModeFromSettings("never", {{"type", "dangerFullAccess"}}, "user",
                            "plan"),
           std::string(kModePlan));
  // Sondy bezali v untrusted + read-only, co ziadna predvolba nie je.
  CHECK_EQ(ModeFromSettings("untrusted", {{"type", "readOnly"}}, "user",
                            "default"),
           std::string(kModeCustom));
  ModeSettings custom;
  CHECK(!SettingsForMode(kModeCustom, &custom));
  CHECK(agent::FindMode(capabilities, kModeCustom) != nullptr);

  // Shift+Tab: normalny -> auto -> planovanie -> len citanie -> normalny.
  // Plny pristup v cykle nie je a z neho sa ide na druhy rezim cyklu.
  CHECK_EQ(agent::NextMode(capabilities, kModeDefault),
           std::string(kModeAuto));
  CHECK_EQ(agent::NextMode(capabilities, kModeAuto), std::string(kModePlan));
  CHECK_EQ(agent::NextMode(capabilities, kModePlan),
           std::string(kModeReadOnly));
  CHECK_EQ(agent::NextMode(capabilities, kModeReadOnly),
           std::string(kModeDefault));
  CHECK_EQ(agent::NextMode(capabilities, kModeFullAccess),
           std::string(kModeAuto));

  // Poradie sprav pri zmene: odpoved {} pride PRED thread/settings/updated
  // (odmerane, noturn.log).  Dve rychle stlacenia nesmu cuvnut na hlasenie,
  // ktore je starsie nez posledne stlacenie.
  ModeTracker tracker;
  tracker.Seed(kModeAuto);
  tracker.Reported(kModeAuto);
  tracker.Requested(kModePlan);
  tracker.Requested(kModeReadOnly);
  tracker.Answered(true);
  tracker.Reported(kModePlan);  // hlasenie k prvemu stlaceniu
  CHECK_EQ(tracker.current(), std::string(kModeReadOnly));
  tracker.Answered(true);
  tracker.Reported(kModeReadOnly);
  CHECK_EQ(tracker.current(), std::string(kModeReadOnly));
  // Odmietnutie vrati posledne slovo servera, nie rezim pred stlacenim.
  tracker.Requested(kModeAuto);
  tracker.Answered(false);
  CHECK_EQ(tracker.current(), std::string(kModeReadOnly));
  // Zmena, ktoru nikto nepytal (zaciatok tahu so starym nastavenim), plati.
  tracker.Reported(kModeFullAccess);
  CHECK_EQ(tracker.current(), std::string(kModeFullAccess));
}

// Prehra fixturu Codexu cez jeden prekladac a po kazdej sprave skontroluje
// invarianty prepisu.  `onMessage` dostane kazdu spravu este pred prekladom
// -- teda vtedy, ked sa adapter na serverovu poziadavku pozera.
struct CodexReplay {
  std::vector<proto::Json> messages;
  std::vector<agent::Event> events;
  model::Transcript transcript;
  proto::codex::Translator translator;
};

bool ReplayCodex(const std::string& path, CodexReplay* replay,
                 const std::function<void(const proto::Json&,
                                          const proto::codex::Translator&)>&
                     onMessage = nullptr) {
  bool ok = false;
  replay->messages = ReadJsonl(path, &ok);
  if (!ok) {
    Fail(__FILE__, __LINE__,
         "chyba " + path + " -- spusti tools/make_codex_fixtures.py");
    return false;
  }
  for (const proto::Json& message : replay->messages) {
    if (onMessage) onMessage(message, replay->translator);
    std::vector<agent::Event> batch = replay->translator.Translate(message);
    replay->events.insert(replay->events.end(), batch.begin(), batch.end());
    replay->transcript.Append(batch);
    std::string problem;
    if (!replay->transcript.CheckInvariants(&problem)) {
      Fail(__FILE__, __LINE__, "invariant: " + problem);
      return false;
    }
  }
  if (replay->transcript.unknownCount() != 0) {
    std::string types;
    for (const std::string& type : replay->transcript.unknownTypes()) {
      types += " " + type;
    }
    Fail(__FILE__, __LINE__, path + ": nezname spravy:" + types);
  }
  return true;
}

template <typename T>
std::vector<T> EventsOf(const std::vector<agent::Event>& events) {
  std::vector<T> out;
  for (const agent::Event& event : events) {
    if (const T* found = std::get_if<T>(&event)) out.push_back(*found);
  }
  return out;
}

// Kazde volanie v prepise ma svoj vysledok -- okrem tych, ktore su v `open`.
void CheckCallsHaveResults(const model::Transcript& transcript,
                           size_t open = 0) {
  std::set<std::string> used;
  std::set<std::string> resulted;
  for (const model::Block& block : transcript.blocks()) {
    if (block.kind == model::BlockKind::ToolUse) used.insert(block.toolUseId);
    if (block.kind == model::BlockKind::ToolResult) {
      resulted.insert(block.toolUseId);
    }
  }
  CHECK(!used.empty());
  CHECK_EQ(used.size(), resulted.size() + open);
  for (const std::string& id : resulted) CHECK(used.count(id) == 1);
}

void TestCodexFixtureMulti(const std::string& dir) {
  TEST("codex fixtura multi: prikazy, chyba, zamietnutie, medzitext");
  CodexReplay replay;
  size_t approvals = 0;
  if (!ReplayCodex(dir + "/codex-multi.jsonl", &replay,
                   [&](const proto::Json& message,
                       const proto::codex::Translator& translator) {
                     if (proto::codex::KindOf(message) !=
                         proto::codex::MessageKind::ServerRequest) {
                       return;
                     }
                     ++approvals;
                     // Povolenie na prikaz menuje polozku; volanie z nej
                     // poskladal item/started, ktory prisiel tesne pred nim.
                     agent::ToolCall call;
                     CHECK(translator.FindCall(
                         message["params"]["itemId"].get<std::string>(),
                         &call));
                     CHECK(call.kind == agent::ToolKind::Shell);
                   })) {
    return;
  }
  CHECK_EQ(approvals, size_t{3});

  // Citatelny prikaz, nie obal pwsh -Command '...'.
  const auto calls = EventsOf<agent::ToolCallStarted>(replay.events);
  CHECK_EQ(calls.size(), size_t{3});
  CHECK_EQ(calls[1].call.primary, std::string("git log"));
  CHECK_EQ(calls[1].call.name, std::string("shell"));

  const auto results = EventsOf<agent::ToolCallFinished>(replay.events);
  CHECK_EQ(results.size(), size_t{3});
  CHECK(!results[0].result.isError);
  CHECK_EQ(results[0].result.text, std::string("prvy\r\ndruhy"));
  CHECK(results[1].result.isError);  // fatal: not a git repository
  CHECK(results[2].result.isError);  // whoami, zamietnute
  CHECK_EQ(results[2].result.text, std::string("zamietnuté"));

  // Oba druhy textu asistenta: medzitext aj odpoved, v poradi prichodu.
  CHECK(EventsOf<agent::AssistantText>(replay.events).size() >= 2);
  CheckCallsHaveResults(replay.transcript);

  // Model a kontext, ktore panel ukazuje.
  const auto models = EventsOf<agent::ModelChanged>(replay.events);
  CHECK_EQ(models.size(), size_t{1});
  if (!models.empty()) CHECK_EQ(models[0].model, std::string("gpt-6-luna"));
  const auto context = EventsOf<agent::ContextUsed>(replay.events);
  CHECK(!context.empty());
  if (!context.empty()) CHECK_EQ(context.back().window, 258400LL);
  const auto limits = EventsOf<agent::RateLimitChanged>(replay.events);
  CHECK(!limits.empty());
  if (!limits.empty()) {
    CHECK_EQ(limits.back().windows.size(), size_t{1});
    CHECK_EQ(limits.back().windows[0].minutes, 43200);
  }
  const auto ends = EventsOf<agent::TurnEnded>(replay.events);
  CHECK_EQ(ends.size(), size_t{1});
  if (!ends.empty()) {
    CHECK(ends[0].outcome == agent::TurnOutcome::Completed);
  }
}

void TestCodexFixtureEdit(const std::string& dir) {
  TEST("codex fixtura edit: povolenie suboru bez diffu sa sparuje s polozkou");
  CodexReplay replay;
  bool paired = false;
  if (!ReplayCodex(dir + "/codex-edit.jsonl", &replay,
                   [&](const proto::Json& message,
                       const proto::codex::Translator& translator) {
                     if (message.value("method", std::string()) !=
                         "item/fileChange/requestApproval") {
                       return;
                     }
                     // Ziadost obsah nenesie -- len itemId.
                     CHECK(!message["params"].contains("changes"));
                     agent::ToolCall call;
                     paired = translator.FindCall(
                         message["params"]["itemId"].get<std::string>(), &call);
                     CHECK(call.kind == agent::ToolKind::CreateFile);
                     CHECK(call.newContent.has_value());
                     if (call.newContent) {
                       CHECK_EQ(*call.newContent,
                                std::string("Čaj je lepší než káva.\n"));
                     }
                     CHECK(call.primaryIsPath);
                   })) {
    return;
  }
  CHECK(paired);
  CheckCallsHaveResults(replay.transcript);
  // Jeden riadok s koncovym zlomom je jeden riadok, nie dva.
  bool oneLine = false;
  for (const model::Block& block : replay.transcript.blocks()) {
    if (block.kind == model::BlockKind::ToolUse) {
      oneLine = block.summary.ends_with(L"caj.txt, 1 riadok");
      CHECK(block.body.find(L"súbor: ") == 0);
    }
  }
  CHECK(oneLine);
  // Vytvoreny subor ma za sebou jedno slovo, nie prazdny vystup.
  bool done = false;
  for (const model::Block& block : replay.transcript.blocks()) {
    if (block.kind == model::BlockKind::ToolResult) {
      done = block.summary == L"vytvorené";
    }
  }
  CHECK(done);
}

// Tvary doslova z tools/probe_permission_session.py (CLI 2.1.288, 6. 10.
// 2026, claude-gui-lkk.61): navrhy idu do localSettings, my ich posielame
// spat ako session -- tak platia a nic sa nezapise.
void TestClaudeSessionPermissions() {
  TEST("claude povolenie na session: navrhy CLI s destination session");
  const proto::Json record = proto::Json::parse(R"J(
    {"type":"control_request","request_id":"r1","request":{
      "subtype":"can_use_tool","tool_name":"Bash","display_name":"Bash",
      "input":{"command":"ping -n 1 127.0.0.1"},
      "permission_suggestions":[{"type":"addRules",
        "rules":[{"toolName":"Bash","ruleContent":"ping -n 1 127.0.0.1"}],
        "behavior":"allow","destination":"localSettings"}],
      "decision_reason_type":"other","tool_use_id":"t1"}})J");
  proto::PermissionRequest request;
  CHECK(proto::ParsePermissionRequest(record, &request));
  const proto::Json kept = proto::SessionPermissions(request.suggestions);
  CHECK_EQ(kept, proto::Json::parse(R"J([{"type":"addRules",
        "rules":[{"toolName":"Bash","ruleContent":"ping -n 1 127.0.0.1"}],
        "behavior":"allow","destination":"session"}])J"));
  const proto::Json allow = proto::MakeAllow(request, nullptr, kept);
  CHECK_EQ(allow["response"]["response"]["updatedPermissions"], kept);
  CHECK(!proto::MakeAllow(request, nullptr)["response"]["response"].contains(
      "updatedPermissions"));

  // Ask pravidlo: navrhy nechodia, tlacidlo na session sa neponukne.
  const proto::Json rule = proto::Json::parse(R"J(
    {"type":"control_request","request_id":"r2","request":{
      "subtype":"can_use_tool","tool_name":"Bash","display_name":"Bash",
      "input":{"command":"echo hello"},"decision_reason_type":"rule",
      "tool_use_id":"t2"}})J");
  CHECK(proto::ParsePermissionRequest(rule, &request));
  CHECK(proto::SessionPermissions(request.suggestions).is_null());
}

// Nie fixtura: computer use sa na tento stroj bez Codex Desktop nedostane.
// Tvary su doslova zo sondy s nahradnym MCP serverom
// (tools/probe_computer_use.py, 6. 10. 2026, claude-gui-lkk.44.12).
void TestCodexElicitation() {
  TEST("codex elicitacia: ano/nie je povolenie, formular s polami nie");
  const proto::Json codexAsks = proto::Json::parse(
      R"({"threadId":"t","turnId":"u","serverName":"fakecu","mode":"form",
          "_meta":{"codex_approval_kind":"mcp_tool_call",
                   "persist":["session","always"],
                   "tool_description":"Computer use: open a desktop application.",
                   "tool_params":{"app":"Notepad"},
                   "tool_params_display":[{"name":"app","value":"Notepad",
                                           "display_name":"app"}]},
          "message":"Allow the fakecu MCP server to run tool \"open_app\"?",
          "requestedSchema":{"type":"object","properties":{}}})");
  agent::PermissionRequest request;
  CHECK(proto::codex::ElicitationPermission(codexAsks, &request));
  CHECK_EQ(request.call.name, std::string("fakecu"));
  CHECK_EQ(request.call.fields.size(), size_t{1});
  if (!request.call.fields.empty()) {
    CHECK_EQ(request.call.fields[0].name, std::string("app"));
    CHECK_EQ(request.call.fields[0].value, std::string("Notepad"));
  }
  CHECK_EQ(request.reason,
           std::string("Allow the fakecu MCP server to run tool \"open_app\"?"));
  CHECK(!request.description.empty());
  // Codex ponuka "session" aj "always"; "always" by zapisalo do config.toml.
  CHECK(request.offered ==
        (std::vector<agent::Verdict>{agent::Verdict::Allow,
                                     agent::Verdict::AllowForSession,
                                     agent::Verdict::Deny}));

  // Otazka samotneho pluginu: _meta null, ziadne argumenty.  Prave tuto
  // appka odmietala a plugin z toho hlasil "not approved".
  const proto::Json pluginAsks = proto::Json::parse(
      R"({"threadId":"t","turnId":"u","serverName":"fakecu","mode":"form",
          "_meta":null,"message":"Allow Codex to use Notepad?",
          "requestedSchema":{"type":"object","properties":{}}})");
  CHECK(proto::codex::ElicitationPermission(pluginAsks, &request));
  CHECK_EQ(request.reason, std::string("Allow Codex to use Notepad?"));
  CHECK(request.call.fields.empty());
  CHECK(request.offered == (std::vector<agent::Verdict>{agent::Verdict::Allow,
                                                        agent::Verdict::Deny}));

  proto::Json form = pluginAsks;
  form["requestedSchema"]["properties"] = {{"name", {{"type", "string"}}}};
  CHECK(!proto::codex::ElicitationPermission(form, &request));
  const proto::Json url = proto::Json::parse(
      R"({"threadId":"t","turnId":null,"serverName":"s","mode":"url",
          "_meta":null,"message":"Sign in","url":"https://example.com",
          "elicitationId":"e"})");
  CHECK(!proto::codex::ElicitationPermission(url, &request));

  CHECK_EQ(proto::codex::MakeElicitationAnswer(agent::Verdict::Allow),
           proto::Json::parse(R"({"action":"accept","content":{},"_meta":null})"));
  CHECK_EQ(
      proto::codex::MakeElicitationAnswer(agent::Verdict::AllowForSession),
      proto::Json::parse(
          R"({"action":"accept","content":{},"_meta":{"persist":"session"}})"));
  CHECK_EQ(proto::codex::MakeElicitationAnswer(agent::Verdict::Deny),
           proto::Json::parse(
               R"({"action":"decline","content":null,"_meta":null})"));
}

// Nie fixtura: zamietnutie recenzentom sa da vyvolat len politikou
// recenzenta, ktora zamietne vsetko.  Tvary su skratene, ale doslova zo
// sondy (tools/probe_codex_never.py, never-review-deny-1.log, 7. 10. 2026).
void TestCodexReviewerDenial() {
  TEST("codex auto: zamietnutie recenzentom povie kto a preco");
  proto::codex::Translator translator;
  std::vector<agent::Event> events;
  for (const char* line : {
           R"({"method":"item/started","params":{"item":{
               "type":"commandExecution","id":"exec-cb02",
               "command":"pwsh -Command 'curl.exe https://example.com'",
               "source":"agent","status":"inProgress",
               "aggregatedOutput":null,"exitCode":null}}})",
           R"({"method":"item/autoApprovalReview/started","params":{
               "targetItemId":"exec-cb02","review":{"status":"inProgress",
               "riskLevel":null,"userAuthorization":null,"rationale":null},
               "action":{"type":"command","source":"unifiedExec"}}})",
           R"({"method":"guardianWarning","params":{"threadId":"t",
               "message":"Automatic approval review denied (risk: critical, authorization: high): Policy says no."}})",
           R"({"method":"item/autoApprovalReview/completed","params":{
               "targetItemId":"exec-cb02","decisionSource":"agent",
               "review":{"status":"denied","riskLevel":"critical",
                         "userAuthorization":"high",
                         "rationale":"Policy says no."},
               "action":{"type":"command","source":"unifiedExec"}}})",
           R"({"method":"item/completed","params":{"item":{
               "type":"commandExecution","id":"exec-cb02",
               "command":"pwsh -Command 'curl.exe https://example.com'",
               "source":"agent","status":"declined",
               "aggregatedOutput":null,"exitCode":null}}})",
       }) {
    for (agent::Event& event : translator.Translate(proto::Json::parse(line))) {
      events.push_back(std::move(event));
    }
  }
  // Recenzent nie je neznama sprava: started a warning su zamerne ticho.
  CHECK(EventsOf<agent::Unrecognised>(events).empty());
  const auto results = EventsOf<agent::ToolCallFinished>(events);
  CHECK_EQ(results.size(), size_t{1});
  if (!results.empty()) {
    const agent::ToolResult& result = results[0].result;
    CHECK(result.isError);
    CHECK_EQ(result.text, i18n::Utf8(i18n::Str::kAutoReviewDenied) +
                              ": Policy says no.");
  }

  // Schvalenie nic neprida: vysledok je vystup prikazu, ako po ano citatela.
  proto::codex::Translator approving;
  approving.Translate(proto::Json::parse(
      R"({"method":"item/autoApprovalReview/completed","params":{
          "targetItemId":"exec-ok","review":{"status":"approved",
          "rationale":"Fine."}}})"));
  const auto approved = approving.Translate(proto::Json::parse(
      R"({"method":"item/completed","params":{"item":{
          "type":"commandExecution","id":"exec-ok","status":"completed",
          "aggregatedOutput":"200","exitCode":0}}})"));
  const auto approvedResults = EventsOf<agent::ToolCallFinished>(approved);
  CHECK_EQ(approvedResults.size(), size_t{1});
  if (!approvedResults.empty()) {
    CHECK_EQ(approvedResults[0].result.text, std::string("200"));
    CHECK(!approvedResults[0].result.isError);
  }
}

void TestCodexFixtureAsk(const std::string& dir) {
  TEST("codex fixtura ask: otazka je klucovana id, odpoved je pole");
  CodexReplay replay;
  std::vector<agent::Question> asked;
  agent::ToolCall call;
  if (!ReplayCodex(dir + "/codex-ask.jsonl", &replay,
                   [&](const proto::Json& message,
                       const proto::codex::Translator&) {
                     if (message.value("method", std::string()) ==
                         "item/tool/requestUserInput") {
                       asked = proto::codex::ReadQuestions(message["params"]);
                       call = proto::codex::QuestionCall(message["params"]);
                     }
                   })) {
    return;
  }
  CHECK_EQ(asked.size(), size_t{1});
  if (asked.empty()) return;
  CHECK_EQ(asked[0].id, std::string("drink_preference"));
  CHECK_EQ(asked[0].text, std::string("Pijem radšej čaj alebo kávu?"));
  CHECK_EQ(asked[0].header, std::string("Nápoj"));
  CHECK(asked[0].allowsOther);
  CHECK_EQ(asked[0].options.size(), size_t{2});
  CHECK(call.kind == agent::ToolKind::Question);
  CHECK(!call.id.empty());

  // Presne odpoved, s ktorou model pri sonde pokracoval.
  agent::QuestionAnswer answer;
  answer.chosen = {{"Čaj"}};
  CHECK_EQ(proto::codex::MakeQuestionAnswer(asked, answer),
           proto::Json::parse(
               R"({"answers":{"drink_preference":{"answers":["Čaj"]}}})"));
  const agent::ToolResult result =
      proto::codex::QuestionResult(call.id, asked, answer);
  CHECK(!result.isError);
  CHECK(result.text.find("Čaj") != std::string::npos);
  answer.declined = true;
  CHECK_EQ(proto::codex::MakeQuestionAnswer(asked, answer),
           proto::Json::parse(R"({"answers":{}})"));
  CHECK(proto::codex::QuestionResult(call.id, asked, answer).isError);

  // request_user_input_async: model si ho pri overeni naostro (4. 10. 2026)
  // vybral namiesto blokujuceho.  Stream z toho nie je zachyteny, takze toto
  // NIE JE fixtura: tvar polozky je zo schemy (ThreadItem agentMessage,
  // AsyncUserInputQuestion) a text a otazka doslovne z rolloutu tej session.
  proto::Json async = proto::Json::parse(R"({
    "method": "item/completed",
    "params": {"item": {"type": "agentMessage", "id": "call_e6Z",
      "text": "Čaj alebo káva?\n- Čaj\n- Káva", "phase": "final_answer",
      "memoryCitation": null, "delivery": "async",
      "questions": [{"title": "Čaj alebo káva?", "options": ["Čaj", "Káva"]}]},
      "threadId": "t", "turnId": "u"}})");
  proto::codex::Translator translator;
  const std::vector<agent::Event> events = translator.Translate(async);
  // Blok otazky, nie text s odrazkami -- ten by ju zopakoval.
  CHECK(EventsOf<agent::AssistantText>(events).empty());
  const auto posed = EventsOf<agent::ToolCallStarted>(events);
  CHECK_EQ(posed.size(), size_t{1});
  if (!posed.empty()) {
    CHECK(posed[0].call.kind == agent::ToolKind::Question);
    CHECK_EQ(posed[0].call.questions.size(), size_t{1});
  }
  const auto byPrompt = EventsOf<agent::QuestionByPrompt>(events);
  CHECK_EQ(byPrompt.size(), size_t{1});
  // Nesie id volania, aby panel vedel vysledok (odpoved alebo "bez
  // odpovede") zaradit za neho -- claude-gui-lkk.44.11.
  if (!byPrompt.empty() && !posed.empty()) {
    CHECK_EQ(byPrompt[0].callId, std::string("call_e6Z"));
    CHECK_EQ(byPrompt[0].callId, posed[0].call.id);
    model::Transcript transcript;
    transcript.Append(events);
    agent::ToolResult dismissed;
    dismissed.callId = byPrompt[0].callId;
    dismissed.text = "bez odpovede";
    dismissed.isError = true;
    transcript.Append({agent::ToolCallFinished{dismissed}});
    std::string problem;
    CHECK(transcript.CheckInvariants(&problem));
    CHECK_EQ(transcript.blocks().size(), size_t{2});
    CHECK(transcript.blocks()[1].kind == model::BlockKind::ToolResult);
    CHECK_EQ(transcript.blocks()[1].summary, std::wstring(L"chyba: bez odpovede"));
  }
  if (!byPrompt.empty() && !byPrompt[0].questions.empty()) {
    const agent::Question& question = byPrompt[0].questions[0];
    CHECK_EQ(question.text, std::string("Čaj alebo káva?"));
    CHECK_EQ(question.options.size(), size_t{2});
    CHECK(question.allowsOther);
  }
  // V historii sa otazka uz neponuka znova, len sa ukaze.
  proto::Json resumed = {
      {"thread", {{"turns", proto::Json::array({{{"status", "completed"},
                    {"items", proto::Json::array({async["params"]["item"]})}}})}}}};
  const std::vector<agent::Event> history =
      proto::codex::TranslateHistory(resumed);
  CHECK_EQ(EventsOf<agent::ToolCallStarted>(history).size(), size_t{1});
  CHECK(EventsOf<agent::QuestionByPrompt>(history).empty());
}

void TestCodexFixtureInterrupt(const std::string& dir) {
  TEST("codex fixtura interrupt: tah konci svojim turn/completed");
  CodexReplay replay;
  if (!ReplayCodex(dir + "/codex-interrupt.jsonl", &replay)) return;
  const auto ends = EventsOf<agent::TurnEnded>(replay.events);
  CHECK_EQ(ends.size(), size_t{1});
  if (!ends.empty()) {
    CHECK(ends[0].outcome == agent::TurnOutcome::Interrupted);
  }
  // Za koncom tahu este chodi vystup prikazu, ktory nikdy neskoncil.  Do
  // prepisu z neho nesmie nic pribudnut.
  CHECK(std::holds_alternative<agent::TurnEnded>(replay.events.back()));
  // Prikaz bezal, ked prisiel koniec -- vo fixture vysledok nema.
  CheckCallsHaveResults(replay.transcript, 1);

  // Naozivo (4. 10. 2026) vsak prerusenie prikaz nezabilo, dobehol a jeho
  // item/completed prislo po konci tahu.  Polozka je skopirovana z fixtury,
  // len s dokoncenym stavom.  Volanie sa nesmie objavit druhykrat -- vysledok
  // patri za to povodne.
  proto::Json running;
  for (const proto::Json& message : replay.messages) {
    if (message.value("method", std::string()) == "item/started" &&
        message["params"]["item"].value("type", std::string()) ==
            "commandExecution") {
      running = message;
    }
  }
  CHECK(!running.is_null());
  if (running.is_null()) return;
  proto::Json late = running;
  late["method"] = "item/completed";
  late["params"]["item"]["status"] = "completed";
  late["params"]["item"]["aggregatedOutput"] = "koniec\r\n";
  late["params"]["item"]["exitCode"] = 0;
  const std::vector<agent::Event> after = replay.translator.Translate(late);
  CHECK_EQ(after.size(), size_t{1});
  CHECK(EventsOf<agent::ToolCallStarted>(after).empty());
  replay.transcript.Append(after);
  CheckCallsHaveResults(replay.transcript);
  // Koncovy zlom riadku, ktory Codex nechava, z vystupu zmizne.
  const auto finished = EventsOf<agent::ToolCallFinished>(after);
  if (!finished.empty()) {
    CHECK_EQ(finished[0].result.text, std::string("koniec"));
  }
}

void TestCodexFixtureReasoning(const std::string& dir) {
  TEST("codex fixtura reasoning: suhrn premyslania je blok");
  CodexReplay replay;
  if (!ReplayCodex(dir + "/codex-reasoning.jsonl", &replay)) return;
  CHECK_EQ(EventsOf<agent::ThinkingTick>(replay.events).size(), size_t{1});
  const auto thinking = EventsOf<agent::Thinking>(replay.events);
  CHECK_EQ(thinking.size(), size_t{1});
  if (!thinking.empty()) {
    CHECK(thinking[0].text.find("Vypočítavam") != std::string::npos);
  }
  CHECK(CountKinds(replay.transcript).count(model::BlockKind::Thinking) > 0);
}

void TestCodexFixtureResume(const std::string& dir) {
  TEST("codex fixtura resume: historia prichadza v tvare streamu");
  bool ok = false;
  const auto messages = ReadJsonl(dir + "/codex-resume.jsonl", &ok);
  if (!ok) {
    Fail(__FILE__, __LINE__, "chyba codex-resume.jsonl");
    return;
  }
  const proto::Json* resumed = nullptr;
  for (const proto::Json& message : messages) {
    if (proto::codex::KindOf(message) == proto::codex::MessageKind::Response &&
        message.contains("result") && message["result"].contains("thread")) {
      resumed = &message;
    }
  }
  CHECK(resumed != nullptr);
  if (resumed == nullptr) return;

  const std::vector<agent::Event> history =
      proto::codex::TranslateHistory((*resumed)["result"]);
  model::Transcript transcript;
  const model::HistoryCounts counts =
      model::RestoreHistory(history, &transcript);
  std::string problem;
  CHECK(transcript.CheckInvariants(&problem));
  CHECK_EQ(transcript.unknownCount(), size_t{0});
  CHECK_EQ(counts.prompts, size_t{1});
  const auto kinds = CountKinds(transcript);
  // Zamietnuty prikaz sa do historie neuklada (odmerane), takze z troch
  // zostali dva -- jeden uspesny, jeden zlyhany.
  CHECK_EQ(kinds.at(model::BlockKind::ToolUse), size_t{2});
  CHECK_EQ(kinds.at(model::BlockKind::ToolResult), size_t{2});
  CHECK_EQ(kinds.at(model::BlockKind::AssistantText), size_t{2});
  CheckCallsHaveResults(transcript);
  // Prompt je prvy blok, ako po obnoveni Claude.
  CHECK(transcript.blocks().front().kind == model::BlockKind::UserPrompt);
  // Cesty sa skracuju voci projektu, a ten sa vie z odpovede.
  CHECK(!EventsOf<agent::WorkingDirectory>(history).empty());
  const auto models = EventsOf<agent::ModelChanged>(history);
  CHECK(!models.empty());
  // Rezim obnoveneho threadu: untrusted + workspaceWrite nie je predvolba.
  const proto::Json& result = (*resumed)["result"];
  CHECK_EQ(proto::codex::ModeFromSettings(
               result["approvalPolicy"], result["sandbox"],
               result.value("approvalsReviewer", std::string()),
               result["collaborationMode"].value("mode", std::string())),
           std::string(proto::codex::kModeCustom));
}

int main(int argc, char** argv) {
  const std::string fixtures = argc > 1 ? argv[1] : "tests/fixtures";

  TestUtfRoundTrip();
  TestCatalog();
  TestRangeMap();
  TestBlockAtAndNavigation();
  TestControlCharactersNeverReachTheBuffer();
  TestErrorNavigationAndFirstLine();
  TestInterruptLeavesAMark();
  TestSessionEndedKeepsStderr();
  TestToolResultsSitBehindTheirCall();
  TestBookmarksSurviveCollapsing();
  TestBookmarkIntoCollapsedBlock();
  TestFindSearchesCollapsedBlocks();
  TestRateLimitParsing();
  TestInitializeResponseParsing();
  TestPermissionModeSwitch();
  TestPortModeCycleMatchesClaude();
  TestArguments();
  TestSessionOrdinals();
  TestSettings();
  TestSettingsWrite();
  TestUpdateRules();
  TestPermissionModeReports();
  TestPermissionModeTracker();
  TestAnsweringModel();
  TestTranslatorModelAndTurn();
  TestUsageParsing();
  TestContextTokensParsing();
  TestNewlinesAreOneCharacter();
  TestAnsiEscapesAreStripped();
  TestToolPathsAreShortened();
  TestEditAndWriteSayWhatChanged();
  TestOwnInstructionsStayOutOfTranscript();
  TestPermissionTextMatchesTranscript();
  TestFailedToolResultReadsLikeAnError();
  TestEmptyBlocksAreDropped();
  TestSpeakerPrefix();
  TestSummariesAreOneLine();
  TestAskUserQuestionRoundTrip();
  TestQuestionsReadAsText();
  TestSessionPickIgnoresMtime();
  TestSessionSummaryFindsTheHumanPrompt();
  TestHistoryRestore();
  TestSessionFilePath();
  TestProjectKeyAndTime();
  TestFixtureBasic(fixtures);
  TestFixtureDenied(fixtures);
  TestFixtureThinking(fixtures);
  TestFixtureDisk(fixtures);
  TestCodexModes();
  TestCodexElicitation();
  TestCodexReviewerDenial();
  TestClaudeSessionPermissions();
  TestCodexFixtureMulti(fixtures);
  TestCodexFixtureEdit(fixtures);
  TestCodexFixtureAsk(fixtures);
  TestCodexFixtureInterrupt(fixtures);
  TestCodexFixtureReasoning(fixtures);
  TestCodexFixtureResume(fixtures);

  if (const char* corpus = std::getenv("AGENTALOUD_CORPUS")) {
    SoakOverCorpus(corpus);
  }

  std::printf("\n%d kontrol, %d zlyhani\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
