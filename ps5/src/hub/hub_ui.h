// WWE SmackDown vs. Raw 2008 PS5 - the hub: the screen the app opens on, before the game
// (the PC launcher's settings, driven by the controller). The SmackDown vs. Raw
// and WWE 2K14 PS5 ports' hub, with its Home, Settings, Saves and About pages,
// and a menu music rotation (R3: the next song).
//
// This part is plain ImGui + the standard library, so it also builds on a
// PC for previews (tools/hub_preview). ps5_hub.cpp puts it on the PS5's
// screen, reads the DualSense, plays the music and starts the game.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "hub_install.h"
#include "imgui.h"

namespace svr2008::hub {

// The controller, as the hub sees it.
enum Button : uint32_t {
  kUp = 1u << 0,
  kDown = 1u << 1,
  kLeft = 1u << 2,
  kRight = 1u << 3,
  kCross = 1u << 4,
  kCircle = 1u << 5,
  kSquare = 1u << 6,
  kTriangle = 1u << 7,
  kL1 = 1u << 8,
  kR1 = 1u << 9,
  kOptions = 1u << 10,
  kR3 = 1u << 11,
};

struct Pad {
  uint32_t held = 0;     // down now
  uint32_t pressed = 0;  // down now, not before (with key repeat for the d-pad)
};

struct Image {
  ImTextureID id = 0;
  float width = 0, height = 0;
  // the drawing's share of the picture, across and down (a button picture's
  // drawing is centred in its square: ps5/tools/hub_buttons.py)
  float fill_w = 1, fill_h = 1;
  explicit operator bool() const { return id != 0; }
};

// The part of a picture that isn't clear (alpha above 8), as fill_w / fill_h.
inline void MeasureFill(Image& img, const unsigned char* rgba, int w, int h) {
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if (rgba[(size_t(y) * w + x) * 4 + 3] > 8) {
        x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1;
        y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
      }
  if (x1 >= 0) img.fill_w = float(x1 - x0 + 1) / float(w), img.fill_h = float(y1 - y0 + 1) / float(h);
}

struct Assets {
  ImFont* display = nullptr;    // Oswald Bold: titles, the big button, numbers
  ImFont* nav = nullptr;        // Oswald SemiBold: the tabs, section names
  ImFont* body = nullptr;       // Inter Regular: text
  ImFont* body_bold = nullptr;  // Inter SemiBold: labels, values
  Image logo;                   // the game's logo
  Image hero;                   // the picture behind it all
  std::map<uint32_t, Image> buttons;  // the PS5's button pictures, by Button (kLeft | kRight: the d-pad)
};

struct Paths {
  std::filesystem::path app;        // the app folder (/data/homebrew/PPSA92008)
  std::filesystem::path config;     // svr2008.toml (the hub's settings)
  std::filesystem::path user_data;  // UserData: the saves are the game's content folders in it
};

// The sounds the hub asks for (ps5_hub.cpp plays them).
enum class Sound { kMove, kSelect, kBack, kPage, kOn, kOff, kStart, kError };

// What the hub plays through (none: silent, as in tools/hub_preview).
struct Audio {
  std::function<void(Sound)> sfx;
  // The menu music: the next song (returns its name; empty: no songs).
  std::function<std::string()> next_song;
};

// The pages, in the order of the top bar.
enum Tab { kHome, kSettingsTab, kSavesTab, kAboutTab, kTabCount };

class Hub {
 public:
  void Init(const Paths& paths, const Assets& assets, std::string version);
  void SetAudio(Audio audio) { audio_ = std::move(audio); }
  // Draws the hub over the whole display; true when the game should start.
  bool Draw(const Pad& pad, float dt);
  // From START GAME until Draw returns true: the hub fades out (and its music).
  bool Leaving() const { return leaving_; }
  // A song started (shown at the bottom for a few seconds).
  void NowPlaying(const std::string& name);
  bool HasMusic() const { return music_; }
  // The game's files are in Assets (START GAME can go).
  bool Installed() const { return state_.Installed(); }
  void SetHasMusic(bool on) { music_ = on; }
  // Settings' Menu Music (hub_music_volume, 50% when not set), 0..1.
  float MusicVolume() const;

  struct Setting;

 private:
  struct SaveItem {  // a content folder of the game's (a save)
    std::string name, kind;
    std::filesystem::path folder;
    uint32_t files = 0;
    uint64_t bytes = 0;
    int64_t modified = 0;  // unix time
  };
  struct Backup {
    std::string name;
    uint32_t files = 0;
    uint64_t bytes = 0;
  };
  // A row of a list: an item, or a heading over the next ones.
  struct Row {
    int item = -1;  // -1: a heading
    std::string heading;
    float y = 0;
  };

  void Load();
  void LoadSaves();
  std::string Value(const std::string& key, const std::string& fallback) const;
  void Save(const std::string& key, const std::string& value);
  int SettingIndex(const Setting& r) const;

