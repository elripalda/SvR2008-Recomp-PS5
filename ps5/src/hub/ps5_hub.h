// WWE SmackDown vs. Raw 2008 - the PS5 hub on the console (ps5_hub.cpp).
#pragma once

#if defined(__PROSPERO__)
#include <functional>

#include "hub_ui.h"

struct ImFontAtlas;
namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace rex::ui

namespace svr2008::hub {
// The hub's fonts (from OnConfigureFonts).
void ConfigureFonts(ImFontAtlas* atlas);
// Shows the hub; `launch` runs (on the UI thread) on START GAME.
void Show(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, const Paths& paths,
          std::function<void()> launch);
}  // namespace svr2008::hub
#endif
