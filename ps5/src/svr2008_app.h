// WWE SmackDown vs. Raw 2008 - PS5 port: the app (nefariousjosiah's WWE-SVR2008-RECOMP and its
// native renderer on the ReXGlue runtime's PS5 platform layer). The renderer is the PC port's
// (src/reblue, src/native); this is the PS5's version of its src/svr2008_app.h, with the hub
// shown first.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <thread>

#include <rex/rex_app.h>

namespace bd::gpu {
class ImGuiOverlayDrawer;
}
namespace rex::ui {
class ImGuiDialog;
class ImGuiDrawer;
}

class Svr2008App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  ~Svr2008App() override;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx);

  void OnPreSetup(rex::RuntimeConfig& config) override;
  std::unique_ptr<rex::ui::ImmediateDrawer> OnCreateImmediateDrawer() override;
  void OnConfigurePaths(rex::PathConfig& paths) override;
  // The hub (ps5/src/hub) first; START GAME continues the start.
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override;
  void OnConfigureFonts(ImFontAtlas* atlas) override;
  void OnPreLaunchModule() override;
  void OnPostLoadXexImage() override;
  void OnWindowPixelSizeChanged(uint32_t, uint32_t) override;
  void OnShutdown() override;
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override;

 private:
  bool EnsureHostDevice();
  void InstallOverlayHook();
  void StartHubFrames();
  void StopHubFrames();

  bd::gpu::ImGuiOverlayDrawer* overlay_drawer_ = nullptr;  // owned by ReXApp
  std::atomic<bool> overlay_capture_pending_{false};
  bool device_created_ = false;
  // The hub's frames: the game isn't presenting yet, so a thread presents the overlay alone
  // (Video::PresentOverlayFrame) until START GAME.
  std::thread hub_frames_;
  std::atomic<bool> hub_frames_run_{false};
  // In the game: the FPS counter, and L3 + R3 logging a frame's draws (test builds).
  rex::ui::ImGuiDrawer* drawer_ = nullptr;
  std::unique_ptr<rex::ui::ImGuiDialog> fps_counter_;
  std::unique_ptr<rex::ui::ImGuiDialog> pad_watch_;
};
