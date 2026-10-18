// WWE SmackDown vs. Raw 2008 - PS5 port: where things are, the settings the game needs, the
// native renderer's device and overlay, and the hub.
#include "generated/default/svr2008_init.h"
#include "svr2008_app.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/ui/imgui_drawer.h>

#include <rex/input/input_system.h>
#include <rex/ui/imgui_dialog.h>

#include "crash_report.h"
#include "fps_counter.h"
#include "gpu/device.h"
#include "gpu/imgui_overlay_drawer.h"
#include "hub/ps5_hub.h"

namespace fs = std::filesystem;

REXCVAR_DEFINE_BOOL(svr_fps_counter, true, "Game", "Show the frame rate in the top-left corner while playing");

namespace bd::gpu {
void SvrDrawLogNextFrame();  // src/native/svr_draw_log.h
}

namespace {

// Test builds: L3 + R3 together writes every draw of the next frame to the log ([drawlog]
// lines), to see what a scene is made of (the PC port's F9).
class PadWatchDialog final : public rex::ui::ImGuiDialog {
 public:
  PadWatchDialog(rex::ui::ImGuiDrawer* drawer, rex::Runtime* runtime) : ImGuiDialog(drawer), runtime_(runtime) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    auto* input = runtime_ ? static_cast<rex::input::InputSystem*>(runtime_->input_system()) : nullptr;
    uint16_t buttons = 0;
    if (input) {
      rex::input::X_INPUT_STATE st{};
      if (input->GetState(0, &st) == 0) buttons = uint16_t(st.gamepad.buttons);
    }
    constexpr uint16_t kL3R3 = 0x0040 | 0x0080;  // XINPUT_GAMEPAD_LEFT_THUMB | RIGHT_THUMB
    const bool both = (buttons & kL3R3) == kL3R3;
    if (both && !held_) {
      bd::gpu::SvrDrawLogNextFrame();
      shown_ = 2.0f;
      REXLOG_INFO("[drawlog] L3 + R3: logging the next frame");
    }
    held_ = both;
    if (shown_ > 0) {
      shown_ -= io.DeltaTime;
      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 40), ImGuiCond_Always,
                              ImVec2(0.5f, 1.0f));
      constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
      if (ImGui::Begin("##svr_drawlog", nullptr, kFlags)) ImGui::TextUnformatted("Frame captured to the log");
      ImGui::End();
    }
  }

 private:
  rex::Runtime* runtime_;
  bool held_ = false;
  float shown_ = 0;
};

fs::path g_config_path;

// The settings a first start writes (svr2008.toml beside the app; edit it over FTP).
constexpr char kConfigVersion[] = "(settings v2)";
constexpr char kDefaultConfig[] =
    "# WWE SmackDown vs. Raw 2008 - settings (settings v2)\n"
    "# the game's own 60 fps mode\n"
    "svr_60fps = true\n"
    "# 0 = auto, 1 = the Xbox 360's 720p, 2 = 1440p, 3 = 4K\n"
    "svr_render_scale = 2\n"
    "# 5 = the game's 16:9\n"
    "bd_aspect_ratio = 5\n"
    "input_backend = \"xinput\"\n"
    "# (the PC port's timing fixes in the runtime)\n"
    "gpu_wait_poll_us = 2000\n"
    "xmp_throttle_hammering_only = true\n"
    "# (a window, not fullscreen: fullscreen gives the hub and overlays a 4:3 screen on the PS5)\n"
    "fullscreen = false\n"
    "\n"
    "# test builds: more detail in the log\n"
    "[log.levels]\n"
    "fs = \"debug\"\n";

