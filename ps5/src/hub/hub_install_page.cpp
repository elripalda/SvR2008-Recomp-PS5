// WWE SmackDown vs. Raw 2008 - the PS5 hub's Install page (see hub_ui.h):
// the disc images found, the install on a thread and its progress. (The
// SmackDown vs. Raw 2011 port's Install page, for one disc and no PC files.)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>

#include "hub_style.h"
#include "hub_ui.h"

namespace svr2008::hub {

namespace fs = std::filesystem;

// The install, on its thread (one at a time).
struct Hub::Task {
  std::thread thread;
  install::Progress p;
  ~Task() {
    p.cancel = true;  // (the hub closing: stops at the next piece)
    if (thread.joinable()) thread.join();
  }
};

namespace {

// Where a file is, for people: the app folder, a USB drive...
std::string Where(const fs::path& file, const fs::path& app) {
  const std::string dir = file.parent_path().string(), g = app.string();
  if (dir == g || dir.rfind(g + "/", 0) == 0) return dir == g ? "The app folder" : "The app folder: " + dir.substr(g.size() + 1);
  if (dir.rfind("/mnt/usb", 0) == 0) return "USB drive " + std::to_string(dir.size() > 8 ? dir[8] - '0' + 1 : 1);
  if (dir.rfind("/mnt/ext", 0) == 0) return "Extended storage";
  return dir;
}

constexpr const char* kSteps[] = {"Read the disc image", "Copy the game files", "Finish"};

}  // namespace

void Hub::LoadInstall(bool scan) {
  state_ = install::Look(paths_.app);
  if (scan) images_ = install::Scan(paths_.app);
}

void Hub::StartInstall(const fs::path& image) {
  if (task_) return;
  auto t = std::make_shared<Task>();
  task_file_ = image;
  task_speed_ = 0, task_last_ = 0, task_in_ = 0;
  Task* raw = t.get();
  const fs::path app = paths_.app;
  t->thread = std::thread([raw, image, app] { install::Install(image, app, raw->p); });
  task_ = std::move(t);
}

void Hub::StopTask() {
  if (task_) task_->p.cancel = true;
}

void Hub::PollTask() {
  if (!task_) return;
  // the speed, smoothed
  const uint64_t done = task_->p.done;
  if (dt_ > 0 && done >= task_last_ && task_last_) {
    const float now = float(done - task_last_) / dt_;
    task_speed_ = task_speed_ <= 0 ? now : task_speed_ + (now - task_speed_) * (1 - std::exp(-dt_ * 1.5f));
  }
  task_last_ = done ? done : 1;
  if (!task_->p.finished || confirm_) return;  // (a question open: the outcome waits)
  std::shared_ptr<Task> t = std::move(task_);
  if (t->thread.joinable()) t->thread.join();
  LoadInstall(true);
  install::Progress& p = t->p;
  if (!p.ok) {
    Play(Sound::kError);
    Notice(p.cancel ? "Install stopped" : "The install didn't finish", p.result);
    return;
  }
  Play(Sound::kStart);
  std::error_code ec;
  const fs::path image = task_file_;
  if (fs::exists(image, ec)) {
    const uint64_t bytes = fs::file_size(image, ec);
    Confirm("The game is installed",
            "It's in the Assets folder: press Options to start it. Delete the disc image now to free " + Size(bytes) +
                "? It isn't needed any more, unless you'd like to install again later.",
            "Delete It", [this, image] {
              std::error_code e2;
              fs::remove(image, e2);
              Toast(e2 ? "The disc image couldn't be deleted" : "Disc image deleted");
              LoadInstall(true);
              item_[tab_] = 0;
            });
  } else {
    Toast(p.result);
  }
}

void Hub::InstallActivate(int what) {
  if (task_) return;
  if (what == 2) {  // look again
    LoadInstall(true);
    Play(Sound::kSelect);
    const size_t n = images_.size();
    Toast(n ? std::to_string(n) + (n == 1 ? " disc image found" : " disc images found") : "No disc image found");
    return;
  }
  const int sel = item_[tab_];
  if (sel < 0 || sel >= int(images_.size())) return;
  const install::DiscImage d = images_[size_t(sel)];
  if (what == 1) {  // delete the file
    Play(Sound::kSelect);
    Confirm("Delete this disc image?", d.file.filename().string() + " is deleted from the console, freeing " +
                                           Size(d.bytes) + ".",
            "Delete", [this, d] {
              std::error_code e2;
              fs::remove(d.file, e2);
              Toast(e2 ? "It couldn't be deleted" : d.file.filename().string() + " deleted");
              LoadInstall(true);
              item_[tab_] = 0;
            });
    return;
  }
  if (d.state != install::DiscImage::kThisGame) {
    Play(Sound::kError);
    Notice("This disc image can't be used", d.note.empty() ? "It can't be read." : d.note);
    return;
  }
  Play(Sound::kSelect);
  StartInstall(d.file);
}

float Hub::InstallHints(float r) {
  if (task_) return Hint(r, kCircle, "Stop");
  r = Hint(r, kCircle, "Home");
  r = Hint(r, kSquare, "Look Again");
  if (images_.empty()) return r;
  r = Hint(r, kTriangle, "Delete File");
  return Hint(r, kCross, state_.Installed() ? "Install Again" : "Install");
}

void Hub::Empty(const char* title, const std::string& text) {
  Text(a_.display, 40, kListX, kListY + 40, kText, title);
  Wrapped(a_.body, 23, kListX, kListY + 110, 820, White(0.65f), text);
}

void Hub::DrawInstall() {
  if (task_) {
    DrawInstallProgress();
    return;
  }
  std::string sub;
  if (state_.Installed())
    sub = state_.xex_ok ? "The game is installed." : "The game is installed, but its default.xex isn't the one this app needs.";
  else if (state_.incomplete)
    sub = "An install stopped part-way: install again to go on where it stopped.";
  else
    sub = "Set the game up from your own disc image. It takes a few minutes.";
  PageTitle("INSTALL", sub);
  const std::string how =
      "Copy your SmackDown vs. Raw 2008 Xbox 360 disc image (.iso) over FTP into /data/homebrew/PPSA92008/, or put it on "
      "a USB drive, then press Square to look again. The hub copies the game into the Assets folder; the disc image "
      "can be deleted afterwards.";
  if (images_.empty()) {
    Empty(state_.Installed() ? "No disc image here" : "Add your disc image",
          state_.Installed() ? "The game is installed, so none is needed. To install again: " + how : how);
    return;
  }
  const int sel = item_[tab_] = std::clamp(item_[tab_], 0, int(images_.size()) - 1);
  std::vector<Row> list;
  list.push_back({-1, "DISC IMAGES"});
  for (int i = 0; i < int(images_.size()); ++i) list.push_back({i});
  const float x = kListX, w = kListW;
  List(list, sel, 76, [&](int i, float ry, float over) {
    const install::DiscImage& d = images_[size_t(i)];
    const bool ok = d.state == install::DiscImage::kThisGame;
    const std::string st = !ok                       ? (d.state == install::DiscImage::kOtherVersion ? "Other version"
                                                         : d.state == install::DiscImage::kOtherGame  ? "Other game"
                                                                                                      : "Can't be used")
                           : state_.Installed() ? "Installed"
                                                : "Ready";
    const bool lit = ok && !state_.Installed();
    Text(a_.body_bold, 24, x + 24, ry + 9, Mix(White(0.94f), kInk, over), Fit(a_.body_bold, 24, d.file.stem().string(), w - 260));
    Text(a_.body, 18, x + 24, ry + 41, Mix(White(0.42f), Black(0.55f), over),
         Fit(a_.body, 18, Size(d.bytes) + "  \xC2\xB7  " + Where(d.file, paths_.app), w - 260));
    const float sx = x + w - 24 - TextWidth(a_.body_bold, 18, st);
    if (lit) dl_->AddCircleFilled(P(sx - 14, ry + 35), 4.5f * s_, WithAlpha(kAccent, 0.6f + 0.4f * std::sin(time_ * 3)), 16);
    Text(a_.body_bold, 18, sx, ry + 24, Mix(White(lit ? 0.9f : 0.5f), Black(0.7f), over), st);
  });
  const install::DiscImage& d = images_[size_t(sel)];
  Panel([&](float px, float py, float pw) {
    py += Wrapped(a_.display, 44, px, py, pw, kText, d.file.stem().string(), 1.15f) + 22;
    const bool ok = d.state == install::DiscImage::kThisGame;
    py = Facts(px, py, pw,
               {{"GAME", ok ? "SmackDown vs. Raw 2008" : d.note},
                {"SIZE", Size(d.bytes)},
                {"WHERE", Where(d.file, paths_.app)},
                {"INSTALLS", ok ? std::to_string(d.game_files) + " files, " + Size(d.game_bytes) : "-"}});
    if (ok)
      Wrapped(a_.body, 21, px, py + 10, pw, White(0.55f),
              state_.Installed() ? "The game is installed. Installing again only copies the files that are missing."
                                 : "The game's files are copied into the Assets folder. Then press Options to start it.");
  });
}

void Hub::DrawInstallProgress() {
  install::Progress& p = task_->p;
  const int step = std::clamp(int(p.step), 0, 2);
  PageTitle("INSTALLING", task_file_.filename().string());
  Ease(task_in_, 1, 6);
  const uint64_t done = p.done, total = p.total;
  const float frac = total ? float(double(done) / double(total)) : 0.0f;
  const float x = kListX, y = 300;
  Text(a_.body_bold, 16, x, y, White(0.5f), "STEP " + std::to_string(step + 1) + " OF 3  \xC2\xB7  " + Upper(kSteps[step]), 0, 3);
  char pct[16];
  std::snprintf(pct, sizeof(pct), "%d%%", int(frac * 100));
  Text(a_.display, 140, x - 6, y + 34, kText, pct);
  // the bar, with a sheen running along it
  const float bw = 860, by = y + 206;
  Rect(x, by, bw, 10, White(0.12f), 5);
  static float shown = 0;
  shown = shown > frac ? frac : shown + (frac - shown) * (1 - std::exp(-dt_ * 8));
  const float fw = std::max(10.0f, bw * shown);
  Rect(x, by, fw, 10, kAccent, 5);
  const float sheen = std::fmod(time_ * 0.6f, 1.4f) - 0.2f;
  const float sx = x + fw * sheen;
  if (sheen > 0 && sheen < 1)
    dl_->AddRectFilledMultiColor(P(std::max(x, sx - 60), by), P(std::min(x + fw, sx + 60), by + 10), White(0), White(0.35f),
                                 White(0.35f), White(0));
  std::string line = install::Bytes(done) + " of " + install::Bytes(total);
  if (task_speed_ > 1 << 20) {
    char sp[32];
    std::snprintf(sp, sizeof(sp), "%.0f MB/s", task_speed_ / 1048576.0f);
    line += std::string("  \xC2\xB7  ") + sp;
    const float left = float(total - std::min(done, total)) / task_speed_;
    if (left > 1) {
      line += "  \xC2\xB7  ";
      line += left >= 90 ? "about " + std::to_string(int(left / 60 + 0.5f)) + " min left" : std::to_string(int(left) + 1) + " s left";
    }
  }
  Text(a_.body_bold, 22, x, by + 34, White(0.85f), line);
  Text(a_.body, 19, x, by + 76, White(0.45f), Fit(a_.body, 19, p.File(), bw));
  // the steps, on the right
  Panel([&](float px, float py, float pw) {
    Text(a_.body_bold, 15, px, py + 5, White(0.45f), "STEPS", 0, 3);
    py += 52;
    for (int k = 0; k < 3; ++k) {
      const bool now = k == step, past = k < step;
      const float cy = py + 14;
      if (past) {
        dl_->AddCircleFilled(P(px + 12, cy), 12 * s_, White(0.9f), 24);
        dl_->AddLine(P(px + 6, cy), P(px + 10.5f, cy + 5), kInk, 2.4f * s_);
        dl_->AddLine(P(px + 10.5f, cy + 5), P(px + 18, cy - 5), kInk, 2.4f * s_);
      } else if (now) {
        const float a0 = time_ * 5;
        dl_->PathArcTo(P(px + 12, cy), 11 * s_, a0, a0 + 4.2f, 24);
        dl_->PathStroke(kAccent, 0, 3 * s_);
      } else {
        dl_->AddCircle(P(px + 12, cy), 11 * s_, White(0.25f), 24, 1.6f * s_);
      }
      Text(now ? a_.body_bold : a_.body, 22, px + 44, py, now || past ? White(now ? 0.95f : 0.6f) : White(0.35f), kSteps[k]);
      py += 56;
    }
    Wrapped(a_.body, 19, px, py + 20, pw, White(0.45f),
            "You can look around the other pages meanwhile. If it stops, install again: it goes on where it stopped.");
  });
}

}  // namespace svr2008::hub
