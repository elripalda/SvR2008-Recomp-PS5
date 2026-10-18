// WWE SmackDown vs. Raw 2008 - the PS5 hub (see hub_ui.h): the frame, the
// Home, Settings, Saves and About pages, and the controls. (The SmackDown
// vs. Raw 2011 port's hub, cut down to what the 2008 game has.)
#include "hub_ui.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <utility>

#include "hub_style.h"

namespace svr2008::hub {

namespace fs = std::filesystem;

static const char* const kTabNames[kTabCount] = {"HOME", "SETTINGS", "SAVES", "ABOUT"};

namespace {

std::string Unquote(std::string v) {
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
  return v;
}

// A folder's entries (none when it can't be read).
std::vector<fs::directory_entry> ListDir(const fs::path& dir) {
  std::vector<fs::directory_entry> out;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) out.push_back(*it);
  return out;
}

bool Hex(const std::string& s, size_t n) {
  if (s.size() != n) return false;
  for (char c : s)
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  return true;
}

int64_t Modified(const fs::path& p) {
  struct stat st {};
  return stat(p.string().c_str(), &st) == 0 ? int64_t(st.st_mtime) : 0;
}

// The newest change in a folder (its files').
int64_t NewestIn(const fs::path& dir) {
  int64_t t = Modified(dir);
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_regular_file(ec)) t = std::max(t, Modified(it->path()));
  return t;
}

std::string Date(int64_t t) {
  if (!t) return "-";
  const std::time_t tt = std::time_t(t);
  std::tm tm{};
  localtime_r(&tt, &tm);
  char buf[48];
  std::strftime(buf, sizeof(buf), "%b %d, %Y  %H:%M", &tm);
  return buf;
}

// The profile folders in UserData (16 hex digits: the game's saves are in them).
std::vector<fs::path> Profiles(const fs::path& user_data) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (const auto& e : ListDir(user_data))
    if (e.is_directory(ec) && Hex(e.path().filename().string(), 16)) out.push_back(e.path());
  return out;
}

// Copies a folder's contents (recursively) into another. False on any error.
bool CopyInto(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to, ec);
  if (ec) return false;
  for (const auto& e : ListDir(from)) {
    std::error_code ec2;
    fs::copy(e.path(), to / e.path().filename(), fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec2);
    if (ec2) return false;
  }
  return true;
}

}  // namespace

// ----------------------------------------------------------------- settings

struct Hub::Setting {
  const char* section;
  const char* key;
  const char* label;
  const char* short_label;  // (Home's numbers; null: not shown there)
  const char* help;
  bool quoted;                      // a TOML string
  std::vector<const char*> values;  // as written to the file
  std::vector<const char*> labels;
  const char* fallback;             // the value when the file has none
};

// The settings (svr2008.toml): the PC port's settings menu, the ones that apply on the PS5.
static const std::vector<Hub::Setting>& Settings() {
  static const std::vector<Hub::Setting> rows = {
      {"DISPLAY", "svr_render_scale", "Rendering Quality", "QUALITY",
       "How sharp the game is drawn. High draws it at twice the Xbox 360's resolution (1440p) and is the "
       "recommended setting. Ultra draws it at 4K, which the PS5 sends to the TV either way.",
       false, {"1", "2", "3"}, {"Original (720p)", "High - 2x (1440p)", "Ultra - 3x (4K)"}, "2"},
      {"DISPLAY", "svr_60fps", "Frame Rate", "FPS",
       "60 keeps matches and cutscenes at the game's own 60 fps mode (the game runs at the right speed either "
       "way). 30 is the original.",
       false, {"true", "false"}, {"60 fps", "30 fps (original)"}, "true"},
      {"DISPLAY", "bd_anisotropy", "Anisotropic Filter", nullptr,
       "Keeps the ring, the mat and the crowd sharp when seen at an angle.", false, {"0", "2", "4", "8", "16"},
       {"Off", "2x", "4x", "8x", "16x"}, "16"},
      {"DISPLAY", "svr_fps_counter", "FPS Counter", nullptr,
       "Shows the frame rate in the top-left corner while you play.", false, {"true", "false"}, {"On", "Off"},
       "true"},
      {"DISPLAY", "svr_display_ramp", "Console Gamma", nullptr,
       "Shows the picture through the Xbox 360's own display gamma, the way the console sends it to a TV. Off "
       "shows the frame as the game drew it.",
       false, {"true", "false"}, {"On", "Off"}, "true"},
      {"DISPLAY", "svr_bloom", "Bloom", nullptr,
       "How much bright lights glow. Original is the Xbox 360's full bloom, which washes out the arena in the "
       "entrance wide shots. Soft is half of it.",
       false, {"100", "50", "0"}, {"Original", "Soft", "Off"}, "50"},
      {"DISPLAY", "svr_edge_blur", "Original Edge Blur", nullptr,
       "The console's own anti-aliasing filter, which softens hair, ropes and the crowd. Off keeps the picture "
       "sharp; the higher resolution smooths edges instead.",
       false, {"false", "true"}, {"Off", "On"}, "false"},
  };
  return rows;
}

// --------------------------------------------------------------------- data

void Hub::Init(const Paths& paths, const Assets& assets, std::string version) {
  paths_ = paths;
  a_ = assets;
  version_ = std::move(version);
  Load();
}

void Hub::Load() {
  config_.clear();
  std::ifstream in(paths_.config);
  for (std::string line; std::getline(in, line);) {
    const size_t a = line.find_first_not_of(" \t");
    if (a == std::string::npos || line[a] == '#') continue;
    if (line[a] == '[') break;  // (top-level keys only)
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(a, eq - a), v = line.substr(eq + 1);
    k.erase(k.find_last_not_of(" \t") + 1);
    v.erase(0, v.find_first_not_of(" \t"));
    if (const size_t hash = v.find(" #"); hash != std::string::npos && v[0] != '"') v.erase(hash);
    v.erase(v.find_last_not_of(" \t\r") + 1);
    config_.emplace_back(k, v);
  }
  LoadSaves();
  // (the game's files come from the app store: only whether they're there)
  LoadInstall(false);
}

