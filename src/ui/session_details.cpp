#include "ui/session_details.h"

#include <cwchar>

#include "ui/resource.h"

namespace ui {
namespace {

// A blank box reads as "edit, blank" and says nothing about why.  Every one of
// these is empty for a reason a reader can act on -- wait for the first turn,
// mostly -- so the reason is what goes in.
const wchar_t kUnknown[] = L"zatiaľ neznáme";

std::wstring Number(long long value) { return std::to_wstring(value); }

// Four decimals, and a comma: this is Slovak text, and at the sums a session
// reaches ("0,0453 USD") two decimals would round most of them to zero.
std::wstring Money(double amount) {
  wchar_t text[32] = {};
  std::swprintf(text, 32, L"%.4f", amount);
  std::wstring result = text;
  const size_t dot = result.find(L'.');
  if (dot != std::wstring::npos) result[dot] = L',';
  return result + L" USD";
}

std::wstring FormatCost(const SessionDetails& details) {
  if (!details.haveUsage) return kUnknown;
  const proto::Usage& usage = details.usage;
  if (usage.billedUsd > 0) return Money(usage.billedUsd);
  // Both numbers, because on a subscription the billed one is 0 and printing
  // it alone would say the session was free, while printing only the list
  // price would claim money nobody is being charged.
  return Money(usage.listUsd) + L" podľa cenníka, účtované 0";
}

std::wstring FormatTokens(const SessionDetails& details) {
  if (!details.haveUsage) return kUnknown;
  const proto::Usage& usage = details.usage;
  std::wstring text = L"vstup " + Number(usage.inputTokens) + L", výstup " +
                      Number(usage.outputTokens);
  if (usage.thinkingTokens > 0) {
    text += L", myslenie " + Number(usage.thinkingTokens);
  }
  text += L", cache čítaná " + Number(usage.cacheReadTokens) +
          L", cache zapísaná " + Number(usage.cacheCreationTokens);
  return text;
}

std::wstring OrUnknown(const std::wstring& text) {
  return text.empty() ? kUnknown : text;
}

}  // namespace

SessionDetailsDialog::SessionDetailsDialog(const SessionDetails& details,
                                           std::function<void()> onCopyId)
    : details_(details), onCopyId_(std::move(onCopyId)) {}

bool SessionDetailsDialog::OnInit() {
  SetText(IDC_DETAILS_ID, OrUnknown(details_.id));
  SetText(IDC_DETAILS_MODEL, OrUnknown(details_.model));
  // The CLI's own word for the mode, with nothing added.  It first said
  // "default (pýta sa na všetko)" while the mode was still unknown and plain
  // "default" once system/init had been, so the same session read two
  // different ways depending on when it was asked -- and the gloss was not
  // worth having anyway.  Unknown is unknown here like everywhere else in this
  // dialog: what the default resolves to is a matter of settings, so filling
  // it in ourselves would be a guess that can be wrong.
  SetText(IDC_DETAILS_MODE, OrUnknown(details_.permissionMode));
  SetText(IDC_DETAILS_PROJECT, OrUnknown(details_.project));
  SetText(IDC_DETAILS_COST, FormatCost(details_));
  SetText(IDC_DETAILS_TOKENS, FormatTokens(details_));
  // There is nothing here to copy when there is no id, and a button that does
  // nothing is worse than one that says it cannot.
  SetEnabled(IDC_DETAILS_COPY, !details_.id.empty());
  return false;  // the dialog manager puts the focus on the id box
}

bool SessionDetailsDialog::OnCommand(int id, int notification) {
  if (id != IDC_DETAILS_COPY) return false;
  // Copies and stays open.  The dialog is also where the id is read from, and
  // closing it on the copy would take that away at the moment it is wanted --
  // the usual next step is to check the id was the one meant.
  if (onCopyId_) onCopyId_();
  return true;
}

}  // namespace ui
