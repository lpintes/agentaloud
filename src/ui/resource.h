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
#define IDD_COMMANDS        103
#define IDD_KEYS            104
#define IDD_NEW_SESSION     105
#define IDD_FIND            106
#define IDD_ABOUT           107
#define IDD_TASKS           108
#define IDD_PLAN_APPROVAL   109
#define IDD_SIGN_IN         110
#define IDD_LINKS           111
#define IDD_FORM_FIELD      112
#define IDD_FORM_URL        113
#define IDD_MODELS          114

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
// Shown only when the backend offers it (agent::Verdict::AllowForSession).
#define IDC_PERM_SESSION        1053
// What the session button lets through; hidden with it (claude-gui-lkk.62).
#define IDC_PERM_SCOPE          1054
#define IDC_PERM_SCOPE_LABEL        1063
// Allowed past this session, in the project's settings; shown, with what it
// lets through, only when offered (agent::Verdict::AllowInProject).
#define IDC_PERM_PROJECT        1055
#define IDC_PERM_PROJECT_SCOPE  1056
#define IDC_PERM_PROJECT_SCOPE_LABEL 1064

#define IDC_PERM_INPUT_LABEL        1060
#define IDC_PERM_DESCRIPTION_LABEL  1061
#define IDC_PERM_REASON_LABEL       1062

// The slash command picker.  The list's label carries the count of what the
// filter left ("Príkazy (12 z 79)"), because that is the one thing a screen
// reader reads by itself on the way into the list -- the filtering itself
// happens in another control and is announced by nobody.  See
// ui/command_dialog.h.
#define IDC_CMD_FILTER          1070
#define IDC_CMD_LIST            1071
#define IDC_CMD_DETAIL          1072

#define IDC_CMD_FILTER_LABEL    1080
#define IDC_CMD_LIST_LABEL      1081
#define IDC_CMD_DETAIL_LABEL    1082

// The key list.  One read-only multiline box and nothing else to fill in: the
// text is a constant in ui/keys_dialog.cpp, so there is no second control here
// and no state to keep.  See ui/keys_dialog.h.
#define IDC_KEYS_TEXT           1090
#define IDC_KEYS_TEXT_LABEL     1091

// Nová session.  See ui/new_session_dialog.h.
#define IDC_NEW_BACKEND         1100
#define IDC_NEW_MODEL           1101
#define IDC_NEW_PROJECT         1102
#define IDC_NEW_BROWSE          1103
#define IDC_NEW_CONVERSATION    1104

#define IDC_NEW_BACKEND_LABEL   1110
#define IDC_NEW_MODEL_LABEL     1111
#define IDC_NEW_PROJECT_LABEL   1112
#define IDC_NEW_CONVERSATION_LABEL 1113

// Hľadanie v prepise.  See ui/find_dialog.h.
#define IDC_FIND_TEXT           1120
#define IDC_FIND_TEXT_LABEL     1130

// O programe.  See ui/about_dialog.h.
#define IDC_ABOUT_TEXT          1140
#define IDC_ABOUT_PAGE          1141
#define IDC_ABOUT_TEXT_LABEL    1150

// Úlohy na pozadí.  See ui/tasks_dialog.h.
#define IDC_TASKS_LIST          1160
#define IDC_TASKS_LIST_LABEL    1161

// Schválenie plánu.  See ui/plan_dialog.h.
#define IDC_PLAN_TEXT           1170
#define IDC_PLAN_MODE           1171
#define IDC_PLAN_REVISE         1173

#define IDC_PLAN_TEXT_LABEL     1180
#define IDC_PLAN_MODE_LABEL     1181

// Prihlásenie agenta.  See ui/sign_in_dialog.h.
#define IDC_SIGNIN_STATUS       1190
#define IDC_SIGNIN_URL          1191
#define IDC_SIGNIN_CODE         1192
#define IDC_SIGNIN_ANSWER       1193
#define IDC_SIGNIN_OPEN         1194
#define IDC_SIGNIN_COPY         1195
#define IDC_SIGNIN_SUBMIT       1196

#define IDC_SIGNIN_STATUS_LABEL 1200
#define IDC_SIGNIN_URL_LABEL    1201
#define IDC_SIGNIN_CODE_LABEL   1202
#define IDC_SIGNIN_ANSWER_LABEL 1203

// Odkazy v bloku.  See ui/links_dialog.h.
#define IDC_LINKS_LIST          1210
#define IDC_LINKS_LIST_LABEL    1211

// Zmena modelu (F5).  See ui/model_dialog.h.
#define IDC_MODELS_LIST         1240
#define IDC_MODELS_LIST_LABEL   1241

// Formulár od MCP servera.  One field per dialog; of the three controls for
// the value, the one the field needs is shown.  See ui/form_dialog.h.
#define IDC_FORM_MESSAGE        1220
#define IDC_FORM_TEXT           1221
#define IDC_FORM_CHOICE         1222
#define IDC_FORM_CHOICES        1223
#define IDC_FORM_BACK           1224
#define IDC_FORM_DECLINE        1225
#define IDC_FORM_LINK           1226

#define IDC_FORM_MESSAGE_LABEL  1230
#define IDC_FORM_VALUE_LABEL    1231
#define IDC_FORM_LINK_LABEL     1232

#endif
