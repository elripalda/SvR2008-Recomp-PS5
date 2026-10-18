// SvR 2008: the console's display gamma table (see svr_display_ramp.h).

#include "svr_display_ramp.h"

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/xenos.h>
#include <rex/runtime.h>

#include "core/logging.h"
#include "gpu/resources.h"
#include "gpu/texture_upload.h"

REXCVAR_DEFINE_BOOL(svr_display_ramp, true, "SvR",
                    "Show the picture through the console's display gamma table, as an Xbox 360 "
                    "on a TV does (deeper shadows); off shows the frame as the game drew it");

namespace bd::gpu {
namespace {

std::atomic<GuestTexture *> g_ramp{nullptr};
GuestTexture *g_ramp_texture = nullptr;  // kept when the table goes back to a straight line
u32 g_table[256] = {};
bool g_have_table = false;

const rex::graphics::CommandProcessor *GuestCommandProcessor() {
  auto *runtime = rex::Runtime::instance();
  if (!runtime)
    return nullptr;
  auto *graphics = static_cast<rex::graphics::GraphicsSystem *>(runtime->graphics_system());
  return graphics ? graphics->command_processor() : nullptr;
}

// DC_LUT_30_COLOR: blue [9:0], green [19:10], red [29:20].
u32 Channel(u32 entry, int c) { return (entry >> (20 - 10 * c)) & 0x3FFu; }

bool StraightLine(const u32 *t) {
  for (u32 i = 0; i < 256; ++i) {
    const int want = int(i * 1023u / 255u);
    for (int c = 0; c < 3; ++c) {
      const int d = int(Channel(t[i], c)) - want;
      if (d < -2 || d > 2)
        return false;
    }
  }
  return true;
}

void LogTable(const u32 *t, bool straight) {
  std::string line;
  for (u32 i : {0u, 16u, 32u, 64u, 96u, 128u, 160u, 192u, 224u, 255u})
    line += fmt::format(" {}:{:.3f}/{:.3f}/{:.3f}", i, Channel(t[i], 0) / 1023.0,
                        Channel(t[i], 1) / 1023.0, Channel(t[i], 2) / 1023.0);
  BD_INFO("[display] console gamma table{} (input:R/G/B){}", straight ? " (straight line)" : "",
          line);
}

}  // namespace

void SvrUpdateDisplayRamp() {
  const auto *cp = GuestCommandProcessor();
  if (!cp)
    return;
  const auto *entries = cp->guest_gamma_ramp_256_entry_table();
  u32 table[256];
  for (u32 i = 0; i < 256; ++i)
    table[i] = entries[i].value & 0x3FFFFFFFu;
  if (g_have_table && std::memcmp(table, g_table, sizeof(table)) == 0) {
    // Unchanged; follow the setting.
    g_ramp.store(REXCVAR_GET(svr_display_ramp) && !StraightLine(g_table) ? g_ramp_texture : nullptr,
                 std::memory_order_relaxed);
    return;
  }
  std::memcpy(g_table, table, sizeof(table));
  g_have_table = true;
  const bool straight = StraightLine(table);
  LogTable(table, straight);
  if (straight) {
    g_ramp.store(nullptr, std::memory_order_relaxed);
    return;
  }
  // 256x1 RGBA16: the 10-bit outputs as 16-bit unorm (one 2 KiB row, already 256-byte aligned).
  std::vector<u16> texels(256 * 4);
  for (u32 i = 0; i < 256; ++i) {
    for (int c = 0; c < 3; ++c) {
      const u32 v = Channel(table[i], c);
      texels[i * 4 + c] = u16((v << 6) | (v >> 4));
    }
    texels[i * 4 + 3] = 0xFFFF;
  }
  if (g_ramp_texture)
    SetMirrorReuseTarget(g_ramp_texture);
  GuestTexture *tex = BuildBCMirrorTexture(
      256, 1, u32(rex::graphics::xenos::TextureFormat::k_16_16_16_16), texels.data(),
      texels.size() * sizeof(u16), 256 * 8, 256);
  SetMirrorReuseTarget(nullptr);
  if (!tex) {
    BD_WARN("[display] console gamma table: texture upload failed; showing the frame as written");
    g_ramp.store(nullptr, std::memory_order_relaxed);
    return;
  }
  g_ramp_texture = tex;
  g_ramp.store(REXCVAR_GET(svr_display_ramp) ? tex : nullptr, std::memory_order_relaxed);
}

GuestTexture *SvrDisplayRamp() { return g_ramp.load(std::memory_order_relaxed); }

}  // namespace bd::gpu
