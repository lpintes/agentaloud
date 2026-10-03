#ifndef PROTO_CLAUDE_SESSIONS_H
#define PROTO_CLAUDE_SESSIONS_H

// What conversations this project already has, read off the disk.
//
// It lives in proto/ for the same reason session.cpp does: the layout of
// ~/.claude/projects and the shape of the records in there are the CLI's, and
// they stop being true the moment the CLI changes.  It knows nothing about
// windows or transcripts -- it answers one question, "which conversation is
// the newest one here", and hands back what is needed to name it out loud.
//
// Why we make the list ourselves rather than let `claude -c` do it: the CLI's
// own idea of "the last conversation" comes from ~/.claude/history.jsonl, and
// only an interactively typed prompt is written there.  Every ClaudeLens
// session is headless, so none of ours is in it -- `claude -c` would carry on
// the last conversation held in a TERMINAL and call it ours.  The .jsonl files
// have no such gap: a headless session writes one exactly like any other.

#include <string>
#include <vector>

#include "proto/jsonl.h"

namespace proto {

// One conversation's file, summarized -- enough to sort the list and to say
// which one was picked.
//
// Two spellings on purpose: `id` goes on a command line (--resume) and is
// ASCII either way, while the prompt is text from the stream and stays UTF-8
// as far as the layer that renders it.  proto/ must not reach up into model/
// for Utf16FromUtf8, and doing the conversion here would be that in spirit.
struct SessionSummary {
  std::wstring id;          // file name without .jsonl; the handle --resume takes
  std::string lastStamp;    // ISO 8601 UTC of the last record that carries one
  std::string firstPrompt;  // UTF-8, whitespace collapsed; empty when none
};

// The directory name the CLI keeps a project's sessions under: every character
// outside [A-Za-z0-9] becomes a dash, which is where the double dash after the
// drive letter comes from.  Case is left alone -- both spellings occur on this
// machine, and NTFS does not care.
std::wstring ProjectKey(const std::wstring& projectPath);

// ~/.claude/projects/<key>, or CLAUDE_CONFIG_DIR/projects/<key> when that is
// set -- the CLI honours it and a lookup that ignored it would quietly read
// the wrong machine's worth of sessions.  Empty when the home is unknown.
std::wstring ProjectSessionDir(const std::wstring& projectPath);

// Summarizes one .jsonl.  False for a file that holds no user or assistant
// record: a conversation nobody ever had is not one to carry on.
//
// It reads the whole file but parses almost none of it -- the last stamp is
// found by walking records backwards from the end (the ones written at close,
// last-prompt and atis-latch, carry no timestamp) and the first prompt by
// walking forwards until one turns up.  Both are within a few records of their
// end in every file measured, so a two-megabyte session costs a read and about
// five parses.  A whole project of 27 sessions, 24 MB of them, was 35 ms.
bool ReadSessionSummary(const std::wstring& path, SessionSummary* out);

// Where one conversation of this project lives, whether or not the file is
// there.  --resume also takes a session TITLE, and a title is not a file name,
// so a path built from one simply will not exist -- which is an answer for the
// caller, not an error to report.
std::wstring SessionFilePath(const std::wstring& projectPath,
                             const std::wstring& id);

// The records of one session file that a transcript can be built from, in the
// order they were written.
//
// Filtered here and not by the caller, because which types the disk format and
// the stream have in common is this layer's knowledge: they share `user` and
// `assistant` and nothing else that makes a block.  The disk carries a dozen
// more types of its own (attachment, queue-operation, mode, permission-mode,
// file-history-snapshot ...) and lacks system/init, result and
// rate_limit_event entirely.  Handed to model/ unfiltered they would all count
// as records of an unknown type, which is a thing the soak tests watch for --
// the alarm would then be ringing for the ordinary case.
//
// Records of a subagent's conversation (isSidechain) are left out: they are a
// different conversation that happens to be filed here, and interleaved into
// this one they would read as if Claude had answered itself.  Measured over
// this machine's 191 session files, not one record has the flag set, so the
// line below has never yet had anything to do.
//
// False when the file cannot be read or holds no conversation at all, which is
// what the caller needs to know BEFORE it says out loud what it restored.
bool ReadSessionRecords(const std::wstring& path, std::vector<Json>* out);

// The text a human typed into this `user` record, or empty when nobody did --
// including when the record is not a `user` one at all.  An `assistant` record
// keeps its text in the very same place, and taken for a prompt it would put
// the answer in the transcript twice, once in each speaker's name.
//
// A user record is also where the CLI files tool results, the echo of a slash
// command, the caveat a hook printed and its own periodic context report.
// None of those was typed by anybody, and a transcript that showed them as
// prompts would be putting words in the reader's mouth.  The machine-written
// ones are told apart by their opening: a tag ("<command-name>",
// "<local-command-stdout>", "<local-command-caveat>") or the isMeta flag.
std::string HumanPromptText(const Json& record);

// Is this the CLI's own mark that a turn was cut short -- "[Request
// interrupted by user]", with or without "for tool use" on the end?
//
// It arrives as user text like a prompt does, and it is not one; the
// transcript has a block kind of its own for it, the same one the live path
// writes when the reader presses Esc.
bool IsInterruptMark(const Json& record);

// The project's newest conversation, or false when it has none.
//
// NEWEST BY THE LAST RECORD THAT CARRIES A TIME, not by the file's mtime.
// Closing a session appends records that have no timestamp, so mtime says when
// the file was last written rather than when the conversation last happened;
// for two sessions closed minutes apart it decides on something that has
// nothing to do with either.  ISO 8601 with a Z sorts correctly as text, which
// is why the stamp is never parsed for this.
bool LatestSession(const std::wstring& projectPath, SessionSummary* out);

// An ISO 8601 UTC stamp as local wall-clock time, "6. 9. 2026 14:32".  The
// stamp is handed back unchanged when it cannot be read, because a stamp the
// reader can puzzle over beats a blank where the time should be.
std::wstring LocalTimeText(const std::string& isoStamp);

}  // namespace proto

#endif