void Hub::LoadSaves() {
  save_items_.clear();
  backups_.clear();
  std::error_code ec;
  // UserData/<profile>/<title>/<content type>/<name>: one save each
  for (const fs::path& profile : Profiles(paths_.user_data)) {
    for (const auto& title : ListDir(profile)) {
      if (!title.is_directory(ec) || !Hex(title.path().filename().string(), 8)) continue;
      for (const auto& type : ListDir(title.path())) {
        const std::string t = type.path().filename().string();
        if (!type.is_directory(ec) || !Hex(t, 8)) continue;  // (not Headers)
        for (const auto& pkg : ListDir(type.path())) {
          if (!pkg.is_directory(ec)) continue;
          SaveItem s;
          s.folder = pkg.path();
          s.name = pkg.path().filename().string();
          // (the game's folder names, for people: "0Saved Game" -> "Saved Game", "exdata" -> "Extra Data")
          if (s.name.size() > 1 && std::isdigit(static_cast<unsigned char>(s.name[0])) &&
              std::isalpha(static_cast<unsigned char>(s.name[1])))
            s.name.erase(0, 1);
          if (s.name == "exdata") s.name = "Extra Data";
          s.kind = t == "00000001" ? "Saved game" : t == "00000002" ? "Downloadable content" : "Game data";
          s.bytes = FolderBytes(pkg.path(), &s.files);
          s.modified = NewestIn(pkg.path());
          save_items_.push_back(std::move(s));
        }
      }
    }
  }
  std::sort(save_items_.begin(), save_items_.end(), [](const SaveItem& a, const SaveItem& b) { return a.name < b.name; });
  for (const auto& e : ListDir(paths_.app / "SaveBackups")) {
    if (!e.is_directory(ec)) continue;
    Backup b;
    b.name = e.path().filename().string();
    b.bytes = FolderBytes(e.path(), &b.files);
    backups_.push_back(std::move(b));
  }
  // newest first (their names start with the date)
  std::sort(backups_.begin(), backups_.end(), [](const Backup& a, const Backup& b) { return a.name > b.name; });
}

void Hub::BackUpSaves(const std::string& why) {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H-%M-%S", &tm);
  const fs::path root = paths_.app / "SaveBackups";
  fs::path dest = root / (std::string(stamp) + why);
  std::error_code ec;
  for (int k = 2; fs::exists(dest, ec); ++k) dest = root / (std::string(stamp) + why + " " + std::to_string(k));
  bool ok = true;
  for (const fs::path& profile : Profiles(paths_.user_data)) ok = CopyInto(profile, dest / profile.filename()) && ok;
  if (ok) {
    if (why.empty()) Toast("Saves backed up");
  } else {
    Toast("The backup couldn't be made");
    Play(Sound::kError);
  }
  LoadSaves();
}

void Hub::RestoreSaves(const Backup& b) {
  BackUpSaves(" (before restore)");
  for (const fs::path& profile : Profiles(paths_.user_data)) {
    std::error_code ec;
    fs::remove_all(profile, ec);
  }
  if (CopyInto(paths_.app / "SaveBackups" / b.name, paths_.user_data)) {
    Toast("Backup restored");
    Play(Sound::kOn);
  } else {
    Toast("Restoring didn't finish: your saves are in the newest backup");
    Play(Sound::kError);
  }
  LoadSaves();
  item_[kSavesTab] = 0;
}

std::string Hub::Value(const std::string& key, const std::string& fallback) const {
  for (const auto& [k, v] : config_)
    if (k == key) return Unquote(v);
  return fallback;
}

void Hub::Save(const std::string& key, const std::string& value) {
  bool found = false;
  for (auto& [k, v] : config_)
    if (k == key) v = value, found = true;
  if (!found) config_.emplace_back(key, value);
  // The file: that key's line replaced (or added before the first [section]).
  std::vector<std::string> lines;
  {
    std::ifstream in(paths_.config);
    for (std::string line; std::getline(in, line);) lines.push_back(line);
  }
  const std::string entry = key + " = " + value;
  size_t insert_at = lines.size();
  bool done = false;
  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& l = lines[i];
    const size_t first = l.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    if (l[first] == '[') {
      insert_at = std::min(insert_at, i);
      break;
    }
    const size_t eq = l.find('=');
    if (eq == std::string::npos || l[first] == '#') continue;
    std::string k = l.substr(first, eq - first);
    k.erase(k.find_last_not_of(" \t") + 1);
    if (k == key) {
      lines[i] = entry;
      done = true;
      break;
    }
  }
  if (!done) {
    // (before the comment lines over the first [section])
    while (insert_at > 0 && insert_at < lines.size() &&
           lines[insert_at - 1].find_first_not_of(" \t") != std::string::npos &&
           lines[insert_at - 1][lines[insert_at - 1].find_first_not_of(" \t")] == '#')
      --insert_at;
    lines.insert(lines.begin() + std::ptrdiff_t(insert_at), entry);
  }
  const fs::path tmp = paths_.config.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    for (const std::string& l : lines) out << l << "\n";
  }
  std::error_code ec;
  fs::rename(tmp, paths_.config, ec);
}

void Hub::NowPlaying(const std::string&) {
  // (songs change quietly: no name on the screen)
}

// ------------------------------------------------------------------ drawing

float Hub::Ease(float& v, float target, float speed) {
  v = v < -0.5f ? target : v + (target - v) * (1.0f - std::exp(-dt_ * speed));
  return v;
}