// The game's files: <app>/Assets (the app store's setup, or your disc image's files copied
// there), else a disc image (.iso) in the app folder, read in place.
fs::path FindGame(const fs::path& app) {
  std::error_code ec;
  for (const char* d : {"Assets", "assets"})
    if (fs::exists(app / d / "default.xex", ec)) return app / d;
  for (fs::directory_iterator it(app, ec), end; !ec && it != end; it.increment(ec)) {
    std::string ext = it->path().extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".iso" && it->is_regular_file(ec)) return it->path();
  }
  return app / "Assets";
}

}  // namespace

std::unique_ptr<rex::ui::WindowedApp> Svr2008App::Create(rex::ui::WindowedAppContext& ctx) {
  return std::unique_ptr<Svr2008App>(new Svr2008App(ctx, "svr2008", PPCImageConfig));
}

Svr2008App::~Svr2008App() {
  hub_frames_run_.store(false, std::memory_order_release);
  if (hub_frames_.joinable()) hub_frames_.detach();
}

void Svr2008App::OnPreSetup(rex::RuntimeConfig& config) {
  // The native renderer: the Xenos plugin's null backend keeps the game's D3D command stream
  // moving (fences, interrupts, vblank) without drawing; src/reblue draws through plume.
  if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
  if (!config.graphics) {
    config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, "null");
    REXLOG_INFO("Native renderer: guest GPU {}", config.graphics ? "null (sync only)" : "MISSING");
  }
}

std::unique_ptr<rex::ui::ImmediateDrawer> Svr2008App::OnCreateImmediateDrawer() {
  auto drawer = std::make_unique<bd::gpu::ImGuiOverlayDrawer>();
  overlay_drawer_ = drawer.get();
  return drawer;
}

void Svr2008App::OnConfigurePaths(rex::PathConfig& paths) {
  const fs::path app = rex::filesystem::GetExecutableFolder();
  if (paths.game_data_root.empty()) paths.game_data_root = FindGame(app);
  paths.update_data_root.clear();
  paths.user_data_root = app / "UserData";
  paths.cache_root = app / "UserData" / "cache";
  svr2008::InstallCrashReporter(app / "UserData" / "crashes");
  bool current = false;
  {
    std::ifstream in(paths.config_path);
    std::string first;
    std::getline(in, first);
    current = first.find(kConfigVersion) != std::string::npos;
  }
  if (!current) std::ofstream(paths.config_path, std::ios::trunc) << kDefaultConfig;
  g_config_path = paths.config_path;
}

// The renderer's device in two steps: the device and the swap chain (enough for the hub), and
// once the runtime exists its heap and back buffer (Video::CreateHostDevice does whichever is
// left; until the second, the renderer isn't ready and draws nothing).
bool Svr2008App::EnsureHostDevice() {
  if (device_created_) return bd::gpu::Video::CreateHostDevice(window());
  if (!bd::gpu::Video::CreateHostDevice(window())) {
    REXLOG_ERROR("Native renderer: host device creation failed");
    return false;
  }
  device_created_ = true;
  REXLOG_INFO("Native renderer: {} on {}", bd::gpu::Video::BackendName(), bd::gpu::Video::GetDeviceName());
  InstallOverlayHook();
  return true;
}

// The ImGui overlays (the hub, the game's on-screen keyboard) are drawn on top of each frame:
// the UI thread records them, and Present replays the latest recording (as on the PC).
void Svr2008App::InstallOverlayHook() {
  bd::gpu::Video::SetOverlayDrawHook([this](plume::RenderCommandList* cmd, plume::RenderFramebuffer* fb,
                                            uint32_t w, uint32_t h) {
    auto* drawer = overlay_drawer_;
    if (!imgui_drawer() || !imgui_drawer()->HasDialogs()) {
      if (drawer) drawer->ClearCapture();
      return;
    }
    if (!drawer || drawer->NeedsDirectDraw() || !drawer->ReplayLatest(cmd, fb, w, h)) {
      app_context().CallInUIThreadSynchronous([this, cmd, fb, w, h] {
        bd::gpu::ReblueUIDrawContext ctx(w, h, cmd, fb);
        imgui_drawer()->Draw(ctx);
      });
    }
    if (drawer && !overlay_capture_pending_.exchange(true)) {
      app_context().CallInUIThread([this, w, h] {
        if (imgui_drawer() && overlay_drawer_)
          overlay_drawer_->Capture(w, h, [this](rex::ui::UIDrawContext& ctx) { imgui_drawer()->Draw(ctx); });
        overlay_capture_pending_.store(false);
      });
    }
  });
}

