#!/bin/sh
# Runs the stand-in as "Baldur's Gate 3" with the library preloaded, then prints the stand-in's
# output and the library's log. The library goes to a scratch folder with the stand-in's test config
# next to it, and HOME points there too, so the log, the config and the key bindings the library
# writes don't touch the real ones.
#
# Usage: run-standin.sh <library> <stand-in executable>
set -u

dir="$(mktemp -d)"
library="$dir/libbg3-wasd-everywhere.dylib"
config="$dir/bg3-wasd-everywhere.toml"
cp "$1" "$library"
cp "$2" "$dir/Baldur's Gate 3"
"$2" --write-config "$config" || exit 1
home="$dir/home"
profile="$home/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles/Public"
mkdir -p "$home/Library/Logs" "$profile"
log="$home/Library/Logs/bg3-wasd-everywhere/bg3-wasd-everywhere.log"
status=0

HOME="$home" DYLD_INSERT_LIBRARIES="$library" "$dir/Baldur's Gate 3" "$config"
result=$?
echo "exit status $result"
[ "$result" -eq 0 ] || status=1
if [ -f "$log" ]; then
    sed 's/^/  log: /' "$log"
else
    echo "  no log: the library didn't load"
    status=1
fi

echo "=== key bindings written to the scratch profile"
grep -o -E '"CharacterMove[A-Za-z]+"' "$profile/inputconfig_p1.json" 2>/dev/null | sort | uniq -c || status=1

rm -rf "$dir"
exit $status
