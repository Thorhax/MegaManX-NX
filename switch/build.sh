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

# Saber Zero's art and sound are converted from tracked donor files at build
# time. The converters run on this machine (a C compiler, and Python with
# Pillow); the cross build only compiles their output in.
SABER="$BUILD_DIR/saber-cache"
mkdir -p "$ROOT/$SABER"
if [ ! -s "$ROOT/$SABER/saber-v1.bin" ] || [ ! -s "$ROOT/$SABER/ride-zero-v1.bin" ] ||
   [ ! -s "$ROOT/$SABER/saber-sfx-v2.bin" ]; then
  (cd "$ROOT"
   for kind in saber ride; do
     out="$SABER/saber-v1.bin"; [ $kind = ride ] && out="$SABER/ride-zero-v1.bin"
     python3 -I tools/saber/convert_saber_zero.py \
       --manifest "tools/saber/${kind}_zero_manifest.json" \
       --source-dir assets/saber-zero/sprites --out "$out"
   done
   ${CC:-cc} -O2 -Isrc/saber -Itools/saber tools/saber/mmx_saber_sfx_convert.c \
     tools/saber/mmx_saber_sfx_codec.c tools/saber/third_party/stb_vorbis.c -lm \
     -o "$SABER/mmx_saber_sfx_convert"
   "$SABER/mmx_saber_sfx_convert" --source-dir assets/saber-zero/sfx \
     --out "$SABER/saber-sfx-v2.bin")
fi

docker run --rm -v "$ROOT":/work -w /work "$IMAGE" bash -c "
  set -e
  export DEVKITPRO=/opt/devkitpro
  /opt/devkitpro/portlibs/switch/bin/aarch64-none-elf-cmake -S . -B $BUILD_DIR -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_SDL_BACKEND=SDL2 -DBUILD_TESTING=OFF \
    -DMMX_SABER_PREBUILT_CACHE=/work/$SABER >/dev/null
  cmake --build $BUILD_DIR --target MegaManXSNESRecomp_nro -- -k 0
"
