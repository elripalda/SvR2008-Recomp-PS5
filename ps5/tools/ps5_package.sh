#!/usr/bin/env bash
# [ps5 port] Link the PS5 build of SvR 2008 and package it as a PS5 homebrew
# app folder (eboot.bin, sce_module/libc.prx, sce_sys/param.json).
#
#   tools/ps5_package.sh <cmake build dir> <ps5-plume-triangle checkout> [title id]
#
# The link and packaging reuse the PS5 tooling of Nazky's ps5-plume-triangle
# / UnleashedRecomp-Prospero (mihawk-99's RADV link recipe, app CRT, libc.prx
# builder and FSELF signer): build that project once first (its build/ holds
# app_crt.o, app_cpp_runtime.o, the libSceAgc stubs and ps5-native-tool).
# No game files are packaged: copy your installed game folder into the app
# folder on the PS5 (see docs/PS5_PORT_PLAN.md).
set -euo pipefail

build=$(realpath "${1:?cmake build dir}")
tri=$(realpath "${2:?ps5-plume-triangle checkout}")
title_id=${3:-PPSA92008}
port=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

sdk_root=${PS5_PAYLOAD_SDK:-$tri/.deps/native/ps5-payload-sdk}
radv_archive=${RADV_ARCHIVE:-$tri/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
tool=$tri/build/host/ps5-native-tool
for f in "$sdk_root/bin/prospero-lld" "$radv_archive" "$tool" "$tri/build/obj/app_crt.o" \
         "$tri/build/obj/app_cpp_runtime.o" "$tri/build/stubs/libSceAgc.so" "$tri/runtime/libc.prx"; do
    [[ -e $f ]] || { echo "missing $f (build ps5-plume-triangle first)" >&2; exit 2; }
done

# Staging root for radv_link_recipe: it reads tooling/ and vendor/ and writes build/.
stage=$build/ps5-link
mkdir -p "$stage/build"
ln -sfn "$tri/tooling" "$stage/tooling"
ln -sfn "$tri/vendor" "$stage/vendor"

# The objects and static libraries CMake links into svr2008, in its order.
link_cmd=$(ninja -C "$build" -t commands svr2008 | tail -1)
mapfile -t inputs < <(cd "$build" && for w in $link_cmd; do
    case $w in
        *.o|*.a) [[ $w = /* ]] && echo "$w" || echo "$build/$w" ;;
    esac
done)
echo "==> [link] ${#inputs[@]} objects and libraries from CMake"

export PS5_CLANG=${PS5_CLANG:-$(command -v clang-19 || command -v clang)}
source "$tri/tools/radv-link.sh"
radv_link_recipe "$stage" "$sdk_root" "$radv_archive"
# (the recipe's layout, with a fixed gap for the process parameters)
radv_linker_script=(-T "$port/tools/ps5-pie-svr2008.ld")

echo "==> [link] SvR 2008 + RADV into a PS5 PIE ELF"
"$sdk_root/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$tri/tooling/native/app-symbols.map" --exclude-libs=ALL \
    -e _start -o "$stage/build/llvm-pie.elf" \
    "$tri/build/obj/app_crt.o" "$tri/build/obj/app_cpp_runtime.o" \
    --start-group "${inputs[@]}" --end-group \
    "$tri/build/stubs/libSceAgc.so" "$tri/build/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "$sdk_root"/target/lib/*.so

echo "==> [package] eboot.bin for $title_id"
"$tool" link --in "$stage/build/llvm-pie.elf" --out "$stage/build/eboot.elf" \
    --stub-dir "$sdk_root/target/lib" --stub "$tri/build/stubs/libSceAgc.so" \
    --stub "$tri/build/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf
app=$build/ps5-app/$title_id
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$stage/build/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$tri/runtime/libc.prx" "$app/sce_module/libc.prx"
cat > "$app/sce_sys/param.json" <<EOF
{
  "ageLevel": { "default": 0 },
  "applicationCategoryType": 0,
  "applicationDrmType": "free",
  "attribute": 0,
  "attribute2": 0,
  "attribute3": 0,
  "conceptId": "${title_id:4}",
  "contentBadgeType": 1,
  "contentId": "UP9000-${title_id}_00-SVR2008PS5000000",
  "contentVersion": "01.000.000",
  "downloadDataSize": 256,
  "gameIntent": { "permittedIntents": [ { "intentType": "launchActivity" } ] },
  "localizedParameters": {
    "defaultLanguage": "en-US",
    "en-US": { "titleName": "SmackDown vs. Raw 2008" }
  },
  "masterVersion": "01.00",
  "pubtools": { "creationDate": "2026-10-11 00:00:00", "loudnessSnd0": "-28.00", "toolVersion": "2.00" },
  "requiredSystemSoftwareVersion": "0x0000000000000000",
  "sdkVersion": "0x0000000000000000",
  "titleId": "${title_id}",
  "versionFileUri": ""
}
EOF
# The PC release's pipeline list (render states and shader hashes, no game data): the pipelines
# are built while each scene loads (generated/native/pipelines.bin, from SVR2008-NATIVE.zip)
if [[ -f $port/../generated/native/pipelines.bin ]]; then
    cp "$port/../generated/native/pipelines.bin" "$app/pipelines.bin"
fi
# Where the game's files go (the app store's setup, or your disc image's files over FTP)
mkdir -p "$app/Assets"
printf 'Put the files from your WWE SmackDown vs. Raw 2008 disc image here (default.xex, pac, sound...),\nor put the .iso itself in the app folder.\n' > "$app/Assets/PUT YOUR GAME FILES HERE.txt"
# PlayStation button pictures (art/local/pad_icons, made from your game files:
# never in the repo) -> <app>/pad_icons
if [[ -d $port/art/local/pad_icons ]]; then
    mkdir -p "$app/pad_icons"
    cp "$port"/art/local/pad_icons/*.dds "$app/pad_icons/"
fi
# The hub's art and menu music (art/local/hub: logo.png, hero.jpg, buttons/,
# music/ - your own, never in the repo) -> <app>/hub
if [[ -d $port/art/local/hub ]]; then
    cp -r "$port/art/local/hub" "$app/hub"
fi
# Home screen art (your own, from assets/ps5/): icon0.png 512x512; pic0.dds /
# pic1.dds the 3840x2160 BC7 background and launch splash.
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    # art/local/ (gitignored) holds your own art; art/ the placeholders
    if [[ -f $port/art/local/$asset ]]; then
        cp "$port/art/local/$asset" "$app/sce_sys/$asset"
    elif [[ -f $port/art/$asset ]]; then
        cp "$port/art/$asset" "$app/sce_sys/$asset"
    fi
done
"$tool" self --inspect --file "$app/eboot.bin" | tail -3
echo "==> $app ($(stat -c %s "$app/eboot.bin") bytes)"
