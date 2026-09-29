#!/usr/bin/env bash
# Build FlipperOS: Unleashed firmware + our overlay, patches and apps.
#
#   firmware/build.sh                 # updater package (.tgz) in dist/
#   firmware/build.sh fw_dist         # any other fbt target(s)
#   firmware/build.sh flash_usb_full  # build and flash over USB
#
# Environment:
#   DIST_SUFFIX    version suffix of the output files (default: flipperos-local)
#   FBT_ARGS       extra fbt arguments (default: COMPACT=1 DEBUG=0)
#   FLIPPEROS_PACK community apps that go on the SD card with the update:
#                    base   (default) what a plain Unleashed release ships
#                    extra  base + all extra apps, like the Unleashed "e" release
#                    none   just the firmware and our apps ("clean" release)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/firmware/unleashed"
PACK="${FLIPPEROS_PACK:-base}"

# Unleashed does not build the community apps with the firmware: its release
# pipeline copies prebuilt packs from xMasterX/all-the-plugins into
# extra_resources/resources before packaging the update. The source tree alone
# yields the "clean" flavour, which is missing e.g. the Sub-GHz bruteforcer,
# spectrum analyzer, ESP32/NRF24/GPS tools and most games.
# Pinned like the Unleashed submodule: bump both together, and pick the pack
# release built for the same API (all-the-plugins tags follow Unleashed's).
PLUGINS_URL="https://github.com/xMasterX/all-the-plugins/releases/download/9sep2026p2"
PLUGINS_BASE_SHA256="7c5481a905b78e596a15851e14de3f192e4c0439239a4e967cae9fc01622721f"
PLUGINS_EXTRA_SHA256="720a274f0897375c331cc8bedb1b006f9474e62acb630041fef132b11c8950e2"

fetch_pack() { # name sha256
    local name="all-the-apps-$1.tgz" cache="$ROOT/firmware/.cache"
    mkdir -p "$cache"
    if [[ ! -f "$cache/$name" ]] || ! echo "$2  $cache/$name" | sha256sum -c --status; then
        echo "    downloading $name" >&2
        curl -fsSL --retry 4 --retry-delay 3 -o "$cache/$name" "$PLUGINS_URL/$name"
    fi
    echo "$2  $cache/$name" | sha256sum -c --status ||
        { echo "checksum mismatch for $name" >&2; exit 1; }
    echo "$cache/$name"
}

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

case "$PACK" in
none | base | extra) ;;
*)
    echo "FLIPPEROS_PACK must be none, base or extra (got '$PACK')" >&2
    exit 1
    ;;
esac
if [[ "$PACK" != none ]]; then
    echo "==> Adding the $PACK app pack"
    RES="$FW/applications/main/extra_resources/resources"
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    tar xzf "$(fetch_pack base "$PLUGINS_BASE_SHA256")" -C "$tmp"
    mkdir -p "$RES/apps" "$RES/apps_data"
    cp -r "$tmp"/base_pack_build/artifacts-base/. "$RES/apps/"
    cp -r "$tmp"/base_pack_build/apps_data/. "$RES/apps_data/"
    if [[ "$PACK" == extra ]]; then
        tar xzf "$(fetch_pack extra "$PLUGINS_EXTRA_SHA256")" -C "$tmp"
        cp -r "$tmp"/extra_pack_build/artifacts-extra/. "$RES/apps/"
    fi
fi

echo "==> Copying overlay and apps"
cp -r "$ROOT/firmware/overlay/." "$FW/"
cp -r "$ROOT/flipper_os" "$FW/applications_user/"
cp -r "$ROOT/patrol" "$FW/applications_user/"

cd "$FW"
if (($# == 0)); then
    set -- updater_package
fi
echo "==> ./fbt ${FBT_ARGS:-COMPACT=1 DEBUG=0} $*"
export FBT_GIT_SUBMODULE_SHALLOW=1
# shellcheck disable=SC2086
./fbt ${FBT_ARGS:-COMPACT=1 DEBUG=0} "$@"

echo "==> Done. Output: $FW/dist/"
