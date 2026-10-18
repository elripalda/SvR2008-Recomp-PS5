// WWE SmackDown vs. Raw 2008 PS5 - the hub's check of the game's files (set up by Recomp
// Store, or copied into the app's Assets folder by hand).
//
// Plain C++ (std::filesystem + POSIX reads): it also runs on a PC, for tests.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace svr2008::hub::install {

namespace fs = std::filesystem;

// The folder the game's files go in (<app>/Assets).
fs::path GameFolder(const fs::path& app);
// Where SmackDown vs. Raw 2008 is in it: Assets itself (the disc's files as they are).
fs::path GameRoot(const fs::path& app);

// A game file that didn't come over whole (missing, or not its full size).
struct FileProblem {
  std::string path;  // in the game folder
  uint64_t expected = 0, found = 0;
  bool missing = false;
};

// Checks the game folder against SvR 2008's file list (game_files.inc): every
// file the game reads there, at its full size. Logs what it finds.
std::vector<FileProblem> CheckGameFiles(const fs::path& app);

// What the app folder has.
struct State {
  bool xex = false;     // default.xex in the game folder (or a disc image in the app folder)
  bool xex_ok = false;  // ...the one this app was made from
  std::vector<FileProblem> problems;  // game files missing or cut short
  bool Installed() const { return xex; }
};
State Look(const fs::path& app);

// A job's progress, shared with the hub (atomics, and the strings under `m`).
struct Progress {
  std::atomic<uint64_t> done{0}, total{0};  // (the item being worked on)
  std::atomic<int> step{0};                 // which item
  std::atomic<bool> cancel{false}, finished{false};
  std::mutex m;
  std::string file;  // being worked on
  // when finished
  bool ok = false;
  std::string result;
  void SetFile(std::string f) {
    std::lock_guard l(m);
    file = std::move(f);
  }
  std::string File() {
    std::lock_guard l(m);
    return file;
  }
};

// A folder's entries (none if it can't be read; never throws).
std::vector<fs::directory_entry> ListDir(const fs::path& dir);

// "6.6 GB" etc. for messages.
std::string Bytes(uint64_t n);

}  // namespace svr2008::hub::install
