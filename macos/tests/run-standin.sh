#!/bin/sh
# Runs the stand-in as "Baldur's Gate 3" with the library preloaded: once as is, once with the code
# probe. Prints the stand-in's output and the library's log after each run. HOME points at a scratch
# folder, so the log and the key bindings the library writes don't touch the real ones.
#
# Usage: run-standin.sh <probe dylib> <stand-in executable>
set -u

library="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
dir="$(mktemp -d)"
cp "$2" "$dir/Baldur's Gate 3"
home="$dir/home"
profile="$home/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles/Public"
mkdir -p "$home/Library/Logs" "$profile"
log="$home/Library/Logs/bg3-wasd-everywhere/bg3-wasd-everywhere.log"
status=0

for code_probe in 0 1; do
    echo "=== BG3_WASD_CODE_PROBE=$code_probe"
    rm -f "$log"
    HOME="$home" BG3_WASD_CODE_PROBE=$code_probe DYLD_INSERT_LIBRARIES="$library" "$dir/Baldur's Gate 3"
    result=$?
    echo "exit status $result"
    [ "$result" -eq 0 ] || status=1
    if [ -f "$log" ]; then
        sed 's/^/  log: /' "$log"
    else
        echo "  no log: the probe didn't load"
        status=1
    fi
done

echo "=== key bindings written to the scratch profile"
grep -o -E '"CharacterMove[A-Za-z]+"' "$profile/inputconfig_p1.json" 2>/dev/null | sort | uniq -c || status=1

rm -rf "$dir"
exit $status
