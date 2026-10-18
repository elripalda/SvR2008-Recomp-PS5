// WWE SmackDown vs. Raw 2008 - the PS5 hub on the console (see hub_ui.h):
// shown before the game is set up (ReXApp::OnFinalizePaths, asynchronous),
// driven by the DualSense through scePad, with the menu music (hub/music,
// a random song first, R3 the next one), and START GAME resumes the start.
#if defined(__PROSPERO__)

#include "ps5_hub.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/pad_dialog_style.h>

#include "hub_sfx.h"
#include "hub_ui.h"
#include "../ps5/sound.h"
#include "imgui.h"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

extern "C" {
int sceUserServiceInitialize(void* params);
int sceUserServiceGetInitialUser(int32_t* user_id);
int scePadInit(void);
int scePadOpen(int32_t user_id, int32_t type, int32_t index, const void* param);
int scePadGetHandle(int32_t user_id, int32_t type, int32_t index);
int scePadReadState(int32_t handle, void* data);
}

// The fonts, built into the app (fonts/, SIL Open Font License).
#define SVR_INCBIN(name, file)                                                        \
  __asm__(".section .rodata\n.global " #name "\n.balign 16\n" #name ":\n.incbin \"" \
          file "\"\n.global " #name "_end\n" #name "_end:\n.byte 0\n.previous\n");    \
  extern "C" const unsigned char name[];                                              \
  extern "C" const unsigned char name##_end[];
SVR_INCBIN(svr2008_font_oswald_bold, SVR2008_FONTS_DIR "/oswald-latin-700-normal.ttf")
SVR_INCBIN(svr2008_font_oswald, SVR2008_FONTS_DIR "/oswald-latin-600-normal.ttf")
SVR_INCBIN(svr2008_font_inter, SVR2008_FONTS_DIR "/inter-latin-400-normal.ttf")
SVR_INCBIN(svr2008_font_inter_bold, SVR2008_FONTS_DIR "/inter-latin-600-normal.ttf")

namespace svr2008::hub {
namespace {

namespace fs = std::filesystem;

constexpr uint32_t kPadR3 = 0x00000004u, kPadOptions = 0x00000008u, kPadUp = 0x00000010u, kPadRight = 0x00000020u,
                   kPadDown = 0x00000040u, kPadLeft = 0x00000080u, kPadL1 = 0x00000400u, kPadR1 = 0x00000800u,
                   kPadTriangle = 0x00001000u, kPadCircle = 0x00002000u, kPadCross = 0x00004000u,
                   kPadSquare = 0x00008000u;
struct PadData {
  uint32_t buttons;
  uint8_t lx, ly, rx, ry, l2, r2;
  uint16_t padding;
  uint8_t rest[240];
};

Assets g_assets;
// The hub's pictures: kept for the app's life. (The dialog is deleted right
// after the frame START GAME was pressed on, and that frame's draw list,
// rendered afterwards, still uses them.)
std::vector<std::unique_ptr<rex::ui::ImmediateTexture>>* g_textures =
    new std::vector<std::unique_ptr<rex::ui::ImmediateTexture>>();

// The pad: a handle of our own (the input driver later gets the same one).
class Pad5 {
 public:
  Pad read() {
    if (handle_ < 0) {
      sceUserServiceInitialize(nullptr);
      scePadInit();
      int32_t user = -1;
      if (sceUserServiceGetInitialUser(&user) == 0 && user >= 0) {
        handle_ = scePadOpen(user, 0, 0, nullptr);
        if (handle_ < 0) handle_ = scePadGetHandle(user, 0, 0);
      }
      if (handle_ < 0) return {};
    }
    PadData d{};
    if (scePadReadState(handle_, &d) != 0) return {};
    uint32_t b = 0;
    if ((d.buttons & kPadUp) || d.ly < 64) b |= kUp;
    if ((d.buttons & kPadDown) || d.ly > 192) b |= kDown;
    if ((d.buttons & kPadLeft) || d.lx < 64) b |= kLeft;
    if ((d.buttons & kPadRight) || d.lx > 192) b |= kRight;
    if (d.buttons & kPadCross) b |= kCross;
    if (d.buttons & kPadCircle) b |= kCircle;
    if (d.buttons & kPadSquare) b |= kSquare;
    if (d.buttons & kPadTriangle) b |= kTriangle;
    if (d.buttons & kPadL1) b |= kL1;
    if (d.buttons & kPadR1) b |= kR1;
    if (d.buttons & kPadOptions) b |= kOptions;
    if (d.buttons & kPadR3) b |= kR3;
    Pad p;
    p.held = b;
    p.pressed = b & ~last_;
    // (held directions repeat: after 0.4 s, every 0.09 s)
    const auto now = std::chrono::steady_clock::now();
    constexpr uint32_t kDirs = kUp | kDown | kLeft | kRight;
    if (b & kDirs) {
      if ((b & kDirs) != (last_ & kDirs)) {
        repeat_at_ = now + std::chrono::milliseconds(400);
      } else if (now >= repeat_at_) {
        p.pressed |= b & kDirs;
        repeat_at_ = now + std::chrono::milliseconds(90);
      }
    }
    last_ = b;
    // The first read: ignore what was already held (the button that started the app).
    if (first_) {
      first_ = false;
      p.pressed = 0;
    }
    return p;
  }

 private:
  int32_t handle_ = -1;
  uint32_t last_ = 0;
  bool first_ = true;
  std::chrono::steady_clock::time_point repeat_at_{};
};

// The hub's sound (ps5/sound.h): the menu music, one song after another
// from hub/music (a random one first), and the menu sounds (hub_sfx.h, or
// hub/sfx/<name>.*).
class HubAudio {
 public:
  explicit HubAudio(const fs::path& hub) {
    if (!svr2008::sound::StartOutput()) REXLOG_WARN("PS5 hub: no audio port for the hub's sounds");
    music_ = svr2008::sound::NewVoice();
    for (auto& v : sfx_) v = svr2008::sound::NewVoice();
    for (int k = 0; k <= int(Sound::kError); ++k) {
      const Sound s = Sound(k);
      for (const char* ext : {".wav", ".mp3", ".flac", ".ogg"}) {
        const auto file = hub / "sfx" / (std::string(SoundName(s)) + ext);
        std::error_code ec;
        if (fs::is_regular_file(file, ec)) {
          files_[k] = file;
          break;
        }
      }
      if (files_[k].empty()) synth_[k] = std::make_shared<const std::vector<float>>(SynthSound(s));
    }
    // the songs (by name), and a random one to start with
    std::error_code ec;
    for (fs::directory_iterator it(hub / "music", ec), end; !ec && it != end; it.increment(ec))
      if (it->is_regular_file(ec) && svr2008::sound::CanOpen(it->path())) songs_.push_back(it->path());
    std::sort(songs_.begin(), songs_.end());
    if (!songs_.empty()) {
      const uint64_t t = uint64_t(std::chrono::system_clock::now().time_since_epoch().count()) ^
                         uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()) * 0x9E3779B97F4A7C15ull;
      song_ = size_t((t >> 17) % songs_.size());
      StartSong();
    }
    REXLOG_INFO("PS5 hub: {} songs in {}", songs_.size(), (hub / "music").string());
  }
  bool HasMusic() const { return !songs_.empty(); }
  std::string Current() const { return songs_.empty() ? std::string() : songs_[song_].stem().string(); }

  void Sfx(Sound s) {
    const int k = int(s);
    std::unique_ptr<svr2008::sound::Source> src =
        files_[k].empty() ? svr2008::sound::Memory(synth_[k]) : svr2008::sound::Open(files_[k]);
    if (!src) return;
    auto* v = sfx_[next_++ % sfx_.size()];  // (a few at once: quick presses overlap)
    v->SetVolume(1);
    v->Play(std::move(src), false);
  }

  // R3: the song fades down quickly, then the next one plays. Returns its name.
  std::string Next() {
    if (songs_.empty() || leaving_) return {};
    song_ = (song_ + 1) % songs_.size();
    if (switch_in_ <= 0) music_->FadeOut(kSwitchFade);
    switch_in_ = kSwitchFade + 0.05f;
    return Current();
  }

  // Every frame: a switch finishing, or a song that ended (the next one
  // follows). Returns the name of a song that started by itself (else "").
  std::string Tick(float dt) {
    if (songs_.empty() || leaving_) return {};
    if (switch_in_ > 0) {
      switch_in_ -= dt;
      if (switch_in_ <= 0) StartSong();
      return {};
    }
    if (!music_->Active()) {  // (it ended, or couldn't be played)
      if (++failed_ > int(songs_.size()) * 2) return {};  // (none of them plays: stop trying)
      song_ = (song_ + 1) % songs_.size();
      StartSong();
      return Current();
    }
    return {};
  }

  void FadeOut() {
    leaving_ = true;
    music_->FadeOut(0.6f);
  }

 private:
  static constexpr float kMusicVolume = 0.775f;  // (the 2011 hub's level)
  static constexpr float kSwitchFade = 0.35f;

  void StartSong() {
    auto src = svr2008::sound::Open(songs_[song_]);
    if (!src) {
      REXLOG_WARN("PS5 hub: can't play {}", songs_[song_].string());
      return;  // (Tick moves on)
    }
    failed_ = 0;
    music_->SetVolume(kMusicVolume);
    music_->Play(std::move(src), songs_.size() == 1);  // (one song: it loops)
    REXLOG_INFO("PS5 hub: playing {}", songs_[song_].filename().string());
  }

  svr2008::sound::Voice* music_ = nullptr;
  std::array<svr2008::sound::Voice*, 6> sfx_{};
  size_t next_ = 0;
  std::array<fs::path, 8> files_;
  std::array<std::shared_ptr<const std::vector<float>>, 8> synth_;
  std::vector<fs::path> songs_;
  size_t song_ = 0;
  float switch_in_ = 0;
  int failed_ = 0;
  bool leaving_ = false;
};

// The menu sounds for the game's own dialogs (the on-screen keyboard), after
// the hub has gone: made in code, a few voices of their own.
class MenuSfx {
 public:
  MenuSfx() {
    for (auto& v : voices_) v = svr2008::sound::NewVoice();
    for (int k = 0; k <= int(Sound::kError); ++k)
      synth_[k] = std::make_shared<const std::vector<float>>(SynthSound(Sound(k)));
  }
  void Play(int k) {
    if (k < 0 || k > int(Sound::kError)) return;
    auto* v = voices_[next_++ % voices_.size()];
    v->SetVolume(0.85f);
    v->Play(svr2008::sound::Memory(synth_[k]), false);
  }

 private:
  std::array<svr2008::sound::Voice*, 4> voices_{};
  std::array<std::shared_ptr<const std::vector<float>>, 8> synth_;
  size_t next_ = 0;
};

// The game's on-screen keyboard in the hub's style (rex/ui/pad_dialog_style.h).
void StyleGameDialogs(const Assets& a) {
  static MenuSfx* sfx = new MenuSfx();  // (for the app's life)
  rex::ui::PadDialogStyle st;
  st.title = a.display;
  st.body = a.body;
  st.bold = a.body_bold;
  const std::pair<uint32_t, uint32_t> map[] = {{kCross, 0x1000}, {kCircle, 0x2000}, {kSquare, 0x4000},
                                               {kTriangle, 0x8000}, {kL1, 0x0100}, {kR1, 0x0200},
                                               {kOptions, 0x0010}, {kLeft | kRight, 0x000C}};
  for (const auto& [hub, xbox] : map)
    if (auto it = a.buttons.find(hub); it != a.buttons.end() && it->second) st.buttons[xbox] = it->second.id;
  st.sound = [](int k) { sfx->Play(k); };
  rex::ui::SetPadDialogStyle(std::move(st));
}

class HubDialog final : public rex::ui::ImGuiDialog {
 public:
  HubDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, const Paths& paths,
            std::function<void()> launch)
      : ImGuiDialog(drawer), launch_(std::move(launch)) {
    const fs::path hub = paths.app / "hub";
    Assets a = g_assets;
    a.logo = Load(immediate, hub / "logo.png");
    a.hero = Load(immediate, hub / "hero.jpg");
    if (!a.hero) a.hero = Load(immediate, hub / "hero.png");
    // the button pictures (hub/buttons/<name>.png; drawn ones where missing)
    const std::pair<uint32_t, const char*> names[] = {
        {kCross, "Cross"}, {kCircle, "Circle"},   {kSquare, "Square"},         {kTriangle, "Triangle"}, {kL1, "L1"},
        {kR1, "R1"},       {kOptions, "Options"}, {kLeft | kRight, "D-Pad"}, {kR3, "R3"}};
    for (const auto& [b, n] : names)
      if (Image img = Load(immediate, hub / "buttons" / (std::string(n) + ".png"))) a.buttons[b] = img;
    hub_.Init(paths, a, "");
    StyleGameDialogs(a);
    audio_ = std::make_unique<HubAudio>(hub);
    Audio hooks;
    hooks.sfx = [this](Sound s) { audio_->Sfx(s); };
    hooks.next_song = [this] { return audio_->Next(); };
    hub_.SetAudio(std::move(hooks));
    hub_.SetHasMusic(audio_->HasMusic());
    hub_.NowPlaying(audio_->Current());
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) return;
    const Pad pad = pad_.read();
    hub_.NowPlaying(audio_->Tick(io.DeltaTime));
    const bool go = hub_.Draw(pad, io.DeltaTime);
    if (hub_.Leaving() && !faded_) audio_->FadeOut(), faded_ = true;
    if (go) {
      done_ = true;
      Close();
      launch_();
    }
  }

 private:
  Image Load(rex::ui::ImmediateDrawer* immediate, const fs::path& file) {
    if (!immediate) return {};
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(file.string().c_str(), &w, &h, &n, 4);
    if (!px) return {};
    auto tex = immediate->CreateTexture(uint32_t(w), uint32_t(h), rex::ui::ImmediateTextureFilter::kLinear, false, px);
    stbi_image_free(px);
    if (!tex) return {};
    Image img{reinterpret_cast<ImTextureID>(tex.get()), float(w), float(h)};
    g_textures->push_back(std::move(tex));
    return img;
  }

  Hub hub_;
  Pad5 pad_;
  std::unique_ptr<HubAudio> audio_;  // (its voices outlive it: the last sounds finish in the game)
  bool faded_ = false;
  bool done_ = false;
  std::function<void()> launch_;
};

