# Mega Man X Recompiled — Nintendo Switch

A homebrew (NRO) build of this project for Atmosphère. It runs the same
recompiled game, mods (Zero, co-op, X2/X3 weapons, widescreen, password saves)
and launcher as the PC release. Bring your own ROMs: none are included.

> [!NOTE]
> **Performance.** The game runs at 60 FPS at the stock CPU clock (1020 MHz),
> including widescreen, Zero / co-op and the X2/X3 weapons. The NRO never
> changes the clock itself; overclocking only adds headroom.

## Install

1. Copy `switch/MegaManX/` from the release zip to the root of your SD card.
2. Put your own **Mega Man X (USA) (Rev 1)** ROM in `sd:/switch/MegaManX/`,
   ideally named `mmx.sfc`. Any `.sfc`/`.smc` name works; the game checks its
   SHA-256.
3. Optional: put your **Mega Man X3 (USA)** ROM (needed for Zero, co-op and the
   X3 weapons) and **Mega Man X2 (USA)** ROM (needed for the X2 weapons) in the
   same folder. Select them in the launcher's **Mods** page; **Browse** opens a
   built-in SD-card browser.
4. Start **Mega Man X Recompiled** from the Homebrew Menu. Use title-override
   mode (hold R while opening a game) for full memory.

## Controls

| Switch | SNES |
| --- | --- |
| B | Jump (SNES B) |
| Y | Shoot (SNES Y) |
| R | Dash (SNES A) |
| L / A | Previous / next weapon (SNES L / R) |
| X | SNES X |
| − / + | Select / Start |
| D-pad / left stick | D-pad |
| − + A | Save-state browser (Select + SNES R) |
| − + right stick click | Rewind |
| − + left stick click | Back to the launcher (the game restarts into it and resumes where you were) |

You can remap buttons in the launcher's **Controls** page or in
`sd:/switch/MegaManX/config.ini`. In handheld mode you can also tap the
launcher's buttons on the touchscreen.

## Widescreen

The **Widescreen** mod (16:9) is on by default for new installs. To turn it on
or off, open the launcher's **Mods** page and use **Display → Widescreen**.
If you ran an older build, your saved mod choices are kept, so enable it there
once.

## Files on the SD card

Everything lives beside the NRO in `sd:/switch/MegaManX/`:
`config.ini` (written on first run), `saves/`, `mmx-password.srm`, `mods/`,
and `logs/`. Each start moves the previous run's logs to `*.prev.log`, so a
failed start survives the next one. `mmx-switch-err.log` carries a `[perf]`
line every 600 frames: fps, `dropped` (screen refreshes that passed without a
new frame, i.e. visible stutter), mean time per stage, and `pauses` (menus,
rewind, loading). When refreshes were dropped, a second line breaks down the
slowest frame.
Include the logs when you report a problem.

If the launcher misbehaves, set `SkipLauncher = 1` in `config.ini` to boot
straight into the game.

## How the port works

The Switch host is the shared desktop host with the desktop-only pieces left
out:

- **Video:** SDL2 (devkitPro portlibs) through SDL_Renderer on Mesa/nouveau,
  in a 1280×720 full-screen window. The desktop OpenGL presenter and GLSL
  shaders are not built.
- **Launcher:** the shared Dear ImGui launcher over GLES2, drawn 1.25x with a
  high-contrast theme (`launcher_theme_high_contrast`) and touch input. Switch
  SDL allows only one window, so the in-game launcher saves a resume state and
  asks the homebrew loader (`envSetNextLoad`) to start the NRO again into the
  launcher.
- **Widescreen:** `switch/package.sh` turns the widescreen mod on with a 16:9
  view by default. The custom renderer (widescreen, Zero, X2/X3 weapons)
  copies VRAM only on scanlines where it changed, and composes rows on two
  worker threads pinned to cores 1 and 2; the main thread stays on core 0.
  `MMX_RENDER_THREADS=<n>` overrides the worker count.
- **Not included:** internet netplay, the desktop file chooser, and process
  relaunch by `posix_spawn`.
- **CPU:** the NRO runs at the clock the system gives it; it never requests
  boost mode.

The code changes are:

- `switch/` — entry shim (`switch_main.c`), CMake wiring (`switch.cmake`),
  Switch default `config.ini`, compatibility stubs, icon, build and package
  scripts.
- `switch/patches/` — changes to the `snesrecomp` and `recomp-ui`
  submodules. `switch/build.sh` applies them if they are not applied yet.
- `CMakeLists.txt` — `NINTENDO_SWITCH` branches.

## Building

You need Docker with a devkitPro image that has `switch-sdl2` and `switch-mesa`
(`devkitpro/devkita64`). Regenerating the C code also needs Rust and Python.
`switch/build.sh` converts Saber Zero's art and sounds on the build machine
first, so it also needs a C compiler and Python 3 with Pillow there.

```bash
cp "/path/to/Mega Man X (USA) (Rev 1).sfc" mmx.sfc
bash tools/regen.sh usa --no-tests     # generate src/gen from your ROM
bash switch/build.sh                   # -> build-switch/MegaManX.nro
bash switch/package.sh                 # -> dist/SD_CARD + dist/MegaManX-NX.zip
```

`DKP_IMAGE=<image>` selects a different Docker image.
To build without the launcher (the game boots straight from `mmx.sfc`),
configure with `-DMMX_SWITCH_LAUNCHER=OFF`.
