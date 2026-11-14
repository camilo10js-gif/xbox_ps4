#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests
g++ -std=c++17 -fsanitize=address,undefined -g -I. tests/config_test.cpp -o build/tests/config_test
build/tests/config_test
if [[ -n "${GOLDHEN_SDK:-}" ]]; then
 clang -Itests/stubs -I"$GOLDHEN_SDK/include" -c "$GOLDHEN_SDK/source/Detour.c" -o build/tests/detour.o
 clang -Itests/stubs -I"$GOLDHEN_SDK/include" -c "$GOLDHEN_SDK/source/HDE64.c" -o build/tests/hde.o
 clang++ -std=c++17 -Itests/stubs -I. tests/hooks_test.cpp build/tests/detour.o build/tests/hde.o -o build/tests/hooks_test
 build/tests/hooks_test
fi
clang++ -std=c++17 -ffunction-sections -fdata-sections -Itests/stubs -I. tests/usb_test.cpp -Wl,--gc-sections -o build/tests/usb_test
build/tests/usb_test