void Hub::Fade(int first_vertex, float alpha) {
  if (alpha >= 0.999f) return;
  for (int i = first_vertex; i < dl_->VtxBuffer.Size; ++i) {
    ImU32& c = dl_->VtxBuffer[i].col;
    c = WithAlpha(c, alpha);
  }
}

void Hub::Text(ImFont* f, float size, float x, float y, ImU32 col, const std::string& t, float align, float spacing) {
  if (!f) f = ImGui::GetFont();
  float px = x - TextWidth(f, size, t, spacing) * align;
  if (spacing <= 0) {
    dl_->AddText(f, size * s_, P(px, y), col, t.c_str());
    return;
  }
  // (letter by letter: a UTF-8 letter's bytes stay together)
  for (size_t i = 0; i < t.size();) {
    size_t n = 1;
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    const char* a = t.c_str() + i;
    const char* b = t.c_str() + std::min(t.size(), i + n);
    dl_->AddText(f, size * s_, P(px, y), col, a, b);
    px += f->CalcTextSizeA(size, FLT_MAX, 0, a, b).x + spacing;
    i += n;
  }
}

float Hub::TextWidth(ImFont* f, float size, const std::string& t, float spacing) const {
  if (!f) f = ImGui::GetFont();
  if (spacing <= 0) return f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str()).x;
  float w = 0;
  for (size_t i = 0; i < t.size();) {
    size_t n = 1;
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    w += f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str() + i, t.c_str() + std::min(t.size(), i + n)).x + spacing;
    i += n;
  }
  return t.empty() ? 0 : w - spacing;
}

std::string Hub::Fit(ImFont* f, float size, const std::string& t, float width) const {
  if (TextWidth(f, size, t) <= width) return t;
  std::string s = t;
  while (!s.empty() && TextWidth(f, size, s + "...") > width) {
    s.pop_back();
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();  // (whole UTF-8 letters)
  }
  return s + "...";
}

// Text broken into lines of at most `width`; returns the height used.
float Hub::Wrapped(ImFont* f, float size, float x, float y, float width, ImU32 col, const std::string& t, float line) {
  std::istringstream words(t);
  std::string word, cur;
  float yy = y;
  while (words >> word) {
    const std::string next = cur.empty() ? word : cur + " " + word;
    if (!cur.empty() && TextWidth(f, size, next) > width) {
      Text(f, size, x, yy, col, cur);
      yy += size * line;
      cur = word;
    } else {
      cur = next;
    }
  }
  if (!cur.empty()) Text(f, size, x, yy, col, Fit(f, size, cur, width)), yy += size * line;
  return yy - y;
}

void Hub::Rect(float x, float y, float w, float h, ImU32 col, float r) {
  dl_->AddRectFilled(P(x, y), P(x + w, y + h), col, r * s_);
}

// The DualSense's buttons: the pictures in hub/buttons, else drawn in one colour.
void Hub::Glyph(uint32_t button, float cx, float cy, ImU32 col) {
  if (auto it = a_.buttons.find(button); it != a_.buttons.end() && it->second) {
    const float h = 24;  // (the pictures' buttons fill about two thirds of them)
    dl_->AddImage(it->second.id, P(cx - h, cy - h), P(cx + h, cy + h), ImVec2(0, 0), ImVec2(1, 1), col);
    return;
  }
  const ImVec2 c = P(cx, cy);
  const float R = 15 * s_, k = 6.2f * s_, t = 2.2f * s_;
  switch (button) {
    case kCross:
    case kCircle:
    case kSquare:
    case kTriangle:
      dl_->AddCircle(c, R, col, 40, 1.6f * s_);
      if (button == kCross) {
        dl_->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), col, t);
        dl_->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), col, t);
      } else if (button == kCircle) {
        dl_->AddCircle(c, k * 1.1f, col, 28, t);
      } else if (button == kSquare) {
        dl_->AddRect(ImVec2(c.x - k * 0.95f, c.y - k * 0.95f), ImVec2(c.x + k * 0.95f, c.y + k * 0.95f), col, 0, 0, t);
      } else {
        dl_->AddTriangle(ImVec2(c.x, c.y - k * 1.15f), ImVec2(c.x + k * 1.1f, c.y + k * 0.8f),
                         ImVec2(c.x - k * 1.1f, c.y + k * 0.8f), col, t);
      }
      break;
    case kLeft | kRight: {
      const float a = 7 * s_, gap = 5 * s_;
      dl_->AddTriangleFilled(ImVec2(c.x - gap, c.y - a), ImVec2(c.x - gap, c.y + a), ImVec2(c.x - gap - a * 1.2f, c.y), col);
      dl_->AddTriangleFilled(ImVec2(c.x + gap, c.y - a), ImVec2(c.x + gap, c.y + a), ImVec2(c.x + gap + a * 1.2f, c.y), col);
      break;
    }
    case kOptions:
      dl_->AddRect(ImVec2(c.x - 13 * s_, c.y - 11 * s_), ImVec2(c.x + 13 * s_, c.y + 11 * s_), col, 6 * s_, 0, 1.6f * s_);
      for (int i = -1; i <= 1; ++i)
        dl_->AddLine(ImVec2(c.x - 6 * s_, c.y + i * 4.5f * s_), ImVec2(c.x + 6 * s_, c.y + i * 4.5f * s_), col, 1.8f * s_);
      break;
    case kR3:
      dl_->AddCircle(c, R, col, 40, 1.6f * s_);
      dl_->AddCircleFilled(c, R - 4 * s_, col, 40);
      Text(a_.body_bold, 13, cx, cy - 8.5f, Black(1), "R3", 0.5f);
      break;
    case kL1:
    case kR1:
      dl_->AddRect(ImVec2(c.x - 20 * s_, c.y - 13 * s_), ImVec2(c.x + 20 * s_, c.y + 13 * s_), col, 7 * s_, 0, 1.6f * s_);
      Text(a_.body_bold, 15, cx, cy - 9.5f, col, button == kL1 ? "L1" : "R1", 0.5f);
      break;
  }
}

