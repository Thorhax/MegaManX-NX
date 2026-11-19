# Mega Man X Recompiled for Nintendo Switch (MegaManX-NX)

**Version:** 1.3.0
**Author (Switch port):** Thorhax
**Target:** Nintendo Switch homebrew (Atmosphère / Horizon OS)

A native Nintendo Switch port of [Mega Man X Recompiled](https://github.com/mstan/MegaManXSNESRecomp)
by mstan: a static recompilation of the SNES game *Mega Man X* (USA, Rev 1) to C.
The launcher, mods, password saves, save states and rewind all come with it.

> [!NOTE]
> **Performance.** The game runs at 60 FPS at the stock CPU clock (1020 MHz),
> including widescreen, Zero / co-op and the X2/X3 weapons. The NRO never
> changes the clock itself; overclocking only adds headroom.

## Features

- **60 FPS** with frame pacing locked to the Switch display.
- **16:9 widescreen** (on by default) with an anchored HUD.
- **Playable Zero** (X3, Modern or **Saber Zero** behavior), **X / Zero co-op**, and the **X2 / X3 boss weapons**, using your own X2/X3 ROMs.
- **X2 / X3 armor** (experimental): X wears the donor game's armor. X1's capsules unlock the matching parts, and the Hadouken capsule adds X3's Gold Armor.
- **Player 2 Learner Mode** for co-op: Player 2 can't lose health or die, and the camera follows Player 1.
- **Damage multipliers**: scale X's damage dealt and the damage the player takes, independently.
- **Boss Rush**: an endless two-at-a-time Maverick gauntlet with a high score, picked from the title menu.
- **The full launcher** on the console: Mods, Controls and Settings. It works with a controller or the touchscreen, and has a built-in SD-card file browser.
- **Password saves**, a **save-state browser** and **rewind**.
- A **developer menu** for testing (− + X): pause the game, warp to any stage
  and checkpoint, give or remove weapons, Sub Tanks, Heart Tanks and armor,
  infinite health, and make defeated Mavericks and collected items come back.
- **Bring your own ROM.** The NRO contains no ROM data, graphics or music. It reads them from your own ROM.

## Installation

1. Copy the `switch/` folder from the release zip to the root of your SD card,
   so that the NRO is at `sdmc:/switch/MegaManX/MegaManX.nro`.
2. Put your own **Mega Man X (USA) (Rev 1)** ROM in `sdmc:/switch/MegaManX/`,
   ideally named `mmx.sfc`. Any `.sfc` / `.smc` name works, because the game
   checks the ROM's SHA-256.
3. Optional, for Zero, co-op and the X3 weapons or armor: your **Mega Man X3 (USA)** ROM.
   For the X2 weapons or armor: your **Mega Man X2 (USA)** ROM. Put them in the same
   folder and select them in the launcher's **Mods** page.
4. Start **Mega Man X Recompiled** from the Homebrew Menu. A short intro plays
   first; any button skips it after 3 seconds. Use title-override
   mode (hold R while opening a game) so the game gets full memory.

## Controls

| Switch | Action |
| --- | --- |
| B | Jump |
| Y | Shoot |
| R | Dash |
| L / A | Previous / next weapon |
| + / − | Start / Select |
| D-pad / left stick | Move |
| − + A | Save-state browser |
| − + right stick click | Rewind |
| − + left stick click | Back to the launcher (the game resumes where you were) |
| − + X | Developer menu (the game stays paused while it is open) |

Remap the controls in the launcher's **Controls** page, or in `sdmc:/switch/MegaManX/config.ini`.

## Troubleshooting

All files live beside the NRO: `config.ini`, `saves/`, `mods/` and `logs/`.
If something goes wrong, the error screen shows the last log lines.
`logs/mmx-switch-err.log` has a `[perf]` line about every 10 seconds, and the
previous run's logs are kept as `*.prev.log`.

## Building from source

See [switch/README.md](switch/README.md). In short: regenerate the C code from
your own ROM (`tools/regen.sh`), then run `switch/build.sh` (devkitPro Docker)
and `switch/package.sh`. The upstream project's own documentation is in
[README.upstream.md](README.upstream.md).

## Credits

- **mstan**: [Mega Man X Recompiled](https://github.com/mstan/MegaManXSNESRecomp),
  the recompilation, its mods (Zero, co-op, X2/X3 weapons and armor, widescreen, password
  saves) and the launcher integration. This port is built on it.
- **RaphaelAzev**: Saber Zero. Its sprites and sounds come from the Zashiko
  Mod, included upstream with permission (see `assets/saber-zero/CREDITS.md`).
- **RetroPortingToolKit**: the [snesrecomp](https://github.com/RetroPortingToolKit/snesrecomp)
  framework and the [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui) launcher.
- Third-party components keep their own licenses. See `third_party/`,
  `snesrecomp/THIRD_PARTY_ATTRIBUTION.md` and the submodules.
- **Thorhax**: the Nintendo Switch port and its startup intro.
- [pl_mpeg](https://github.com/phoboslab/pl_mpeg) by Dominic Szablewski (MIT) decodes the intro video.

## License

The project, including this port, is licensed under
**PolyForm Noncommercial 1.0.0** (see [LICENSE](LICENSE)), as inherited from
the upstream project. Commercial use is not permitted.

## Disclaimer

This is an unofficial fan project, not affiliated with or endorsed by Capcom
or Nintendo. *Mega Man X* is a trademark of Capcom. No ROMs or game assets are
included or distributed. The NRO contains the game's program code, recompiled
to C and compiled for the Switch; everything else is read from the ROM you
supply, which must be your own legally obtained copy. The NRO icon is made from the game's North American box art, which is
already part of the upstream project. It belongs to Capcom and Nintendo.
