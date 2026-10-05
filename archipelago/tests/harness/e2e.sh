#!/bin/bash
# End-to-end test: the game module (simulated game, under wine) + the real RE4 UHD Client + a real MultiServer.
# Run build.sh first (it prepares the source tree and the stub objects).
#   AP=/path/to/Archipelago ./e2e.sh
set -o pipefail
H="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$H/../../.." && pwd)"
W="${WORK:-$H/.work}"
AP="${AP:?set AP to an Archipelago checkout with worlds/re4uhd linked in}"
E="$W/e2e"
rm -rf "$E" && mkdir -p "$E/players" "$E/out"

# 1. a seed: Leon (default YAML + a starting Shotgun) and a second RE4 player
sed -e 's/^name: Leon/name: Leon/' "$REPO/archipelago/templates/Resident Evil 4 UHD.yaml" > "$E/players/leon.yaml"
cat >> "$E/players/leon.yaml" <<'EOF'
  start_inventory:
    Shotgun: 1
EOF
sed -e 's/^name: Leon/name: Ashley/' "$REPO/archipelago/templates/Resident Evil 4 UHD.yaml" > "$E/players/ashley.yaml"
(cd "$AP" && SKIP_REQUIREMENTS_UPDATE=1 python3 Generate.py --player_files_path "$E/players" --outputpath "$E/out" \
  --seed 4242 > "$E/generate.log" 2>&1) || { tail -20 "$E/generate.log"; exit 1; }
ZIP=$(ls "$E/out"/*.zip | head -1)

# 2. the game module in end-to-end mode
cd "$W/tree/dllmain"
FLAGS="--target=i686-w64-mingw32 -std=c++17 -DNOMINMAX -fms-extensions -fasm-blocks -O1 -g0 -w -DAP_E2E
 -I. -I$H -I$W -I../settings -I../includes -I../external -I../external/ModUtils -I../external/injector/include
 -I../external/spdlog/include -I../external/imgui -I../external/json/include -I../external/DirectXMath/Inc -I../external/SimpleIni"
clang++ $FLAGS -c "$H/test.cpp" -o "$W/test_e2e.o" || exit 1
clang++ --target=i686-w64-mingw32 -static "$W/test_e2e.o" "$W/stubs.o" -o "$E/ap_e2e.exe" -lws2_32 2>&1 | grep -v "duplicate section"

# 3. server, game, client
PORT=38291
(cd "$AP" && SKIP_REQUIREMENTS_UPDATE=1 exec python3 MultiServer.py --host 127.0.0.1 --port $PORT "$ZIP" > "$E/server.log" 2>&1) &
SERVER=$!
sleep 8
(cd "$E" && WINEDEBUG=-all exec timeout 150 wine ./ap_e2e.exe > "$E/game.log" 2>&1) &
GAME=$!
sleep 3
(cd "$AP" && SKIP_REQUIREMENTS_UPDATE=1 exec timeout 150 python3 -c "
import sys; sys.argv = ['client', '--nogui']
import ModuleUpdate
from worlds.re4uhd.client import launch
launch('--nogui', 'archipelago://Leon@127.0.0.1:$PORT')
" > "$E/client.log" 2>&1) &
CLIENT=$!

wait $GAME
RESULT=$?
sleep 2
kill $CLIENT $SERVER 2>/dev/null
wait 2>/dev/null

cat "$E/game.log"
if ! grep -q "has completed their goal" "$E/server.log"; then
  echo "FAIL  the server never saw Leon's goal"
  RESULT=1
else
  echo "ok    the server saw Leon's goal"
fi
SEED=$(basename "$ZIP" .zip); SEED=${SEED#AP_}
# players are numbered alphabetically by YAML file: ashley.yaml is slot 1, leon.yaml slot 2
WANT=$(python3 -c "import zlib; print(zlib.crc32(b'$SEED:0:2') or 1)")
if grep -q "SAVE_TAG $WANT" "$E/game.log"; then
  echo "ok    the save is tagged with this room's seed"
else
  echo "FAIL  save tag isn't derived from the room's seed (want $WANT)"
  RESULT=1
fi
if grep -q "doesn't match this APWorld" "$E/client.log"; then
  echo "FAIL  the client says the game mod doesn't match the APWorld"
  RESULT=1
fi
if grep -q "different seed; ignored" "$E/client.log"; then
  echo "FAIL  the client rejected the game's checks as another seed's"
  RESULT=1
fi
exit $RESULT