ImFont* AddFont(ImFontAtlas* atlas, const unsigned char* begin, const unsigned char* end, float size) {
  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = false;  // (built into the app)
  cfg.OversampleH = 2;
  return atlas->AddFontFromMemoryTTF(const_cast<unsigned char*>(begin), int(end - begin), size, &cfg);
}

}  // namespace

void ConfigureFonts(ImFontAtlas* atlas) {
  // Sized for the 4K screen (the hub scales them to the display).
  g_assets.display = AddFont(atlas, svr2008_font_oswald_bold, svr2008_font_oswald_bold_end, 72.0f * 2);
  g_assets.nav = AddFont(atlas, svr2008_font_oswald, svr2008_font_oswald_end, 34.0f * 2);
  g_assets.body = AddFont(atlas, svr2008_font_inter, svr2008_font_inter_end, 26.0f * 2);
  g_assets.body_bold = AddFont(atlas, svr2008_font_inter_bold, svr2008_font_inter_bold_end, 26.0f * 2);
}

void Show(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, const Paths& paths,
          std::function<void()> launch) {
  REXLOG_INFO("PS5 hub: shown (app folder {})", paths.app.string());
  new HubDialog(drawer, immediate, paths, std::move(launch));  // (the dialog owns itself)
}

}  // namespace svr2008::hub

#endif  // __PROSPERO__
