#ifndef PROTO_SESSIONS_H
#define PROTO_SESSIONS_H

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