void Svr2008App::StartHubFrames() {
  if (hub_frames_run_.exchange(true)) return;
  hub_frames_ = std::thread([this] {
    auto next = std::chrono::steady_clock::now();
    while (hub_frames_run_.load(std::memory_order_acquire)) {
      bd::gpu::Video::PresentOverlayFrame();
      next += std::chrono::microseconds(16667);
      const auto now = std::chrono::steady_clock::now();
      if (next < now) next = now;
      std::this_thread::sleep_until(next);
    }
  });
}

// (not from the UI thread while the frames run: a frame can be waiting on it)
void Svr2008App::StopHubFrames() {
  hub_frames_run_.store(false, std::memory_order_release);
  if (hub_frames_.joinable()) hub_frames_.join();
}

std::optional<rex::PathConfig> Svr2008App::OnFinalizePaths(const rex::PathConfig& defaults,
                                                           std::function<void(rex::PathConfig)> resume) {
  if (!imgui_drawer() || !EnsureHostDevice()) return defaults;
  const fs::path app = rex::filesystem::GetExecutableFolder();
  svr2008::hub::Paths paths{app, g_config_path, app / "UserData"};
  StartHubFrames();
  svr2008::hub::Show(imgui_drawer(), immediate_drawer(), paths, [this, defaults, resume, app] {
    // The hub's changes (svr2008.toml) apply to this start.
    rex::cvar::LoadConfig(g_config_path);
    rex::PathConfig start = defaults;
    start.game_data_root = FindGame(app);
    REXLOG_INFO("PS5 hub: START GAME (game {})", start.game_data_root.string());
    // (the frame thread may be waiting on this thread to draw the overlay: it's stopped and
    // joined elsewhere, then the start goes on here)
    hub_frames_run_.store(false, std::memory_order_release);
    std::thread([this, start, resume] {
      if (hub_frames_.joinable()) hub_frames_.join();
      app_context().CallInUIThread([start, resume] { resume(start); });
    }).detach();
  });
  return std::nullopt;
}

void Svr2008App::OnConfigureFonts(ImFontAtlas* atlas) {
  svr2008::hub::ConfigureFonts(atlas);
  g_fps_counter_font = svr2008::hub::AddCounterFont(atlas, 30.0f);
}

void Svr2008App::OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) { drawer_ = drawer; }

void Svr2008App::OnPreLaunchModule() {
  if (!EnsureHostDevice()) {
    app_context().QuitFromUIThread();
    return;
  }
  if (drawer_) {
    if (REXCVAR_GET(svr_fps_counter)) fps_counter_ = std::make_unique<FpsCounterDialog>(drawer_);
    pad_watch_ = std::make_unique<PadWatchDialog>(drawer_, runtime());
  }
}

void Svr2008App::OnPostLoadXexImage() {
  REXLOG_INFO("==== SmackDown vs. Raw 2008 PS5 port (test build) ====");
  REXLOG_INFO("game: {}", game_data_root().string());
}

void Svr2008App::OnWindowPixelSizeChanged(uint32_t, uint32_t) { bd::gpu::Video::RequestResize(); }

void Svr2008App::OnShutdown() {
  fps_counter_.reset();
  pad_watch_.reset();
  hub_frames_run_.store(false, std::memory_order_release);
  if (hub_frames_.joinable()) hub_frames_.detach();  // (it ends by itself)
  overlay_drawer_ = nullptr;
}
