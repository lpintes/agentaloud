#include "model/background.h"

#include "i18n/i18n.h"

namespace model {

std::wstring BackgroundText(const std::vector<agent::BackgroundTask>& tasks) {
  long long agents = 0;
  long long shells = 0;
  for (const agent::BackgroundTask& task : tasks) {
    (task.kind == agent::TaskKind::Shell ? shells : agents)++;
  }
  std::wstring parts;
  if (agents > 0) parts = i18n::Count(i18n::Plural::kBackgroundAgents, agents);
  if (shells > 0) {
    if (!parts.empty()) parts += L", ";
    parts += i18n::Count(i18n::Plural::kBackgroundShells, shells);
  }
  if (parts.empty()) return {};
  return i18n::Format(i18n::Str::kStatusBackground, {parts});
}

std::wstring TurnStatus(const std::wstring& turn,
                        const std::vector<agent::BackgroundTask>& tasks) {
  const std::wstring background = BackgroundText(tasks);
  if (turn.empty()) return background;
  if (background.empty()) return turn;
  return turn + L"; " + background;
}

}  // namespace model
