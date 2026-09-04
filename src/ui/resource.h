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

#define IDC_DETAILS_ID          1000
#define IDC_DETAILS_MODEL       1001
#define IDC_DETAILS_MODE        1002
#define IDC_DETAILS_PROJECT     1003
#define IDC_DETAILS_CONTEXT     1004
#define IDC_DETAILS_COST        1005
#define IDC_DETAILS_TOKENS      1006
#define IDC_DETAILS_COPY        1007

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

#endif
