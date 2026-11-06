#!/usr/bin/env bash
# Builds the release archive dist/bg3-wasd-everywhere-linux-<version>.tar.gz.
#
# The library is built inside the Steam Runtime 3.0 (sniper) SDK, the runtime Steam runs the native
# game in. Built there, it needs only the glibc that the runtime ships (2.31), so it loads on every
# distribution Steam supports. Needs Docker.
#
# Optional environment: SDK_IMAGE (the SDK image), SDK_CXX (the compiler inside it).
set -euo pipefail

cd "$(dirname "$0")"
sdk_image=${SDK_IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest}
sdk_cxx=${SDK_CXX:-g++}
stem=$(sed -n 's/^NAME := //p' Makefile)
version=$(sed -n 's/^VERSION := //p' Makefile)
name=$stem-linux-$version
stage=dist/$name

rm -rf "$stage" "$stage.tar.gz" "$stage.tar.gz.sha256"
mkdir -p "$stage"

# The sources go into the container on stdin and the library comes back on stdout, so nothing has
# to be mounted. Everything the build prints goes to stderr.
tar -c Makefile "$stem.toml" exports.map src tests |
docker run --rm -i -e CXX="$sdk_cxx" -e STEM="$stem" "$sdk_image" bash -euo pipefail -c '
    exec 3>&1 1>&2
    mkdir /tmp/build && cd /tmp/build && tar -x
    . /etc/os-release
    echo "SDK: $PRETTY_NAME, build ${BUILD_ID:-unknown}; $($CXX --version | head -n 1)"
    so=build/lib$STEM.so
    make -j"$(nproc)" CXX="$CXX" "$so" "build/$STEM.toml"
    strip --strip-unneeded "$so"

    glibc=$(objdump -T "$so" | grep -o "GLIBC_[0-9.]*" | sort -u -V | tail -n 1)
    needed=$(readelf -d "$so" | sed -n "s/.*Shared library: \[\(.*\)\]/\1/p" | xargs)
    exports=$(nm -D --defined-only "$so" | awk "{ print \$3 }" | xargs)
    echo "Needs $glibc; libraries: $needed; exports: $exports"
    if [ "$exports" != "SDL_GetWindowGrab SDL_PollEvent" ]; then
        echo "Unexpected exports"
        exit 1
    fi

    # Load test with the runtime glibc: in a process named bg3, the constructor reads the config,
    # finds no game code to patch and disables itself.
    cp /bin/true /tmp/bg3
    XDG_STATE_HOME=/tmp/state LD_PRELOAD="$PWD/$so" /tmp/bg3
    log=/tmp/state/$STEM/$STEM.log
    sed "s/^/  log: /" "$log"
    grep -q "Config loaded" "$log"
    grep -q "Mod disabled" "$log"

    tar -c -C build "lib$STEM.so" >&3
' | tar -x -C "$stage"

cp "$stem.toml" INSTALL.md ../LICENSE "$stage/"
tar -czf "$stage.tar.gz" --owner=0 --group=0 --numeric-owner --sort=name -C dist "$name"
(cd dist && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
echo "Built $stage.tar.gz"
