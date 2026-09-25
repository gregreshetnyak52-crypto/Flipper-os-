#!/usr/bin/env bash
# Build FlipperOS: Unleashed firmware + our overlay, patches and apps.
#
#   firmware/build.sh                 # updater package (.tgz) in dist/
#   firmware/build.sh fw_dist         # any other fbt target(s)
#   firmware/build.sh flash_usb_full  # build and flash over USB
#
# Environment:
#   DIST_SUFFIX   version suffix of the output files (default: flipperos-local)
#   FBT_ARGS      extra fbt arguments (default: COMPACT=1 DEBUG=0)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/firmware/unleashed"

echo "==> Fetching Unleashed sources"
git -C "$ROOT" submodule update --init --depth 1 firmware/unleashed

echo "==> Resetting Unleashed tree to the pinned commit"
# Keep build outputs and the fbt toolchain between runs
git -C "$FW" reset --hard --quiet
git -C "$FW" clean -fdq -e /build -e /dist -e /toolchain -e /.sconsign.dblite

shopt -s nullglob
patches=("$ROOT"/firmware/patches/*.patch)
if ((${#patches[@]})); then
    echo "==> Applying ${#patches[@]} patch(es)"
    for patch in "${patches[@]}"; do
        echo "    $(basename "$patch")"
        git -C "$FW" apply --whitespace=nowarn "$patch"
    done
fi

echo "==> Copying overlay and apps"
cp -r "$ROOT/firmware/overlay/." "$FW/"
cp -r "$ROOT/flipper_os" "$FW/applications_user/"

cd "$FW"
if (($# == 0)); then
    set -- updater_package
fi
echo "==> ./fbt ${FBT_ARGS:-COMPACT=1 DEBUG=0} $*"
export FBT_GIT_SUBMODULE_SHALLOW=1
# shellcheck disable=SC2086
./fbt ${FBT_ARGS:-COMPACT=1 DEBUG=0} "$@"

echo "==> Done. Output: $FW/dist/"
