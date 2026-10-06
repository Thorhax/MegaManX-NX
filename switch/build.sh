#!/usr/bin/env bash
# Build the Nintendo Switch NRO inside the devkitPro Docker image.
#
#   bash switch/build.sh            # configure (once) + build
#   bash switch/build.sh --clean    # wipe build-switch first
#
# Generated C (src/gen) must already exist: run tools/regen.sh with your ROM.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${DKP_IMAGE:-devkitpro/devkita64:latest}"
BUILD_DIR="${BUILD_DIR:-build-switch}"
if [ "${1:-}" = "--clean" ]; then rm -rf "$ROOT/$BUILD_DIR"; fi

# The Switch support in the snesrecomp and recomp-ui submodules is carried as
# patches against their pinned commits. Apply each once (idempotent).
for sub in snesrecomp recomp-ui; do
  patch="$ROOT/switch/patches/$sub-switch.patch"
  if git -C "$ROOT/$sub" apply --check "$patch" 2>/dev/null; then
    git -C "$ROOT/$sub" apply "$patch" && echo "applied $sub-switch.patch"
  elif ! git -C "$ROOT/$sub" apply --reverse --check "$patch" 2>/dev/null; then
    echo "switch/build.sh: $sub-switch.patch does not apply to $sub" >&2
    exit 1
  fi
done

docker run --rm -v "$ROOT":/work -w /work "$IMAGE" bash -c "
  set -e
  export DEVKITPRO=/opt/devkitpro
  if [ ! -f $BUILD_DIR/build.ninja ]; then
    /opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake -S . -B $BUILD_DIR -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_SDL_BACKEND=SDL2 -DBUILD_TESTING=OFF
  fi
  cmake --build $BUILD_DIR --target MegaManXSNESRecomp_nro -- -k 0
"
