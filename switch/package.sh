#!/usr/bin/env bash
# Stage the SD-card layout from build-switch/:
#   dist/SD_CARD/switch/MegaManX/{MegaManX.nro, assets/, mods/, logging.ini}
# config.ini is NOT shipped: the NRO writes its Switch defaults on first run,
# and an existing player copy is never overwritten.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/${BUILD_DIR:-build-switch}"
OUT="$ROOT/dist/SD_CARD/switch/MegaManX"
rm -rf "$ROOT/dist"
mkdir -p "$OUT"
cp "$BUILD/MegaManX.nro" "$OUT/"
cp -r "$BUILD/assets" "$BUILD/mods" "$OUT/"
cp "$ROOT/logging.ini" "$OUT/"
# Switch default: true 16:9 widescreen on. A player's saved choice
# (mods/preloaded/state.toml) still wins; this only sets the first-run default.
ws="$OUT/mods/preloaded/packages/megaman-x.enhancement.widescreen/1.0.0/manifest.toml"
sed -i -e 's/^default_enabled = false$/default_enabled = true/' \
       -e 's/^default = "adaptive"$/default = "16:9"/' "$ws"
grep -q '^default_enabled = true$' "$ws" && grep -q '^default = "16:9"$' "$ws" || {
  echo "package.sh: could not set the widescreen defaults in $ws" >&2; exit 1; }
cat > "$OUT/PUT_YOUR_ROM_HERE.txt" <<'TXT'
Copy your own Mega Man X (USA) (Rev 1) ROM here as mmx.sfc
(any .sfc/.smc name works; it is verified by SHA-256).

Optional, for the Zero / co-op / X3 weapon mods: your Mega Man X3 (USA) ROM.
Optional, for the X2 weapon mod: your Mega Man X2 (USA) ROM.
Select them in the launcher's Mods page (Browse opens an SD-card browser).
TXT
python3 -I -c "import shutil,sys; shutil.make_archive(sys.argv[1], 'zip', sys.argv[2], 'switch')" "$ROOT/dist/MegaManX-NX" "$ROOT/dist/SD_CARD"
echo "staged: $OUT"
ls -la "$OUT"
