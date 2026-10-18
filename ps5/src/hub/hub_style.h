// WWE SmackDown vs. Raw 2008 PS5 - the PS5 hub's colours and small helpers
// (hub_ui.cpp).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

#include "imgui.h"

namespace svr2008::hub {

constexpr float kW = 1920, kH = 1080;
// the page's list on the left, the details panel on the right
constexpr float kListX = 112, kListW = 900, kListY = 262, kListView = 76 * 8;
constexpr float kPanelX = 1120, kPanelW = 640, kPanelY = 276;

inline ImU32 Rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
inline ImU32 White(float a) { return IM_COL32(255, 255, 255, int(std::clamp(a, 0.0f, 1.0f) * 255)); }
inline ImU32 Black(float a) { return IM_COL32(0, 0, 0, int(std::clamp(a, 0.0f, 1.0f) * 255)); }
inline ImU32 WithAlpha(ImU32 c, float a) {
  const int base = int((c >> IM_COL32_A_SHIFT) & 0xFF);
  return (c & ~IM_COL32_A_MASK) | (ImU32(std::clamp(int(base * a), 0, 255)) << IM_COL32_A_SHIFT);
}
inline float Smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}
inline ImU32 Mix(ImU32 a, ImU32 b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  ImU32 out = 0;
  for (int sh = 0; sh < 32; sh += 8) {
    const float ca = float((a >> sh) & 0xFF), cb = float((b >> sh) & 0xFF);
    out |= ImU32(ca + (cb - ca) * t + 0.5f) << sh;
  }
  return out;
}

const ImU32 kInk = Rgba(12, 13, 16);      // text on a focused (white) row
const ImU32 kAccent = Rgba(226, 36, 46);  // the one colour: SmackDown vs. Raw red
const ImU32 kText = Rgba(244, 245, 247);

inline std::string Upper(std::string s) {
  for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

inline std::string Size(uint64_t bytes) {
  char buf[32];
  if (bytes >= (1ull << 30)) std::snprintf(buf, sizeof(buf), "%.1f GB", double(bytes) / double(1ull << 30));
  else if (bytes >= (1ull << 20)) std::snprintf(buf, sizeof(buf), "%.1f MB", double(bytes) / double(1ull << 20));
  else std::snprintf(buf, sizeof(buf), "%.0f KB", double(bytes) / 1024.0);
  return buf;
}

// A folder's files: total size and count.
inline uint64_t FolderBytes(const std::filesystem::path& dir, uint32_t* files = nullptr) {
  uint64_t total = 0;
  uint32_t n = 0;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec)) total += it->file_size(ec), ++n;
  }
  if (files) *files = n;
  return total;
}

}  // namespace svr2008::hub
