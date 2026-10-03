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

// Thousands separated, and only here.  Six digits in a row is what a context
// window looks like, and read aloud "200000" is a guessing game -- a screen
// reader says the whole thing as one number and the listener has to count.
//
// Not shortened to "1,0 M", which was raised from use and turned down: a
// window of "1 000 000" is a mouthful, but this field exists to answer "does
// the rest of this file still fit", and a rounded number cannot answer it.
// The length is the price of the answer being exact.
std::wstring Grouped(long long value) {
  const std::wstring digits = std::to_wstring(value);
  std::wstring text;
  for (size_t i = 0; i < digits.size(); ++i) {
    if (i > 0 && (digits.size() - i) % 3 == 0) text += L' ';
    text += digits[i];
  }
  return text;
}

std::wstring FormatContext(const SessionDetails& details) {
  const long long window = details.haveUsage ? details.usage.contextWindow : 0;
  if (details.contextTokens <= 0) {
    // The window on its own is worth saying: it is the one number here that
    // says how much room there is, and it arrives a turn before the other.
    if (window <= 0) return kUnknown;
    return L"okno " + Grouped(window) + L" tokenov, využitie zatiaľ neznáme";
  }
  const std::wstring used = Grouped(details.contextTokens) + L" tokenov";
  if (window <= 0) return used;
  // Percent first would be shorter, but the two raw numbers are what a person
  // compares when deciding whether a long file still fits.
  const long long percent = details.contextTokens * 100 / window;
  return used + L" z " + Grouped(window) + L" (" + std::to_wstring(percent) +
         L" %)";
}

std::wstring FormatCost(const SessionDetails& details) {
  if (!details.haveUsage) return kUnknown;
  const agent::Usage& usage = details.usage;
  if (usage.billedUsd && *usage.billedUsd > 0) return Money(*usage.billedUsd);
  // A CLI that states no cost at all gets no number: a zero here would read
  // as "this was free".
  if (!usage.listUsd) return kUnknown;
  // Both numbers, because on a subscription the billed one is 0 and printing
  // it alone would say the session was free, while printing only the list
  // price would claim money nobody is being charged.
  return Money(*usage.listUsd) + L" podľa cenníka, účtované 0";
}

std::wstring FormatTokens(const SessionDetails& details) {
  if (!details.haveUsage) return kUnknown;
  const agent::Usage& usage = details.usage;
  std::wstring text = L"vstup " + Number(usage.inputTokens) + L", výstup " +
                      Number(usage.outputTokens);
  if (usage.reasoningTokens > 0) {
    text += L", myslenie " + Number(usage.reasoningTokens);
  }
  text += L", cache čítaná " + Number(usage.cacheReadTokens) +
          L", cache zapísaná " + Number(usage.cacheWriteTokens);
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
  // The asked-for name only when it says something the model does not: "opus"
  // beside claude-opus-5 would be the same fact twice, but "opusplan" is the
  // only place that says the model will be a different one out of plan mode.
  std::wstring model = OrUnknown(details_.model);
  if (!details_.requestedModel.empty() && !details_.model.empty() &&
      details_.model.find(details_.requestedModel) == std::wstring::npos) {
    model += L", zvolený " + details_.requestedModel;
  }
  SetText(IDC_DETAILS_MODEL, model);
  // The CLI's own word for the mode, with nothing added.  It first said
  // "default (pýta sa na všetko)" while the mode was still unknown and plain
  // "default" once system/init had been, so the same session read two
  // different ways depending on when it was asked -- and the gloss was not
  // worth having anyway.  Unknown is unknown here like everywhere else in this
  // dialog: what the default resolves to is a matter of settings, so filling
  // it in ourselves would be a guess that can be wrong.
  SetText(IDC_DETAILS_MODE, OrUnknown(details_.permissionMode));
  SetText(IDC_DETAILS_PROJECT, OrUnknown(details_.project));
  SetText(IDC_DETAILS_CONTEXT, FormatContext(details_));
  // Directly above the cost, because that is the question it answers.
  SetText(IDC_DETAILS_ACCOUNT, OrUnknown(details_.account));
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
