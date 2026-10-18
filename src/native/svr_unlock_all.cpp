// SvR 2008: Unlock Everything (hub setting).
//
// Everything the game unlocks - the legends, the arenas, the WWE Shop's attires, moves and
// extras - is a bit in the profile (96 bits at profile + 0x1A154), and the game only ever reads
// them through one function, sub_82579F80(id): the shop (sub_824B4530), the superstar and
// arena lists (sub_8257A0C0, sub_8257A2A0), the moves list, the GM options. With the setting on,
// that answers "unlocked" for every id. The save itself isn't changed: turn the setting off and
// the game shows what you've unlocked yourself again; anything you unlock in the meantime is
// still saved as the game normally would (sub_82579FD8 writes the bits directly).

#include <atomic>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>
#include <rex/ppc/function.h>

#include "core/logging.h"

REXCVAR_DEFINE_BOOL(svr_unlock_all, false, "SvR",
                    "Every superstar, arena and WWE Shop item is open (the save isn't changed)");

namespace {
std::atomic<bool> g_logged{false};
}

// bool IsUnlocked(int id)
REX_EXTERN(__imp__sub_82579F80);
REX_HOOK_RAW(sub_82579F80) {
  if (REXCVAR_GET(svr_unlock_all)) {
    if (!g_logged.exchange(true))
      BD_INFO("[svr] unlock everything: on (first asked for id {})", ctx.r3.s32);
    ctx.r3.u64 = 1;
    return;
  }
  __imp__sub_82579F80(ctx, base);
}
