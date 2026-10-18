#!/usr/bin/env python3
"""[ps5 port] Make generated/rexglue.cmake usable in a PS5 cross build (from the SvR 2011 port).

rexglue codegen writes generated/rexglue.cmake with a build step that runs the codegen tool. In
a cross build that tool would be built for the PS5 and can't run on the build machine, so the
step is skipped when CMAKE_CROSSCOMPILING: run codegen on the host first (it generates the same
C++ for every platform). Idempotent: run it after every codegen.

    ps5_patch_generated.py <rexglue.cmake> [project name]
"""
import pathlib, sys

path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "generated/rexglue.cmake")
name = sys.argv[2] if len(sys.argv) > 2 else "svr2008"

# The PS5's 16 KB pages: like Windows and arm64 macOS, guest addresses from 0xE0000000 sit
# 0x1000 higher in host memory (rex::memory::detail::PhysicalHostOffset).
pch = path.parent / "default" / f"{name}_pch.h"
if pch.exists():
    t = pch.read_text()
    old = "#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)\n"
    if old in t:
        pch.write_text(t.replace(old, "#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64) || REX_PLATFORM_PS5\n"))
        print(f"{pch}: 0xE0000000 host offset on the PS5")

s = path.read_text()
if "[ps5 port]" in s:
    print(f"{path}: already patched")
    sys.exit(0)

def rep(old, new):
    global s
    if old not in s:
        sys.exit(f"{path}: expected text not found: {old[:60]!r}")
    s = s.replace(old, new)

rep(f"        add_dependencies(${{target_name}}_recomp {name}_codegen)\n",
    "        if(NOT CMAKE_CROSSCOMPILING)  # [ps5 port]\n"
    f"            add_dependencies(${{target_name}}_recomp {name}_codegen)\n"
    "        endif()\n")
rep(f"    add_dependencies(${{target_name}} {name}_codegen)\n",
    "    if(NOT CMAKE_CROSSCOMPILING)  # [ps5 port]\n"
    f"        add_dependencies(${{target_name}} {name}_codegen)\n"
    "    endif()\n")
rep("add_custom_command(\n    OUTPUT \"${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp\"",
    "if(CMAKE_CROSSCOMPILING)\n"
    "    # [ps5 port] the codegen tool would be a PS5 program: run codegen on the host.\n"
    f"    add_custom_target({name}_codegen)\n"
    "else()\n"
    "add_custom_command(\n    OUTPUT \"${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp\"")
rep(f"add_custom_target({name}_codegen\n    DEPENDS \"${{CMAKE_CURRENT_SOURCE_DIR}}/generated/default/codegen.build.stamp\")\n",
    f"add_custom_target({name}_codegen\n    DEPENDS \"${{CMAKE_CURRENT_SOURCE_DIR}}/generated/default/codegen.build.stamp\")\n"
    "endif()\n")
path.write_text(s)
print(f"{path}: patched for cross builds")
