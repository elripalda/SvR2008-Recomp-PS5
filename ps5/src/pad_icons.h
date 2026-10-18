// WWE SmackDown vs. Raw 2008 - PlayStation button pictures.
//
// The game's button icons are textures in its pac archives: the fonts' icon
// pages (every button shown inside text, menus and matches) and a few button
// pictures in the match HUD. <app>/pad_icons holds PlayStation versions, each
// named after the Xbox texture it replaces (<FNV-1a 64 of the DDS>.dds, same
// size and format). At start, each pac that holds one of those textures is
// copied to UserData/pad_icons with the textures swapped in place (their
// containers are not compressed, so nothing else moves), and the game is
// pointed at the copies. The originals are never written; without
// <app>/pad_icons the game keeps its Xbox buttons.
#pragma once

#include <filesystem>

namespace rex::filesystem {
class VirtualFileSystem;
}

namespace svr2008 {

void InstallPadIcons(rex::filesystem::VirtualFileSystem* vfs, const std::filesystem::path& game,
                     const std::filesystem::path& app);

}  // namespace svr2008
