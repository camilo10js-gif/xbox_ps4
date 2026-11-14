#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
: "${OO_PS4_TOOLCHAIN:?Set OO_PS4_TOOLCHAIN to OpenOrbis v0.5.4}"
: "${GOLDHEN_SDK:?Set GOLDHEN_SDK to GoldHEN SDK checkout}"
build="$repo/build"
mkdir -p "$build/sdk" "$build/pkg/assets" "$build/pkg/sce_sys/about" "$build/obj"
cp -R "$GOLDHEN_SDK"/crt "$GOLDHEN_SDK"/include "$GOLDHEN_SDK"/source "$GOLDHEN_SDK"/Makefile "$build/sdk/"
rm -f "$build/sdk/source/xbox_pad.cpp"
cp "$repo/xbox_pad.cpp" "$build/sdk/source/"
cp -R "$repo/shared" "$build/sdk/source/"
(cd "$build/sdk" && make TARGETCRT=crtprx.o clean crt && make TARGETCRT=crtprx.o TARGET=xbox_pad.prx xbox_pad.prx)
cp "$build/sdk/xbox_pad.prx" "$build/pkg/assets/"
cp "$OO_PS4_TOOLCHAIN/samples/font/assets/fonts/Gontserrat-Regular.ttf" "$build/pkg/assets/font.ttf"
cp "$OO_PS4_TOOLCHAIN/samples/font/assets/fonts/OFL.txt" "$build/pkg/assets/"
cp "$OO_PS4_TOOLCHAIN/samples/input/sce_sys/about/right.sprx" "$build/pkg/sce_sys/about/"
cp "$repo/app/icon0.png" "$build/pkg/sce_sys/"
flags=(--target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -DGRAPHICS_USES_FONT -isysroot "$OO_PS4_TOOLCHAIN" -isystem "$OO_PS4_TOOLCHAIN/include" -isystem "$OO_PS4_TOOLCHAIN/include/c++/v1" -I"$repo" -I"$OO_PS4_TOOLCHAIN/samples/_common")
clang++ "${flags[@]}" -c "$repo/app/main.cpp" -o "$build/obj/app.o"
clang++ "${flags[@]}" -c "$OO_PS4_TOOLCHAIN/samples/_common/graphics.cpp" -o "$build/obj/graphics.o"
ld.lld -m elf_x86_64 -pie --script "$OO_PS4_TOOLCHAIN/link.x" --eh-frame-hdr "$build/obj/"*.o "$OO_PS4_TOOLCHAIN/lib/crt1.o" -L"$OO_PS4_TOOLCHAIN/lib" -lc -lc++ -lkernel -lSceVideoOut -lScePad -lSceUserService -lSceSysmodule -lSceFreeType -o "$build/app.elf"
"$OO_PS4_TOOLCHAIN/bin/linux/create-fself" -in="$build/app.elf" -out="$build/app.oelf" --eboot "$build/pkg/eboot.bin" --paid 0x3800000000000011
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
pkgtool="$OO_PS4_TOOLCHAIN/bin/linux/PkgTool.Core"
cd "$build/pkg"
"$pkgtool" sfo_new sce_sys/param.sfo
for item in APP_TYPE:1 ATTRIBUTE:0 DOWNLOAD_DATA_SIZE:0 SYSTEM_VER:0; do "$pkgtool" sfo_setentry sce_sys/param.sfo "${item%%:*}" --type Integer --maxsize 4 --value "${item#*:}"; done
for key in APP_VER VERSION; do "$pkgtool" sfo_setentry sce_sys/param.sfo "$key" --type Utf8 --maxsize 8 --value 01.00; done
"$pkgtool" sfo_setentry sce_sys/param.sfo CATEGORY --type Utf8 --maxsize 4 --value gd
"$pkgtool" sfo_setentry sce_sys/param.sfo TITLE_ID --type Utf8 --maxsize 12 --value XBPAD0001
"$pkgtool" sfo_setentry sce_sys/param.sfo TITLE --type Utf8 --maxsize 128 --value "XboxPad Configurator"
content=IV0000-XBPAD0001_00-XBOXPADCONFIG001
"$pkgtool" sfo_setentry sce_sys/param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value "$content"
"$OO_PS4_TOOLCHAIN/bin/linux/create-gp4" -out project.gp4 --content-id="$content" --files "eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/about/right.sprx assets/font.ttf assets/OFL.txt assets/xbox_pad.prx"
"$pkgtool" pkg_build project.gp4 "$build"
"$pkgtool" pkg_validate "$build/$content.pkg"
cd "$build"
sha256sum "$content.pkg" sdk/xbox_pad.prx > SHA256SUMS
