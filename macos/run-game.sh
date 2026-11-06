#!/bin/sh
# Starts Baldur's Gate 3 with the probe preloaded. Run it from Terminal while Steam is running:
#
#   ./run-game.sh                 # the probe without patching game code
#   ./run-game.sh --code-probe    # also patch game code (BG3_WASD_CODE_PROBE=1)
#
# Steam for macOS can't set environment variables from the launch options: it opens the whole line
# as one file and fails with "OS Error 260". So the game is started here instead, and Steam's launch
# options must stay empty. BG3_APP overrides where the game is.
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
library="$here/build/libbg3-wasd-everywhere.dylib"
# Not "${BG3_APP:-…}": the apostrophe in the name would start a quoted string there in macOS's sh.
app="${BG3_APP:-}"
if [ -z "$app" ]; then
    app="$HOME/Library/Application Support/Steam/steamapps/common/Baldurs Gate 3/Baldur's Gate 3.app"
fi
log="$HOME/Library/Logs/bg3-wasd-everywhere/bg3-wasd-everywhere.log"

if [ ! -f "$library" ]; then
    echo "Build the probe first: make -C \"$here\"" >&2
    exit 1
fi
if [ "${1:-}" = "--code-probe" ]; then
    export BG3_WASD_CODE_PROBE=1
fi

rm -f "$log"
echo "Log: $log"
# Started outside Steam, the game would ask Steam to start it again, without these variables.
# SteamAppId tells the Steam API that the game is already running as app 1086940.
export SteamAppId=1086940 SteamGameId=1086940
export DYLD_INSERT_LIBRARIES="$library"
exec "$app/Contents/MacOS/Baldur's Gate 3"
