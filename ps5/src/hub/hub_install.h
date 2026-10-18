// WWE SmackDown vs. Raw 2008 - the PS5 hub's installer: the game's files
// from the player's own Xbox 360 disc image (.iso), copied into the app's
// Assets folder, so the app itself ships without any game files. (The
// SmackDown vs. Raw 2011 port's installer, for one disc and no PC files.)
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

// A disc image found (.iso / .xiso).
struct DiscImage {
  fs::path file;
  uint64_t bytes = 0;
  enum State { kThisGame, kOtherVersion, kOtherGame, kNotADisc } state = kNotADisc;
  std::string note;        // why it can't be used
  uint64_t game_bytes = 0;  // what an install copies from it
  int game_files = 0;
};

// Looks for disc images in the app folder (and the folders in it), /data and
// the console's USB drives, and checks each.
std::vector<DiscImage> Scan(const fs::path& app);

// The folder the game's files go in (<app>/Assets).
fs::path GameFolder(const fs::path& app);

// What the app folder has.
struct State {
  bool xex = false;         // Assets/default.xex
  bool xex_ok = false;      // ...the one this app was made from
  bool incomplete = false;  // an install that stopped part-way
  bool Installed() const { return xex && !incomplete; }
};
State Look(const fs::path& app);

// A job's progress, shared with the hub (atomics, and the strings under `m`).
struct Progress {
  std::atomic<uint64_t> done{0}, total{0};
  std::atomic<int> step{0};  // 0 reading the disc, 1 copying, 2 finishing
  std::atomic<bool> cancel{false}, finished{false};
  std::mutex m;
  std::string file;  // being copied
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

// Copies the disc's files into <app>/Assets (on a thread of the caller's);
// files already there whole are kept, so a stopped install goes on.
void Install(const fs::path& image, const fs::path& app, Progress& p);

// A folder's entries (none if it can't be read; never throws).
std::vector<fs::directory_entry> ListDir(const fs::path& dir);

// "6.6 GB" etc. for messages.
std::string Bytes(uint64_t n);

}  // namespace svr2008::hub::install
