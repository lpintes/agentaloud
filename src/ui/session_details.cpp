#include "ui/session_details.h"

#include <cwchar>

#include "i18n/i18n.h"
#include "ui/dialog_texts.h"
#include "ui/resource.h"

namespace ui {

using i18n::Str;

namespace {

// A blank box reads as "edit, blank" and says nothing about why.  Every one of
// these is empty for a reason a reader can act on -- wait for the first turn,
// mostly -- so the reason is what goes in.
std::wstring Unknown() { return i18n::Text(Str::kUnknownYet); }

std::wstring Number(long long value) { return std::to_wstring(value); }

// Four decimals, with the language's separator ("0,0453 USD" in Slovak): at
// the sums a session reaches two decimals would round most of them to zero.
std::wstring Money(double amount) {
  wchar_t text[32] = {};
  std::swprintf(text, 32, L"%.4f", amount);
  std::wstring result = text;
  const size_t dot = result.find(L'.');
  if (dot != std::wstring::npos) {
    result.replace(dot, 1, i18n::Text(Str::kDecimalSeparator));
  }
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
    if (i > 0 && (digits.size() - i) % 3 == 0) {
      text += i18n::Text(Str::kThousandsSeparator);
    }
    text += digits[i];
  }
  return text;
}

std::wstring FormatContext(const SessionDetails& details) {
  const long long window = details.haveUsage ? details.usage.contextWindow : 0;
  if (details.contextTokens <= 0) {
    // The window on its own is worth saying: it is the one number here that
    // says how much room there is, and it arrives a turn before the other.
    if (window <= 0) return Unknown();
    return i18n::Format(Str::kContextWindowOnly, {Grouped(window)});
  }
  if (window <= 0) {
    return i18n::Format(Str::kContextTokens, {Grouped(details.contextTokens)});
  }
  // Percent first would be shorter, but the two raw numbers are what a person
  // compares when deciding whether a long file still fits.
  const long long percent = details.contextTokens * 100 / window;
  return i18n::Format(Str::kContextOf,
                      {i18n::Format(Str::kContextTokens,
                                    {Grouped(details.contextTokens)}),
                       Grouped(window), std::to_wstring(percent)});
}

std::wstring FormatCost(const SessionDetails& details) {
  if (!details.haveUsage) return Unknown();
  const agent::Usage& usage = details.usage;
  if (usage.billedUsd && *usage.billedUsd > 0) return Money(*usage.billedUsd);
  // A CLI that states no cost at all gets no number: a zero here would read
  // as "this was free".
  if (!usage.listUsd) return Unknown();
  // Both numbers, because on a subscription the billed one is 0 and printing
  // it alone would say the session was free, while printing only the list
  // price would claim money nobody is being charged.
  return i18n::Format(Str::kCostListOnly, {Money(*usage.listUsd)});
}

std::wstring FormatTokens(const SessionDetails& details) {
  if (!details.haveUsage) return Unknown();
  const agent::Usage& usage = details.usage;
  std::wstring text =
      i18n::Format(Str::kTokensInput, {Number(usage.inputTokens)}) + L", " +
      i18n::Format(Str::kTokensOutput, {Number(usage.outputTokens)});
  if (usage.reasoningTokens > 0) {
    text += L", " +
            i18n::Format(Str::kTokensReasoning, {Number(usage.reasoningTokens)});
  }
  text += L", " +
          i18n::Format(Str::kTokensCacheRead, {Number(usage.cacheReadTokens)}) +
          L", " +
          i18n::Format(Str::kTokensCacheWrite, {Number(usage.cacheWriteTokens)});
  return text;
}

std::wstring OrUnknown(const std::wstring& text) {
  return text.empty() ? Unknown() : text;
}

}  // namespace

SessionDetailsDialog::SessionDetailsDialog(const SessionDetails& details,
                                           std::function<void()> onCopyId)
    : details_(details), onCopyId_(std::move(onCopyId)) {}

bool SessionDetailsDialog::OnInit() {
  LocalizeDialog(hwnd_, IDD_SESSION_DETAILS);
  SetText(IDC_DETAILS_ID, OrUnknown(details_.id));
  // The asked-for name only when it says something the model does not: "opus"
  // beside claude-opus-5 would be the same fact twice, but "opusplan" is the
  // only place that says the model will be a different one out of plan mode.
  std::wstring model = OrUnknown(details_.model);
  if (!details_.requestedModel.empty() && !details_.model.empty() &&
      details_.model.find(details_.requestedModel) == std::wstring::npos) {
    model += L", " +
             i18n::Format(Str::kModelRequested, {details_.requestedModel});
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