// One hint at the bottom, right-aligned at `right`: the button, then what it does.
float Hub::Hint(float right, uint32_t button, const char* label) {
  const float tw = TextWidth(a_.body_bold, 20, label);
  const float gw = (button == kL1 || button == kR1) ? 40 : 30;
  const float x = right - tw;
  Text(a_.body_bold, 20, x, 1010, White(0.78f), label);
  Glyph(button, x - 12 - gw * 0.5f, 1023, White(0.9f));
  return x - 12 - gw - 40;
}

void Hub::PageTitle(const char* title, const std::string& subtitle) {
  Text(a_.display, 60, 112, 132, kText, title, 0, 1);
  Text(a_.body, 22, 114, 214, White(0.6f), subtitle);
}

void Hub::List(std::vector<Row>& rows, int sel, float rh, const std::function<void(int, float, float)>& draw) {
  const float hh = 50, view = kListView, x = kListX, w = kListW, y0 = kListY;
  float yy = 0;
  int sel_row = -1;
  for (size_t k = 0; k < rows.size(); ++k) {
    Row& r = rows[k];
    if (r.item < 0) {
      yy += k ? 14 : 0;
      r.y = yy;
      yy += hh;
    } else {
      r.y = yy;
      if (r.item == sel) sel_row = int(k);
      yy += rh;
    }
  }
  const float total = yy;
  if (sel_row < 0) return;
  // keep the focused row in view (with its heading, when it's the first under it)
  {
    float top = rows[size_t(sel_row)].y, bottom = top + rh;
    if (sel_row > 0 && rows[size_t(sel_row) - 1].item < 0) top = rows[size_t(sel_row) - 1].y;
    float target = scroll_px_;
    if (top < target) target = top;
    if (bottom > target + view) {
      // (to the first row or heading that keeps it in view: no row cut in half at the top)
      target = bottom - view;
      for (const Row& r : rows)
        if (r.y >= target - 0.5f) {
          target = r.y;
          break;
        }
    }
    scroll_px_ = std::clamp(target, 0.0f, std::max(0.0f, total - view));
  }
  Ease(scroll_f_, scroll_px_, 18);
  Ease(focus_y_, rows[size_t(sel_row)].y, 20);
  dl_->PushClipRect(P(0, y0 - 2), P(kW, y0 + view), true);
  const float push = press_ * 4;
  Rect(x + push, y0 + focus_y_ - scroll_f_ + push * 0.5f, w - push * 2, rh - 6 - push, kText, 10);
  for (size_t k = 0; k < rows.size(); ++k) {
    const Row& row = rows[k];
    const float ry = y0 + row.y - scroll_f_;
    if (ry < y0 - rh - 20 || ry > y0 + view + 20) continue;
    if (row.item < 0) {
      Text(a_.body_bold, 15, x + 24, ry + 20, White(0.45f), row.heading, 0, 3);
      continue;
    }
    const float over = std::clamp(1 - std::fabs(focus_y_ - row.y) / (rh * 0.7f), 0.0f, 1.0f);
    // (a thin line between rows that sit together)
    if (k + 1 < rows.size() && rows[k + 1].item >= 0)
      Rect(x + 24, ry + rh - 3, w - 48, 1, White(0.06f * (1 - over)));
    draw(row.item, ry, over);
  }
  dl_->PopClipRect();
  if (total > view) {  // where the list is
    const float bh = view * view / total;
    const float by = y0 + (view - bh) * scroll_f_ / (total - view);
    Rect(x + w + 16, y0, 3, view - 6, White(0.08f), 1.5f);
    Rect(x + w + 16, by, 3, bh - 6, White(0.5f), 1.5f);
  }
}

void Hub::Panel(const std::function<void(float, float, float)>& draw) {
  const int v0 = dl_->VtxBuffer.Size;
  draw(kPanelX + (1 - Smooth(panel_in_)) * 16, kPanelY, kPanelW);
  Fade(v0, Smooth(panel_in_));
}

float Hub::Facts(float x, float y, float w, const std::vector<std::pair<std::string, std::string>>& facts) {
  for (const auto& [k, v] : facts) {
    Text(a_.body_bold, 15, x, y + 5, White(0.45f), k, 0, 2.5f);
    y += std::max(44.0f, Wrapped(a_.body_bold, 22, x + 160, y, w - 160, White(0.82f), v) + 14);
  }
  return y;
}

void Hub::DrawBackdrop() {
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  dl_->AddRectFilled(ImVec2(0, 0), size, Rgba(8, 9, 11));
  if (a_.hero) {
    // The picture covers the screen, slowly zooming in and out (a 50 s
    // breath) and drifting sideways with the pages.
    const float breath = 0.5f - 0.5f * std::cos(time_ * 6.2832f / 50.0f);
    const float zoom = 1.04f + 0.06f * breath;
    Ease(hero_x_, (float(tab_) / float(kTabCount - 1) - 0.5f) * 0.024f, 3);
    const float sa = size.x / size.y, ia = a_.hero.width / a_.hero.height;
    float uw = 1, vh = 1;
    if (ia > sa) uw = sa / ia;
    else vh = ia / sa;
    uw /= zoom, vh /= zoom;
    const float room = (1 - uw) * 0.5f;
    const float cx = 0.5f + std::clamp(hero_x_ + 0.004f * std::sin(time_ * 0.07f), -room, room);
    const ImVec2 uv0(cx - uw * 0.5f, 0.42f - vh * 0.42f), uv1(cx + uw * 0.5f, 0.42f + vh * 0.58f);
    dl_->AddImage(a_.hero.id, ImVec2(0, 0), size, uv0, uv1);
  }
  // The pages over it: the picture steps back
  Ease(shade_, tab_ == kHome ? 0.0f : 0.84f, 7);
  if (shade_ > 0.003f) dl_->AddRectFilled(ImVec2(0, 0), size, Black(shade_));
  // (on the home page, the lower left darkens a little under the logo)
  if (shade_ < 0.6f) {
    const float k = 1 - shade_ / 0.6f;
    dl_->AddRectFilledMultiColor(ImVec2(0, P(0, 330).y), ImVec2(P(1200, 0).x, size.y), Black(0), Black(0), Black(0),
                                 Black(0.7f * k));
  }
  // the top bar and the bottom stay readable over a bright picture
  dl_->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, P(0, 230).y), Black(0.72f), Black(0.72f), Black(0), Black(0));
  const float fade_from = 430 + 330 * std::clamp(shade_ / 0.84f, 0.0f, 1.0f);
  dl_->AddRectFilledMultiColor(ImVec2(0, P(0, fade_from).y), ImVec2(size.x, size.y), Black(0), Black(0), Black(0.94f),
                               Black(0.94f));
}


