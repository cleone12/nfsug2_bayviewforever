#!/bin/bash
# Builds BayviewForever.asi (every feature in one 32-bit Windows plugin) into ../../build/,
# plus build/BayviewForever.ini (the settings file that goes next to it in scripts\).
set -euo pipefail
export PATH="/opt/homebrew/bin:$PATH"
cd "$(dirname "$0")/.."

# Refuse to build if any byte check in src/ does not match the real speed2.exe.
python3 -I ../tools/check_codechecks.py

VERSION=$(sed -n 's/^#define BF_VERSION "\(.*\)"/\1/p' common/bayview_forever.h)
OUT="../build/BayviewForever.asi"
OBJ="../build/obj"
mkdir -p "$OBJ"
rm -f "$OBJ"/*.o

CXX="i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -DBAYVIEW_FOREVER -Icommon"
# Virtual Garage uses std::vector / std::string, so it keeps exceptions; the rest are built without.
$CXX -c virtual-garage/virtual_garage.cpp -o "$OBJ/virtual_garage.o"
for SRC in bayview-forever/main.cpp covers/covers.cpp drag-steering/drag_steering.cpp \
           endless-career/endless_career.cpp event-logger/event_logger.cpp garage-logger/garage_logger.cpp \
           h-shifter/h_shifter.cpp respawn-at-garage/respawn_at_garage.cpp \
           particle-fix/particle_fix.cpp \
           race-ai/race_ai.cpp surround/surround.cpp unique-parts/unique_parts.cpp \
           wheel-enhancements/wheel_enhancements.cpp; do
    $CXX -fno-exceptions -c "$SRC" -o "$OBJ/$(basename "$SRC" .cpp).o"
done

# libdinput8.a / libdxguid.a only supply static data (c_dfDIJoystick2, GUIDs) for the H-shifter; DirectInput8Create
# is loaded at runtime from the system folder, so the ASI does not import the game folder's dinput8.dll (the ASI loader).
i686-w64-mingw32-g++ -shared -static -static-libgcc -static-libstdc++ -s -o "$OUT" "$OBJ"/*.o \
    -ldinput8 -ldxguid -luser32 -lshell32 -ladvapi32

if i686-w64-mingw32-objdump -p "$OUT" | grep -qi "dinput8.dll"; then
    echo "ERROR: $OUT imports dinput8.dll" >&2
    exit 1
fi

# build/BayviewForever.ini may have been edited by hand (Chris tunes values there): never overwrite such edits.
# build/obj/BayviewForever.ini.copied = what the last build copied; if build/ differs from it, stop until it's merged.
COPIED="$OBJ/BayviewForever.ini.copied"
if [ -f ../build/BayviewForever.ini ] && [ -f "$COPIED" ] && ! cmp -s ../build/BayviewForever.ini "$COPIED"; then
    echo "ERROR: build/BayviewForever.ini was edited by hand since the last build. Merge the changes into" >&2
    echo "       src/bayview-forever/BayviewForever.ini (diff it against $COPIED), then delete $COPIED." >&2
    exit 1
fi
cp bayview-forever/BayviewForever.ini ../build/BayviewForever.ini
cp bayview-forever/BayviewForever.ini "$COPIED"
echo "Built $OUT v$VERSION (+ build/BayviewForever.ini)"
