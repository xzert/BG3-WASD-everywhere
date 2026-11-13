#!/bin/sh
# Builds the release archive dist/bg3-wasd-everywhere-macos-<version>.tar.gz. Runs on an Apple
# Silicon Mac with the Xcode Command Line Tools.
#
# The build goes to dist/.build, apart from build/, so a local config or an old build can't end up
# in the archive. The stand-in test (make check) must pass first.
set -eu

cd "$(dirname "$0")"
stem=$(sed -n 's/^NAME := //p' Makefile)
version=$(sed -n 's/^VERSION := //p' Makefile)
name=$stem-macos-$version
stage=dist/$name
build=dist/.build

rm -rf "$stage" "$stage.tar.gz" "$stage.tar.gz.sha256" "$build"
mkdir -p "$stage"

# CXXFLAGS goes through the environment: on the make command line it would also replace the
# Makefile's += part (-std=c++20 and the include paths).
CXXFLAGS=-O2 make BUILD="$build" "$build/lib$stem.dylib" "$build/$stem-launch" check

for file in "lib$stem.dylib" "$stem-launch"; do
    strip -x "$build/$file"
    # Apple Silicon runs only signed code; an ad-hoc signature is enough for a file without
    # quarantine (INSTALL.md says how to remove it).
    codesign --force --sign - "$build/$file"
    codesign --verify "$build/$file"
    lipo -archs "$build/$file" | grep -qx arm64
    cp "$build/$file" "$stage/"
done
exports=$(nm -gU "$stage/lib$stem.dylib" | awk '{ print $3 }' | xargs)
echo "Library exports: ${exports:-none}"

cp "$stem.toml" INSTALL.md ../LICENSE "$stage/"
tar -czf "$stage.tar.gz" --uid 0 --gid 0 -C dist "$name"
(cd dist && shasum -a 256 "$name.tar.gz" > "$name.tar.gz.sha256")
rm -rf "$build"
echo "Built $stage.tar.gz"