void Hub::DrawTopBar() {
  // the pages, between L1 and R1
  constexpr float kSize = 26, kSpacing = 2.2f, kGap = 40;
  float x = 112;
  Glyph(kL1, x + 20, 70, White(0.55f));
  x += 70;
  float sel_x = 0, sel_w = 0;
  for (int i = 0; i < kTabCount; ++i) {
    const bool sel = i == tab_;
    const float w = TextWidth(a_.nav, kSize, kTabNames[i], kSpacing);
    Text(a_.nav, kSize, x, 52, sel ? kText : White(0.5f), kTabNames[i], 0, kSpacing);
    if (sel) sel_x = x, sel_w = w;
    x += w + kGap;
  }
  Glyph(kR1, x - kGap + 34, 70, White(0.55f));
  // the underline glides to the page, stretching on the way
  const float before = tab_x_;
  Ease(tab_x_, sel_x, 16);
  Ease(tab_w_, sel_w, 22);
  const float travel = std::min(60.0f, std::fabs(sel_x - before) * 0.25f);
  Rect(tab_x_ - travel * 0.5f, 89, tab_w_ + travel, 4, kAccent, 2);
}

void Hub::DrawHints() {
  float r = 1808;
  if (confirm_) {
    if (confirm_yes_ != "OK") r = Hint(r, kCircle, "Cancel");
    r = Hint(r, kCross, confirm_yes_.c_str());
    return;
  }
  if (tab_ != kHome) r = Hint(r, kOptions, "Start Game");
  if (tab_ != kHome) r = Hint(r, kCircle, "Home");
  switch (tab_) {
    case kHome: r = Hint(r, kCross, "Start Game"); break;
    case kSettingsTab: r = Hint(r, kLeft | kRight, "Change"); break;
    case kSavesTab:
      if (item_[tab_] <= int(backups_.size())) r = Hint(r, kCross, item_[tab_] == 0 ? "Back Up" : "Restore");
      break;
  }
  // a message (bottom left, rising in), or the version
  if (toast_time_ > 0) {
    const float a = Smooth(std::min(toast_time_ * 3, (3.0f - toast_time_) * 6));
    const float w = TextWidth(a_.body_bold, 20, toast_) + 44 + (toast_song_ ? 8 : 0);
    const float y = 1002 + (1 - a) * 14;
    Rect(112, y, w, 42, White(0.14f * a), 21);
    if (toast_song_) {  // a note
      const ImU32 c = WithAlpha(kAccent, a);
      dl_->AddCircleFilled(P(130, y + 27), 4.6f * s_, c, 16);
      Rect(133.2f, y + 11, 2.4f, 16, c, 1);
      dl_->AddTriangleFilled(P(133.2f, y + 11), P(141.5f, y + 15.5f), P(133.2f, y + 17.5f), c);
    } else {
      dl_->AddCircleFilled(P(132, y + 21), 4.5f * s_, WithAlpha(kAccent, a), 16);
    }
    Text(a_.body_bold, 20, 146 + (toast_song_ ? 8 : 0), y + 8, White(0.92f * a), toast_);
  } else {
    Text(a_.body, 18, 112, 1012, White(0.32f), "SmackDown vs. Raw 2008" + (version_.empty() ? "" : " " + version_) + "  \xC2\xB7  PlayStation 5");
  }
}

// A question over everything (restore a backup?), rising in.
void Hub::DrawConfirm() {
  Ease(confirm_in_, confirm_ ? 1.0f : 0.0f, 14);
  if (confirm_in_ < 0.01f) return;
  const float a = Smooth(confirm_in_);
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  dl_->AddRectFilled(ImVec2(0, 0), size, Black(0.6f * a));
  const float w = 760, x = (kW - w) * 0.5f;
  const int v0 = dl_->VtxBuffer.Size;
  const float y = 360 + (1 - a) * 30;
  // (its height: the text's lines, wrapped as Wrapped does)
  int lines = 0;
  {
    std::istringstream words(confirm_text_);
    std::string word, cur;
    while (words >> word) {
      const std::string next = cur.empty() ? word : cur + " " + word;
      if (!cur.empty() && TextWidth(a_.body, 22, next) > w - 96) ++lines, cur = word;
      else cur = next;
    }
    if (!cur.empty()) ++lines;
  }
  const float h = 120 + lines * 22 * 1.4f + 48;
  Rect(x, y, w, h, Rgba(24, 25, 29), 16);
  dl_->AddRectFilled(P(x, y), P(x + 6, y + h), kAccent, 16 * s_, ImDrawFlags_RoundCornersLeft);
  Text(a_.display, 40, x + 48, y + 44, kText, confirm_title_);
  Wrapped(a_.body, 22, x + 48, y + 120, w - 96, White(0.72f), confirm_text_);
  Fade(v0, a);
}

