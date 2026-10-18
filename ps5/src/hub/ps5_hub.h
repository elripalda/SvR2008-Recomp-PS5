// WWE SmackDown vs. Raw 2008 PS5 - the PS5 hub on the console (ps5_hub.cpp).
#pragma once

#if defined(__PROSPERO__)
#include <functional>

#include "hub_ui.h"

struct ImFontAtlas;
struct ImFont;
namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace rex::ui

namespace svr2008::hub {
// The hub's fonts (from OnConfigureFonts).
void ConfigureFonts(ImFontAtlas* atlas);
// The hub's fonts, for the port's things in the game.
const Assets& Fonts();
// The in-game FPS counter's font (Oswald, built into the app).
ImFont* AddCounterFont(ImFontAtlas* atlas, float size);
// Shows the hub; `launch` runs (on the UI thread) on START GAME.
void Show(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, const Paths& paths,
          std::function<void()> launch);
// The port's version ("Test 7"), as the hub shows it.
const char* Version();
}  // namespace svr2008::hub
#endif
