#ifndef UI_RESOURCE_H
#define UI_RESOURCE_H

// Identifiers shared between the .rc and the C++ that fills the dialogs in.
// Plain #define and no namespace on purpose: windres understands the C
// preprocessor and nothing else.
//
// Ranges, so that a number never has to be looked up to be sure it is free:
// dialogs from 100, controls from 1000.  IDOK and IDCANCEL come from
// windows.h and are 1 and 2.

#define IDD_SESSION_DETAILS 100
#define IDD_ASK_QUESTION    101
#define IDD_PERMISSION      102

#define IDC_DETAILS_ID          1000
#define IDC_DETAILS_MODEL       1001
#define IDC_DETAILS_MODE        1002
#define IDC_DETAILS_PROJECT     1003
#define IDC_DETAILS_CONTEXT     1004
#define IDC_DETAILS_COST        1005
#define IDC_DETAILS_TOKENS      1006
#define IDC_DETAILS_COPY        1007
#define IDC_DETAILS_ACCOUNT     1008

// Labels need their own ids only because a static with IDC_STATIC (-1) cannot
// be told apart from any other, and each of these is the accessible name of
// the box beside it.
#define IDC_DETAILS_ID_LABEL      1010
#define IDC_DETAILS_MODEL_LABEL   1011
#define IDC_DETAILS_MODE_LABEL    1012
#define IDC_DETAILS_PROJECT_LABEL 1013
#define IDC_DETAILS_CONTEXT_LABEL 1014
#define IDC_DETAILS_COST_LABEL    1015
#define IDC_DETAILS_TOKENS_LABEL  1016
#define IDC_DETAILS_ACCOUNT_LABEL 1017

// The question dialog.  One question at a time, so a fixed template covers the
// two-to-four options every question has -- which of the two lists is used
// depends on multiSelect, and the other one is hidden.  See ui/ask_dialog.h.
#define IDC_ASK_QUESTION        1030
#define IDC_ASK_OPTIONS         1031
#define IDC_ASK_OPTIONS_MULTI   1032
#define IDC_ASK_OTHER           1033

#define IDC_ASK_QUESTION_LABEL  1040
#define IDC_ASK_OPTIONS_LABEL   1041
#define IDC_ASK_OTHER_LABEL     1042

// The permission prompt.  The tool's name is in the caption rather than in a
// field of its own: the focus starts in the arguments, and NVDA reads the
// title first, so the two together are one announcement that says both what
// is about to run and with what.  See ui/permission_dialog.h.
#define IDC_PERM_INPUT          1050
#define IDC_PERM_DESCRIPTION    1051
#define IDC_PERM_REASON         1052

#define IDC_PERM_INPUT_LABEL        1060
#define IDC_PERM_DESCRIPTION_LABEL  1061
#define IDC_PERM_REASON_LABEL       1062

#endif