// ------------------------------------------------------------------- pages

void Hub::DrawHome() {
  // first open: the logo rises in, then the button
  const float in_logo = Smooth((time_ - 0.15f) / 0.8f), in_btn = Smooth((time_ - 0.45f) / 0.6f);
  // (the 2008 logo is wider than 2011's: the same area on the screen)
  const float lx = 104, lw = 700;
  int v0 = dl_->VtxBuffer.Size;
  if (a_.logo) {
    const float lh = lw * a_.logo.height / a_.logo.width;
    const float ly = 862 - 36 - lh + (1 - in_logo) * 40;
    for (int i = 0; i < 8; ++i) {  // a soft shadow under it
      const float a = float(i) * 0.785f, d = 10;
      const float dx = std::cos(a) * d, dy = std::sin(a) * d + 8;
      dl_->AddImage(a_.logo.id, P(lx + dx, ly + dy), P(lx + lw + dx, ly + lh + dy), ImVec2(0, 0), ImVec2(1, 1), Black(0.1f));
    }
    dl_->AddImage(a_.logo.id, P(lx, ly), P(lx + lw, ly + lh));
  } else {
    Text(a_.display, 96, lx, 640, kText, "SMACKDOWN VS RAW 2008");
  }
  Fade(v0, in_logo);

  // START GAME: breathes softly; pressed, it gives a little and flashes
  v0 = dl_->VtxBuffer.Size;
  const float push = press_ * 6;
  const float bx = 112 + push, by = 862 + push + (1 - in_btn) * 24, bw = 420 - push * 2, bh = 84 - push * 2;
  const float pulse = 0.5f + 0.5f * std::sin(time_ * 2.4f);
  const float ring = 5 + 3 * pulse;
  dl_->AddRect(P(bx - ring, by - ring), P(bx + bw + ring, by + bh + ring), White(0.14f + 0.2f * pulse), (12 + ring) * s_, 0,
               2 * s_);
  Rect(bx, by, bw, bh, Mix(kText, kAccent, press_ * 0.9f), 12);
  const ImU32 ink = Mix(kInk, kText, press_);
  const float tx = bx + 40;
  dl_->AddTriangleFilled(P(tx, by + bh * 0.5f - 15), P(tx + 26, by + bh * 0.5f), P(tx, by + bh * 0.5f + 15), ink);
  Text(a_.display, 40, tx + 48, by + bh * 0.5f - 23, ink, "START GAME", 0, 2);
  Fade(v0, in_btn);

}

int Hub::SettingIndex(const Setting& r) const {
  const std::string v = Value(r.key, r.fallback);
  for (size_t k = 0; k < r.values.size(); ++k)
    if (v == r.values[k]) return int(k);
  for (size_t k = 0; k < r.values.size(); ++k)
    if (std::string(r.fallback) == r.values[k]) return int(k);
  return 0;
}

void Hub::DrawSettings() {
  PageTitle("SETTINGS", "Saved as you change them. They take effect when the game starts.");
  const auto& set = Settings();
  const int sel = std::clamp(item_[tab_], 0, int(set.size()) - 1);
  std::vector<Row> rows;
  for (int i = 0; i < int(set.size()); ++i) {
    if (i == 0 || std::strcmp(set[size_t(i)].section, set[size_t(i) - 1].section)) rows.push_back({-1, set[size_t(i)].section});
    rows.push_back({i});
  }
  const float x = kListX, w = kListW;
  List(rows, sel, 54, [&](int i, float yy, float over) {
    const auto& r = set[size_t(i)];
    Text(a_.body_bold, 23, x + 24, yy + 11, Mix(White(0.92f), kInk, over), r.label);
    const std::string v = r.labels[size_t(SettingIndex(r))];
    if (i == sel) {
      // the value slides in from the side it was changed toward
      const float vr = x + w - 46, cy = yy + 24;
      const float slide = (1 - Smooth(value_in_)) * 18 * value_dir_;
      const int v0 = dl_->VtxBuffer.Size;
      Text(a_.body_bold, 23, vr + slide, yy + 11, kInk, v, 1);
      Fade(v0, 0.25f + 0.75f * Smooth(value_in_));
      const float vl = vr - TextWidth(a_.body_bold, 23, v) - 18;
      const float nudge_l = value_dir_ < 0 ? (1 - Smooth(value_in_)) * 6 : 0;
      const float nudge_r = value_dir_ > 0 ? (1 - Smooth(value_in_)) * 6 : 0;
      dl_->AddTriangleFilled(P(vl - nudge_l, cy - 7), P(vl - nudge_l, cy + 7), P(vl - 9 - nudge_l, cy), Black(0.75f * over));
      dl_->AddTriangleFilled(P(vr + 14 + nudge_r, cy - 7), P(vr + 14 + nudge_r, cy + 7), P(vr + 23 + nudge_r, cy),
                             Black(0.75f * over));
    } else {
      Text(a_.body, 23, x + w - 24, yy + 11, Mix(White(0.6f), kInk, over), v, 1);
    }
  });
  // the focused setting, explained
  const auto& r = set[size_t(sel)];
  Panel([&](float px, float py, float pw) {
    Text(a_.display, 44, px, py, kText, r.label);
    py += 72;
    py += Wrapped(a_.body, 23, px, py, pw, White(0.68f), r.help);
    py += 34;
    const int cur = SettingIndex(r);
    for (int k = 0; k < int(r.labels.size()); ++k) {
      const bool c = k == cur;
      const float pop = c ? 1 + 0.6f * (1 - Smooth(value_in_)) : 1;
      if (c) dl_->AddCircleFilled(P(px + 8, py + 14), 6 * pop * s_, kAccent, 20);
      else dl_->AddCircle(P(px + 8, py + 14), 5.5f * s_, White(0.3f), 20, 1.5f * s_);
      Text(c ? a_.body_bold : a_.body, 22, px + 30, py, c ? kText : White(0.45f), r.labels[size_t(k)]);
      py += 42;
    }
  });
}

