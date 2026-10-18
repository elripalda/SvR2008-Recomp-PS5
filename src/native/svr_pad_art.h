// SvR 2008: PlayStation button pictures in place of the game's Xbox ones.
//
// ps_buttons.bin (made by ps5/tools/ps_buttons from the game's own files) lists the game
// textures that show Xbox buttons, by the content key of their top level, and the edits that
// turn each into the PlayStation version: the drawn PlayStation pictures pasted over the Xbox
// ones, the controls card's lines and controller redrawn. When the game loads one of those
// textures, its mirror is built from the edited picture instead.

#pragma once

#include <filesystem>

#include <rex/types.h>

namespace bd::gpu {

struct GuestTexture;

// Reads the pack (once, before the game starts); false when it's missing or not usable, or the
// setting is off (the game's own pictures then).
bool SvrPadArtLoad(const std::filesystem::path &file);

// From the mirror build of a 2D DXT texture: `staging` holds its top level as host BC blocks,
// rows `staging_row_bytes` apart. The edited picture's host texture when the texture is one of
// the pack's, else nullptr.
GuestTexture *SvrPadArtReplace(u32 width, u32 height, u32 format, const u8 *staging,
                               u32 staging_row_bytes);

}  // namespace bd::gpu
