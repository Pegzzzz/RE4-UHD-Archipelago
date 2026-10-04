#!/bin/bash
# Behavioral test harness for dllmain/Archipelago.cpp.
# Compiles the module against a simulated RE4 game state (sim.h/stubs.cpp) and runs scenarios under wine.
#
# Needs: clang, mingw-w64 (i686), wine + wine32, and an Archipelago checkout with worlds/re4uhd linked in ($AP).
#   AP=/path/to/Archipelago ./build.sh
set -e
H="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$H/../../.." && pwd)"
W="${WORK:-$H/.work}"

# 1. A copy of the source tree the SDK headers can be parsed from with clang (MSVC-only bits patched)
rm -rf "$W/tree" && mkdir -p "$W/tree"
cp -r "$REPO/dllmain" "$REPO/includes" "$REPO/settings" "$REPO/external" "$W/tree/"
find "$W/tree" \( -name "*.h" -o -name "*.hpp" -o -name "*.cpp" \) -print0 | xargs -0 sed -i -E '/^\s*#\s*include/ s#\\#/#g'
printf '#pragma once\nnamespace injector { struct reg_pack {}; }\n' > "$W/tree/external/injector/include/injector/assembly.hpp"
sed -i 's/^\s*struct GLOBAL_WK::RTP\* Rtp_4F2C;/\tvoid* Rtp_4F2C;/' "$W/tree/dllmain/SDK/global.h"
sed -i 's/struct Message::MessageFont\*/void*/' "$W/tree/dllmain/SDK/message.h"
sed -i 's/^enum ID_CLASS;/enum ID_CLASS : int;/; s/^enum ID_CLASS\s*$/enum ID_CLASS : int/' "$W/tree/dllmain/SDK/ID.h"

# 2. slot_data and location ids from the real APWorld
SKIP_REQUIREMENTS_UPDATE=1 python3 "$H/gen_slot.py" "$W/slot_data.json" > /dev/null 2>&1


# 3. build + run
cd "$W/tree/dllmain"
FLAGS="--target=i686-w64-mingw32 -std=c++17 -DNOMINMAX -fms-extensions -fasm-blocks -O1 -g0 -w
 -I. -I$H -I$W -I../settings -I../includes -I../external -I../external/ModUtils -I../external/injector/include
 -I../external/spdlog/include -I../external/imgui -I../external/json/include -I../external/DirectXMath/Inc -I../external/SimpleIni"
clang++ $FLAGS -c "$H/stubs.cpp" -o "$W/stubs.o"
clang++ $FLAGS -c "$H/test.cpp" -o "$W/test.o"
clang++ --target=i686-w64-mingw32 -static "$W/test.o" "$W/stubs.o" -o "$W/ap_test.exe" -lws2_32 2>&1 | grep -v "duplicate section" || true
cd "$W"
rm -rf re4_tweaks
WINEDEBUG=-all timeout 300 wine ./ap_test.exe 2>&1 | tee "$W/last_run.txt"