void Hub::DrawSaves() {
  PageTitle("SAVES", std::to_string(save_items_.size()) + (save_items_.size() == 1 ? " save, " : " saves, ") +
                         std::to_string(backups_.size()) + (backups_.size() == 1 ? " backup." : " backups."));
  const int nb = int(backups_.size()), nf = int(save_items_.size());
  const int n = 1 + nb + nf;
  const int sel = item_[tab_] = std::clamp(item_[tab_], 0, n - 1);
  std::vector<Row> rows;
  rows.push_back({-1, "BACKUPS"});
  for (int i = 0; i <= nb; ++i) rows.push_back({i});
  if (nf) {
    rows.push_back({-1, "YOUR SAVES  " + std::to_string(nf)});
    for (int i = 0; i < nf; ++i) rows.push_back({1 + nb + i});
  }
  const float x = kListX, w = kListW;
  List(rows, sel, 76, [&](int i, float ry, float over) {
    std::string name, sub;
    if (i == 0) {
      name = "Back Up Now", sub = "Copies your saves into SaveBackups";
      // (a plus sign)
      const ImU32 c = Mix(White(0.9f), kInk, over);
      Rect(x + w - 24 - 26, ry + 34, 26, 3, c, 1.5f);
      Rect(x + w - 24 - 14.5f, ry + 22.5f, 3, 26, c, 1.5f);
    } else if (i <= nb) {
      const Backup& b = backups_[size_t(i - 1)];
      name = b.name, sub = std::to_string(b.files) + (b.files == 1 ? " file" : " files") + "  \xC2\xB7  " + Size(b.bytes);
    } else {
      const SaveItem& f = save_items_[size_t(i - 1 - nb)];
      name = f.name, sub = f.kind + "  \xC2\xB7  " + Size(f.bytes) + "  \xC2\xB7  " + Date(f.modified);
    }
    Text(a_.body_bold, 24, x + 24, ry + 9, Mix(White(0.94f), kInk, over), Fit(a_.body_bold, 24, name, w - 120));
    Text(a_.body, 18, x + 24, ry + 41, Mix(White(0.42f), Black(0.55f), over), Fit(a_.body, 18, sub, w - 120));
  });
  Panel([&](float px, float py, float pw) {
    if (sel == 0) {
      Text(a_.display, 44, px, py, kText, "Back Up Now");
      Wrapped(a_.body, 22, px, py + 72, pw, White(0.65f),
              "Copies all of your saves - Season, Created Superstars, settings - into SaveBackups, named by the date "
              "and time.");
    } else if (sel <= nb) {
      const Backup& b = backups_[size_t(sel - 1)];
      py += Wrapped(a_.display, 44, px, py, pw, kText, b.name, 1.15f) + 22;
      py = Facts(px, py, pw, {{"FILES", std::to_string(b.files)}, {"SIZE", Size(b.bytes)}});
      Wrapped(a_.body, 21, px, py + 20, pw, White(0.6f),
              "Restoring puts these saves back. The saves you have now are backed up first.");
    } else {
      const SaveItem& f = save_items_[size_t(sel - 1 - nb)];
      py += Wrapped(a_.display, 44, px, py, pw, kText, f.name, 1.15f) + 22;
      Facts(px, py, pw,
            {{"TYPE", f.kind}, {"FILES", std::to_string(f.files)}, {"SIZE", Size(f.bytes)}, {"CHANGED", Date(f.modified)}});
    }
  });
}

void Hub::DrawAbout() {
  PageTitle("ABOUT", "SmackDown vs. Raw 2008 for PlayStation 5" + (version_.empty() ? "" : "  \xC2\xB7  " + version_));
  struct Credit {
    const char* who;
    const char* what;
  };
  const Credit people[] = {
      {"nefariousjosiah", "Created WWE-SVR2008-RECOMP, the PC port this is built on, and its native renderer."},
      {"@elripalda", "Brought it to PlayStation 5: the PS5 port and this hub."},
      {"ReXGlue", "The static recompiler and runtime that turn the Xbox 360 game into a native program."},
      {"re:Blue", "The renderer the PC port's was adapted from."},
      {"Xenia", "The Xbox 360 emulator the runtime's code comes from."},
  };
  float y = 276;
  for (const Credit& c : people) {
    Text(a_.display, 32, 112, y, kText, c.who);
    Wrapped(a_.body, 21, 112, y + 46, 860, White(0.62f), c.what);
    y += 112;
  }
  const float px = kPanelX;
  float py = 286;
  Text(a_.body_bold, 15, px, py, White(0.45f), "BUILT WITH", 0, 3);
  py += 40;
  const char* parts[] = {"PS5 Payload SDK", "Mesa RADV (Vulkan)", "plume and XenosRecomp", "Dear ImGui",
                         "stb_image, stb_vorbis, dr_mp3 and dr_flac", "Oswald and Inter fonts (SIL Open Font License)"};
  for (const char* part : parts) {
    Text(a_.body, 21, px, py, White(0.78f), part);
    py += 36;
  }
  py += 30;
  Text(a_.body_bold, 15, px, py, White(0.45f), "PLEASE NOTE", 0, 3);
  py += 40;
  Wrapped(a_.body, 19, px, py, 680, White(0.55f),
          "An unofficial fan project, not affiliated with or endorsed by WWE, THQ, Yuke's, Microsoft or Sony. WWE "
          "SmackDown vs. Raw 2008 and all related names belong to their owners. No game files are included: you need "
          "your own copy of the game.");
}

// --------------------------------------------------------------- behaviour

int Hub::Items() const {
  switch (tab_) {
    case kSettingsTab: return int(Settings().size());
    case kSavesTab: return 1 + int(backups_.size()) + int(save_items_.size());
    default: return 0;
  }
}

