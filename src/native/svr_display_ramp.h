// SvR 2008: the console's display gamma table, applied when the frame is presented.
//
// The Xbox 360 sends the front buffer to the TV through a 256-entry gamma table (DC_LUT) that the
// game's D3D programs: its gamma ramp, reshaped for the kind of display the console reports (an
// HDTV here). With the null guest GPU those register writes still reach the runtime's command
// processor, which keeps the table; this hands it to the present pass.

#pragma once

namespace bd::gpu {

struct GuestTexture;

// Call once per frame before Video::Present, outside the renderer's lock. Rebuilds the table's
// 256x1 texture when the game changed it.
void SvrUpdateDisplayRamp();
// The table as a texture for the present pass, or nullptr (a straight line, or svr_display_ramp
// off): the frame is shown as written.
GuestTexture *SvrDisplayRamp();

}  // namespace bd::gpu