  // pages
  void DrawBackdrop();
  void DrawTopBar();
  void DrawHints();
  void DrawConfirm();
  void DrawHome();
  void DrawSettings();
  void DrawSaves();
  void DrawAbout();
  void GoTo(int tab, float dir);
  void PageTitle(const char* title, const std::string& subtitle);
  // A scrolled list with headings at the page's left: rows' items are
  // 0..n-1 in order; `draw` draws item i's row at y (`over`: how much the
  // focus highlight covers it, 0..1).
  void List(std::vector<Row>& rows, int sel, float rh, const std::function<void(int, float, float)>& draw);
  // The details panel at the right; `draw` from its top, faded in.
  void Panel(const std::function<void(float x, float y, float w)>& draw);
  // Label: value lines of the panel; returns the y after them.
  float Facts(float x, float y, float w, const std::vector<std::pair<std::string, std::string>>& facts);

  void Navigate(const Pad& pad);
  int Items() const;
  void Activate(int dir);
  void Play(Sound s) {
    if (audio_.sfx) audio_.sfx(s);
  }
  void Toast(std::string text) {
    toast_ = std::move(text);
    toast_time_ = 3;
    toast_song_ = false;
  }
  void Confirm(std::string title, std::string text, std::string yes, std::function<void()> action) {
    confirm_title_ = std::move(title), confirm_text_ = std::move(text), confirm_yes_ = std::move(yes);
    confirm_ = std::move(action);
    confirm_no_ = nullptr;
    confirm_no_label_.clear();
    confirm_in_ = 0;
  }
  // ...with Circle doing something too (`no_label` in the hints instead of Cancel).
  void Confirm(std::string title, std::string text, std::string yes, std::function<void()> action, std::string no_label,
               std::function<void()> no_action) {
    Confirm(std::move(title), std::move(text), std::move(yes), std::move(action));
    confirm_no_label_ = std::move(no_label);
    confirm_no_ = std::move(no_action);
  }
  // A message (Cross closes it).
  void Notice(std::string title, std::string text) { Confirm(std::move(title), std::move(text), "OK", [] {}); }
  void BackUpSaves(const std::string& why);
  void RestoreSaves(const Backup& b);

  Paths paths_;
  Assets a_;
  Audio audio_;
  std::string version_;
  // layout: virtual 1920 x 1080, scaled to the display
  float s_ = 1, ox_ = 0, oy_ = 0;
  ImDrawList* dl_ = nullptr;
  float time_ = 0, dt_ = 0;

  int tab_ = 0;
  int item_[8] = {};  // the focused row on each page
  bool launch_ = false;
  std::string toast_;
  float toast_time_ = 0;
  bool toast_song_ = false;  // (the toast is a song's name)
  std::string pending_song_;  // (shown once the hub has faded in)
  bool music_ = false;
  // a question (restore a backup?): Cross does it, Circle doesn't (or does confirm_no_)
  std::function<void()> confirm_, confirm_no_;
  std::string confirm_title_, confirm_text_, confirm_yes_, confirm_no_label_;
  float confirm_in_ = 0;
  // animation (eased toward their targets)
  float tab_x_ = -1, tab_w_ = 0, focus_y_ = -1, shade_ = 0.3f, page_in_ = 1;
  float page_dir_ = 1;          // the page slides in from this side (+1: the right)
  float panel_in_ = 1;          // the details panel, after the focus moves
  int panel_item_ = -1;
  float value_in_ = 1, value_dir_ = 0;  // a value, after it changes
  float press_ = 0;             // a flash on the focused thing when it's pressed
  float hero_x_ = -1;           // the picture drifts sideways with the pages
  bool leaving_ = false;        // START GAME: fading out
  float leave_ = 0;
  float scroll_f_ = 0, scroll_px_ = 0;  // the page's list: its scroll, shown and wanted

  std::vector<std::pair<std::string, std::string>> config_;  // top-level key = value
  std::vector<SaveItem> save_items_;
  // the game's files
  install::State state_;
  std::vector<Backup> backups_;
  // helpers (virtual coordinates)
  ImVec2 P(float x, float y) const { return ImVec2(ox_ + x * s_, oy_ + y * s_); }
  float Ease(float& v, float target, float speed = 16);
  void Text(ImFont* f, float size, float x, float y, ImU32 col, const std::string& t, float align = 0,
            float spacing = 0);
  float TextWidth(ImFont* f, float size, const std::string& t, float spacing = 0) const;
  // `t` cut to fit `width` (with "...").
  std::string Fit(ImFont* f, float size, const std::string& t, float width) const;
  std::vector<std::string> WrapLines(ImFont* f, float size, float width, const std::string& t);
  float Wrapped(ImFont* f, float size, float x, float y, float width, ImU32 col, const std::string& t,
                float line = 1.4f);
  void Rect(float x, float y, float w, float h, ImU32 col, float r = 0);
  // Text with its capitals centred on `cy` (Text's y is the top of its line).
  void TextMid(ImFont* f, float size, float x, float cy, ImU32 col, const std::string& t, float align = 0,
               float spacing = 0);
  float CapMid(ImFont* f, float size) const;    // the capitals' middle, below the line's top
  float Baseline(ImFont* f, float size) const;  // the baseline, below the line's top
  // A PlayStation button centred on (cx, cy), its picture's square `size`
  // across; returns the width of the button drawn in it.
  float Glyph(uint32_t button, float cx, float cy, ImU32 col, float size);
  float GlyphWidth(uint32_t button, float size) const;
  float Hint(float right, uint32_t button, const char* label);  // returns its left edge
  void Fade(int first_vertex, float alpha);                    // fades what was drawn since
};

}  // namespace svr2008::hub