void Hub::Activate(int dir) {
  const int item = item_[tab_];
  switch (tab_) {
    case kSettingsTab: {
      const auto& r = Settings()[size_t(item)];
      const int n = int(r.values.size());
      const int idx = (SettingIndex(r) + dir + n) % n;
      Save(r.key, r.quoted ? std::string("\"") + r.values[size_t(idx)] + "\"" : std::string(r.values[size_t(idx)]));
      value_in_ = 0;
      value_dir_ = float(dir);
      Play(Sound::kSelect);
      break;
    }
    case kSavesTab: {
      if (item == 0) {
        BackUpSaves("");
        Play(Sound::kSelect);
      } else if (item - 1 < int(backups_.size())) {
        const Backup b = backups_[size_t(item - 1)];
        Play(Sound::kSelect);
        Confirm("Restore this backup?", "Your saves are replaced by the ones from " + b.name +
                                            ". The saves you have now are backed up first, so nothing is lost.",
                "Restore", [this, b] { RestoreSaves(b); });
      }
      break;
    }
  }
}

void Hub::Navigate(const Pad& pad) {
  const uint32_t p = pad.pressed;
  if (leaving_) return;
  // R3: the next song, anywhere
  if ((p & kR3) && audio_.next_song) {
    audio_.next_song();  // (quietly: the music just changes)
  }
  if (confirm_) {  // a question: Cross does it, Circle doesn't
    if ((p & kCross) && confirm_in_ > 0.6f) {  // (not before it has been seen)
      auto action = std::move(confirm_);
      confirm_ = nullptr;
      action();
    } else if (p & kCircle) {
      confirm_ = nullptr;
      Play(Sound::kBack);
    }
    return;
  }
  // START GAME: once the game is installed (and not while it installs)
  auto start = [&] {
    if (!state_.Installed()) {
      Play(Sound::kError);
      Confirm("The game isn't installed",
              "Install SmackDown vs. Raw 2008 with the app store, or copy the files of your disc image into "
              "/data/homebrew/PPSA92008/Assets/ (default.xex, pac, sound... in Assets).",
              "OK", [] {});
    } else {
      press_ = 1, launch_ = true;
    }
  };
  if (p & kOptions) {
    start();
    return;
  }
  if (p & (kL1 | kR1)) {
    GoTo((tab_ + ((p & kR1) ? 1 : kTabCount - 1)) % kTabCount, (p & kR1) ? 1.0f : -1.0f);
    Play(Sound::kPage);
    return;
  }
  if ((p & kCircle) && tab_ != kHome) {
    GoTo(kHome, -1);
    Play(Sound::kBack);
    return;
  }
  if (tab_ == kHome) {
    if (p & kCross) start();
    return;
  }
  const int n = Items();
  if (n == 0) return;
  int& sel = item_[tab_];
  const int before = sel;
  if (p & kUp) sel = (sel + n - 1) % n;
  if (p & kDown) sel = (sel + 1) % n;
  if (sel != before) Play(Sound::kMove);
  switch (tab_) {
    case kSettingsTab:
      if (p & kLeft) Activate(-1);
      if (p & (kRight | kCross)) Activate(1);
      if (p & (kLeft | kRight | kCross)) press_ = 0.6f;
      break;
    case kSavesTab:
      if (p & kCross) press_ = 1, Activate(0);
      break;
  }
}

void Hub::GoTo(int tab, float dir) {
  if (tab == tab_) return;
  tab_ = tab;
  scroll_f_ = -1;
  scroll_px_ = 0;
  focus_y_ = -1;  // (the highlight starts where the page's row is)
  page_in_ = 0;
  page_dir_ = dir;
  panel_in_ = 1;
  panel_item_ = item_[tab_];
  // (what these pages show can change while the hub is open: looked at again)
  if (tab == kSavesTab) LoadSaves();
}

bool Hub::Draw(const Pad& pad, float dt) {
  dt_ = std::clamp(dt, 0.0f, 0.1f);
  time_ += dt_;
  toast_time_ = std::max(0.0f, toast_time_ - dt_);
  PollTask();
  Navigate(pad);
  if (launch_ && !leaving_) {
    leaving_ = true, press_ = 1;
    Play(Sound::kStart);
  }
  if (item_[tab_] != panel_item_) panel_item_ = item_[tab_], panel_in_ = 0;
  Ease(panel_in_, 1, 10);
  Ease(value_in_, 1, 12);
  Ease(press_, 0, 9);
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  s_ = std::min(size.x / kW, size.y / kH);
  ox_ = (size.x - kW * s_) * 0.5f;
  oy_ = (size.y - kH * s_) * 0.5f;
  dl_ = ImGui::GetForegroundDrawList();
  DrawBackdrop();
  DrawTopBar();
  // the page slides in from the side it came from, fading up
  Ease(page_in_, 1, 11);
  const int first = dl_->VtxBuffer.Size;
  const float shift = (1 - page_in_) * 48 * page_dir_;
  ox_ += shift * s_;
  switch (tab_) {
    case kHome: DrawHome(); break;
    case kSettingsTab: DrawSettings(); break;
    case kSavesTab: DrawSaves(); break;
    case kAboutTab: DrawAbout(); break;
  }
  ox_ -= shift * s_;
  Fade(first, Smooth(page_in_));
  DrawHints();
  DrawConfirm();
  // opening: up from black; START GAME: down to black
  const float open = Smooth(time_ / 0.7f);
  if (leaving_) leave_ = std::min(1.0f, leave_ + dt_ / 0.6f);
  const float black = std::max(1 - open, Smooth(leave_));
  if (black > 0.002f) dl_->AddRectFilled(ImVec2(0, 0), size, Black(black));
  return leaving_ && leave_ >= 1;
}

}  // namespace svr2008::hub
