#ifndef UPDATER_H
#define UPDATER_H

// The part of the updates that talks to the network and to the reader:
// asking GitHub for the newest release, downloading and verifying the
// package, putting its files in place of the running ones and starting again
// (claude-gui-lkk.53).  The rules -- what to offer, how to read GitHub's
// answer, which file goes where -- are in update.h, where a test holds them.
//
// Replacing files works while they are in use: Windows refuses to overwrite a
// running image or a loaded DLL, but lets it be moved within the volume.  So
// the package is unpacked beside the EXE into .update-new, every file it
// replaces moves into .update-old, the new one takes its place, and the next
// start removes both folders.  Nothing is moved until the download matched
// the hash GitHub publishes for it and the unpacking reported success; if any
// move fails, every move made so far is undone.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace updater {

struct Latest {
  enum class Kind { kFound, kNoRelease, kFailed, kCancelled };
  Kind kind = Kind::kFailed;
  std::wstring tag;    // kFound: "v2026.10.2"
  std::wstring error;  // kFailed: why, in words for the reader
};

// Asks github.com which release is the newest.  timeoutMs caps each network
// phase; 0 leaves WinHTTP's defaults.  Blocking.
Latest FetchLatest(DWORD timeoutMs);

// The automatic check's cap on the whole question, DNS included, which
// WinHTTP's own timeouts do not cover: past this the start goes on and the
// answer, if it ever comes, is dropped.  A slow network must not hold up the
// session the reader started.
Latest FetchLatestWithin(DWORD milliseconds);

// The check the reader asks for -- from a menu that does not exist yet (it
// comes after localisation, claude-gui-lkk.52).  No cap: someone on a slow
// network chose to wait.  A dialog says what is going on, with Zrušiť, which
// gives kCancelled.
Latest FetchLatestAsked(HWND owner);

enum class Choice { kUpdate, kLater, kSkip };

// "Je k dispozícii verzia ...", with Aktualizovať, Neskôr and Preskočiť túto
// verziu.  Closing the dialog is Neskôr.
Choice AskToUpdate(HWND owner, const std::wstring& tag, bool restartsItself);

enum class Installed { kRestarted, kInstalled, kCancelled, kFailed };

// Downloads the package and SHA256SUMS.txt behind a progress dialog the
// reader can cancel, checks the hash, unpacks and puts the files in place.
// With `restart` it also starts the new EXE -- before the dialog closes, so
// that the new window gets the focus -- and kRestarted means this process
// should leave.  On kFailed a message has been shown, including the offer to
// open the release page when the folder cannot be written to.
Installed DownloadAndInstall(HWND owner, const std::wstring& tag, bool restart);

// The folders a previous update left behind.  Called at every start; failing
// is fine -- the old process may still be closing, and the next start tries
// again.
void RemoveLeftover();

// Local date "YYYY-MM-DD", the form the settings keep the last check in.
std::wstring Today();

// The steps below the dialogs, out here so a probe can run them on a folder
// of its own without the network.  Unpack runs the system's tar.exe; Install
// replaces the files of `dir` with those unpacked into `dir`\.update-new.
// Both give an empty string on success and the reason otherwise.
std::wstring Unpack(const std::wstring& dir, const std::string& zip);
enum class Written { kOk, kNotWritable, kFailed };
Written Install(const std::wstring& dir, const std::wstring& exeName,
                std::wstring& error);

}  // namespace updater

#endif
