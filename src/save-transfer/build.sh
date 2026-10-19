#!/bin/bash
# Builds NFSU2 Save Transfer (a 32-bit Windows program) into ../../build/.
# Test the save logic on the Mac first: ./test.sh
set -euo pipefail
export PATH="/opt/homebrew/bin:$PATH"
cd "$(dirname "$0")"

VERSION=$(sed -n 's/^#define TOOL_VERSION "\(.*\)"/\1/p' save_transfer.cpp)
OUT="../../build/NFSU2SaveTransfer-v${VERSION}.exe"
RES="${TMPDIR:-/tmp}/nfsu2_save_transfer_res.o"

i686-w64-mingw32-windres app.rc -O coff -o "$RES"
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -municode -mwindows \
    -static -static-libgcc -static-libstdc++ -s \
    -o "$OUT" save_transfer.cpp save_core.cpp "$RES" -lcomctl32 -lshell32 -lole32

echo "Built $OUT"
