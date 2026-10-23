# Storm Eagle Magic Carpet

Magic Carpet works through the original ROM's Boomerang Cutter and platform
logic. The compatibility change preserves the setup in **solo widescreen**;
there is no flight toggle or replacement movement code. Native 4:3 already
performed the grab on the upstream baseline.

## Performing the trick

Bring the dash boots and Boomerang Cutter to Storm Eagle with its Heart Tank
still available. Climb the first moving platforms, return left to the rooftop,
and throw a boomerang left to retrieve the heart. Move right and collect the
returning heart as the first platform group reappears. The collection and
platform allocation must coincide; ordinary boomerang retrieval does not grant
a carpet. Ride or wall-kick against the captured platform to gain height, then
travel right. Going above the map can lose the platform.

The setup is demonstrated in [Tiki's Carpet Tutorial](https://www.speedrun.com/mmx/guides/wse6g).
The investigation also used the owner's [Son of a Glitch reference](https://www.youtube.com/watch?v=TIgErVZGYlA)
and FractalFusion's [original demonstration and memory explanation](https://tasvideos.org/Forum/Posts/399955).
The checked-in controller sequence credits his
[January 2015 input movie](https://tasvideos.org/UserFiles/Info/20369464872915053).
The old movie's Snes9x 1.43 save format is incompatible with the current oracle;
the tests reconstruct a fresh stage entrance instead of importing that state.

## Why widescreen needed a change

The heart and the first ten moving platforms use the same sixteen-slot item
pool, `$7E:1628..1927`, with 48 bytes per slot. The returning boomerang's `$0C`
word refers to its carried item's slot. Collection frees that slot; a platform
allocated before the reference is cleared can inherit it and follow the
boomerang. The platform then retains the game's ordinary rider collision.

Widescreen previously moved the platform creation/removal switches and scanned
collectibles ahead of the native view. This changed the lift cycle and loaded
the heart into `$1808` while the ten platform slots were occupied. At collection,
the platforms reused `$1628` instead, so the stale heart reference could not
capture one. Restoring only the switch timing was insufficient.

Solo play now keeps the original platform switch coordinates and gives the
Storm Eagle heart exclusively to the native spawn pass. Its wide collectible
rescan is also suppressed. Widescreen rendering remains available; these actors
enter at their original gameplay boundaries. The extended margins show private
ROM art for the dormant platform group and uncollected Heart Tank, without
allocating guest objects. Dormant platforms hold their original starting poses;
their movement and collision begin with native initialization. Previews cover
the allocation and initialization frames, then yield to submitted real art.
When a live actor skips native sprite submission, its captured pose can still
draw in the margin. They never draw over the
native 256-pixel field, duplicate a carried tank, or restore a collected tank.
This is a visual approximation of inactive objects; it does not extend their
interaction boundaries. Co-op keeps its existing platform
and pickup behavior. This work does not qualify the trick for co-op or netplay.

## Reproducing the checks

Use the USA Rev 1 ROM; headerless SHA-256:
`b8f70a6e7fb93819f79693578887e2c11e196bdf1ac6ddc7cb924b1ad0be2d32`.
ROM data, generated code, screenshots and private save states are not committed.

With normal project dependencies and generated sources prepared:

```text
cmake -S . -B build -DMMX_STATE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target MegaManXSNESRecomp mmx_state_tests
ctest --test-dir build -R mmx_magic_carpet --output-on-failure
```

The fixtures arrange a fresh Storm Eagle entrance and acquired equipment, then
play 1,800 controller samples without writing the heart, boomerang or platforms.
They check the original heart reference becoming a platform reference, maximum
health increasing from 16 to 18, sustained flight and deterministic restore of
an active carpet. The boots must be acquired **before** entering the stage:
native initialization creates their separate sprite actor then. Granting only
the ownership bit after entry allows dashing with the lower body missing, even
in the original ROM under Snes9x. Both reproduction tools initialize the boots
before entry; the ROM-backed checks also verify a visible lower-leg pixel in
the native field and widescreen output during a dash. Native, 16:9 and 32:9 are
covered. The ROM-backed checks also require visible margin art before the heart
is allocated and across its native spawn handoff. Renderer checks cover native
pixel preservation, partial allocation, collection, carried and hidden actors,
co-op/menu/stage guards, and capture restore at different widths. The previews
derive entirely from captured RAM and ROM, with no extra simulation state.
A separate widescreen control
removes shooting from the same inputs and requires no carpet. Automated checks
use a dummy audio device and disable background replay recording.

For the independent emulator reproduction, supply a Snes9x libretro core:

```text
python tools/check_magic_carpet_oracle.py --core /path/to/snes9x_libretro.dll --rom mmx.sfc --output /path/to/private-results
```

This headless tool opens no audio device. It writes CSV traces, screenshots,
core save states, the tested core hash and a result summary. Its CSV includes
both native frame counters to expose emulator lag.

Observed with the raw demonstration inputs on 2026-10-17:

| Run | Grab sample | Samples carrying a platform |
| --- | ---: | ---: |
| Snes9x 1.63 (`b5cc765`) | 992 | 808 |
| Recompiler, native view | 992 | 601 |
| Recompiler, fixed 16:9 | 992 | 601 |
| Recompiler, fixed 32:9 | 992 | 601 |

The fixed recompiler views have identical player coordinates and carried-item
references throughout all 1,800 samples. Before the fix, 16:9 never captured a
platform with these inputs.

The tested oracle captures the platform at sample 992 and retains it through
sample 1799. The native recompiler also captures it at 992, with the same player
positions through 1194. Snes9x first repeats a gameplay frame at 1195 and repeats
66 frames through this sequence. Replaying every raw movie sample in the
recompiler therefore changes subsequent jump timing; its carpet goes above the
map and disappears at 1593. This is a successful grab and ride, not a claim of
complete hardware movie timing or a validated full-stage speedrun route.

## Upper sky rendering

The original ROM also scrambles the scenery during the high climb: the
independent Snes9x reproduction shows pieces of runway and mountains mixed
with clouds. In the recorded sequence, the cloud scroller reaches source
coordinates beyond its painted region, including negative Y, and the native
circular tilemap retains unrelated airport tiles. This is authentic visual
corruption, separate from the platform grab.

The compositor cleans up Storm Eagle's cloud phase above camera Y `$0400`.
It reconstructs the foreground from the retained stage map and repeats the
authored 256 by 256 cloud screen on both axes. This also activates the
compositor at native 4:3 width; it needs no widescreen setting. HUD, sprites,
brightness and color effects retain their usual composition. The raw renderer
oracle still exposes the original pixels with asset repairs disabled.

The repair changes presentation only. The 4:3, 16:9 and 32:9 controller traces
must remain identical to the validated grab and ride. Other stages, the
airport panorama, lower camera positions and the pause menu keep their
existing rendering paths.

## Interactive practice on Windows

After the widescreen CTest passes, run:

```powershell
./tools/play_magic_carpet.ps1 -Scene setup
./tools/play_magic_carpet.ps1 -Scene active
```

`setup` starts on the rooftop before the throw. `active` starts on an already
captured platform. The script copies the fixture into a new temporary directory
because `--resume-state` consumes its input file. The fixture originals and
the player's configuration are preserved. A private mod catalog enables fixed
16:9 without changing the player's installed mod selections. Interactive launches use normal
audio; the test's dummy audio setting is not carried into the game.
