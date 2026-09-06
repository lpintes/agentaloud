// The C++ half of claude-gui-lkk.2: what tools/spike_control.py proved, done
// through win/ and proto/ and with no Python anywhere.  No windows yet -- if
// this prints an approved tool and a denied one, the protocol layer is right
// and the GUI can be built on it.
//
//   spike_console <working-dir> [allow|deny|interrupt]
//
// The working directory should be a throwaway git repository: the point of the
// exercise is a real `git commit`, and on approval it really happens.
//
// `interrupt` runs a different errand (claude-gui-lkk.5.4): it starts a tool
// that will not finish on its own and stops the turn from underneath it, with
// every record printed whole.  What the end of an interrupted turn looks like
// is not something one can read out of the CLI binary with any confidence.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>

#include "proto/session.h"

namespace {

// Every record printed whole rather than summarised.  Only the interrupt
// errand wants it: there the shape of the records IS the result.
bool gVerbose = false;

// The console is the only place in this program that speaks to a person, so
// it is also the only place that has to care that Windows consoles are not
// UTF-8 until told otherwise.
void UseUtf8Console() {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
}

std::string Shorten(const std::string& text, size_t limit) {
  if (text.size() <= limit) return text;
  return text.substr(0, limit) + "...";
}

// The text blocks of one assistant message, which is all a console needs of it.
void PrintAssistantText(const proto::Json& record) {
  auto message = record.find("message");
  if (message == record.end()) return;
  auto content = message->find("content");
  if (content == message->end() || !content->is_array()) return;
  for (const proto::Json& block : *content) {
    const std::string type = block.value("type", std::string());
    if (type == "text") {
      std::printf("  claude: %s\n", block.value("text", std::string()).c_str());
    } else if (type == "tool_use") {
      std::printf("  nastroj: %s %s\n",
                  block.value("name", std::string()).c_str(),
                  Shorten(block.value("input", proto::Json::object()).dump(),
                          120).c_str());
    }
  }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  UseUtf8Console();
  if (argc < 2) {
    std::printf(
        "pouzitie: spike_console <pracovny-adresar> [allow|deny|interrupt]\n");
    return 2;
  }
  const std::wstring mode = argc < 3 ? L"allow" : argv[2];
  const bool interrupt = mode == L"interrupt";
  // The interrupt errand has to let the tool start before it can stop it.
  const bool allow = interrupt || mode != L"deny";
  gVerbose = interrupt;

  proto::Session::Options options;
  options.workingDir = argv[1];
  options.model = L"haiku";
  options.extraArgs = {L"--tools", L"Bash"};
  // Filled in here rather than left to Start, so that the line printed below
  // is the line that really runs -- id included.
  options.sessionId = proto::NewSessionId();

  std::printf("spustam: %ls\n", proto::BuildCommandLine(options).c_str());
  std::printf("rozhodnutie o povoleni: %s\n\n", allow ? "allow" : "deny");

  proto::Session session;
  bool asked = false;

  const bool started = session.Start(
      options,
      [](const proto::Event& event) {
        if (gVerbose) {
          std::printf("[%s] %s\n", proto::KindName(event.kind),
                      event.raw.dump().c_str());
          return;
        }
        switch (event.kind) {
          case proto::EventKind::SystemInit:
            std::printf("[init] model=%s session=%s\n",
                        event.raw.value("model", std::string()).c_str(),
                        event.sessionId.c_str());
            break;
          case proto::EventKind::Assistant:
            PrintAssistantText(event.raw);
            break;
          case proto::EventKind::SystemPermissionDenied:
            std::printf("[zamietnute pravidlom] %s\n",
                        event.raw.value("message", std::string()).c_str());
            break;
          case proto::EventKind::Result:
            std::printf("[koniec tahu] %s\n",
                        event.raw.value("stop_reason", std::string()).c_str());
            break;
          case proto::EventKind::Unknown:
            // Loud on purpose: an unrecognised record is the one thing this
            // spike is meant to notice.
            std::printf("[NEZNAMY ZAZNAM] %s\n",
                        Shorten(event.raw.dump(), 200).c_str());
            break;
          default:
            break;
        }
      },
      [&asked, allow](const proto::PermissionRequest& request) {
        asked = true;
        std::printf("\n[control kanal] pyta sa na %s (dovod: %s)\n",
                    request.toolName.c_str(),
                    request.decisionReasonType.c_str());
        std::printf("  vstup: %s\n", request.input.dump().c_str());
        std::printf("  odpovedam: %s\n\n", allow ? "allow" : "deny");

        proto::PermissionDecision decision;
        decision.allow = allow;
        decision.denyMessage =
            "Pouzivatel to zamietol. Nepokracuj a spytaj sa, co dalej.";
        return decision;
      });

  if (!started) {
    std::printf("claude sa nepodarilo spustit\n");
    return 1;
  }

  if (interrupt) {
    session.SendPrompt(
        "Run exactly this bash command and nothing else: sleep 45");
    // Long enough for the permission to be answered and the tool to be really
    // running.  Interrupting before that would only prove that a request sent
    // into nothing does nothing.
    Sleep(10000);
    std::printf("\n[posielam interrupt]\n");
    if (!session.Interrupt()) {
      std::printf("[ziadny tah nebezal -- interrupt sa neposlal]\n");
    }
    // A generous wait on purpose: the question the errand asks is whether the
    // turn ends at all, and a timeout here is itself the answer.
    if (!session.WaitForTurn(60000)) {
      std::printf("\n[tah sa po interrupte NESKONCIL -- ziadny result]\n");
    } else {
      std::printf("\n[tah po interrupte skoncil]\n");
    }
    session.Stop();
    return 0;
  }

  session.SendPrompt(
      "Run exactly this bash command and nothing else: "
      "git commit --allow-empty -m spike-cpp");
  if (!session.WaitForTurn(120000)) {
    std::printf("\ntah sa neskoncil v case\n");
  }
  session.Stop();

  std::printf("\ncontrol kanal sa %s\n",
              asked ? "ozval" : "NEOZVAL -- smerovanie povoleni nefunguje");
  return asked ? 0 : 1;
}
