# Accuracy-preserving performance burndown

Campaign: 2026-10-22. Tracker: `beads-9nma`. Title branch:
`perf/raster-snapshot-burndown`; framework branch:
`perf/raster-snapshot-storage`.

Keep the general SNES implementation as the correctness reference. A title may
specialize work when it preserves the applicable game and hardware contract.
Promote reusable mechanisms into snesrecomp without moving title-specific
spawning, object allocation, artwork, cameras, or scene rules into the framework.

AOT CPU code and modeled devices can coexist in a native host application.
MMX also deliberately interprets its small original coroutine scheduler while
dispatching task bodies into AOT code; the interpreter is not exclusively a
cold fallback here. Removing modeled state is not itself a performance result.
The useful test is whether a replacement preserves observable behavior while
reducing measured work. Universal compatibility and title-specific fast paths
fit that same framework.

## Baseline and scope

- MMX source: `8a1806c1fa2ce53a9a82f22e3ff0ef1c9825579c`.
- Framework: the title's recorded pin,
  `7a67b85724a8b3be5b592c5a7578e35f739c81fb`.
- Use isolated worktrees; preserve unrelated owner changes.
- Keep generated ROM code, ROMs, captures and measurements private and ignored.
- Record compiler, dependency identities, configuration, exact commands, route,
  output hashes, elapsed times and memory/copy counts with each experiment.
- Desktop results do not qualify lower-end hardware or another OS/architecture.
- The owner's ideal target is the original Xbox. Prioritize its memory/cache
  budget and portable scalar code; a Windows i386 compile is not Xbox runtime QA.
- Old save/capture compatibility is not an acceptance requirement. Current-build
  save/load, rewind and rollback remain requirements; native game progress is
  never discarded.

## Sequential experiments

Owner's validation sequence (2026-10-22): keep MMX as the proving ground for
the remaining candidates. Attribute, implement and validate each there first,
including correctness, current-build restore/replay, rollback/netplay and
headroom. Retain only supported wins. Do not switch titles after every trial.
When the MMX candidate set is settled, combine all applicable retained changes
into one integration build for Super Mario World and one for Super Metroid.
Run automated validation on each combined build before handing it to the owner
for one general gameplay/feel playtest per game. Final approval belongs to the
owner. Rejected probes are excluded; title-specific code is not forced into
other games. Follow-up ecosystem discovery may now begin in separate windows
under the owner's updated instruction below.
The owner will do the final combined gameplay/correctness run after the changes
are together, rather than receiving a new handoff after each small trial.
If local contention prevents useful measurement, `F:/NAS` is the owner's offered
entry point for SSH access to an older fourth-generation system. Inspect its
connection instructions before use; no remote run has been attempted here.

| ID | Tracker | Experiment | Status |
|---|---|---|---|
| P0 | `beads-9wlm` | Build a coherent reference; capture correctness and performance baselines | Complete |
| P1 | `beads-4lwp` | Share exact immutable VRAM snapshots across unchanged scanlines | Retained; local qualification passes |
| P2 | `beads-3w9m` | Attribute and evaluate palette/OAM reuse and tile-row decoding | Screened; these implementations rejected |
| P3 | `beads-mija` | Establish pixel-only bypass eligibility and shared-renderer boundaries | Audit complete; bypass deferred |
| P4 | `beads-kneo` | Qualify retained changes for rollback/netplay and portable consumers | Local checks pass; external targets unverified |
| P5 | `beads-wwgh` | Attribute remaining MMX frame costs and trial substantial exact optimizations | Complete; draw-scoped stage-map reuse retained |
| P6 | `beads-akjp` | Integrate applicable retained changes into combined SMW/SM builds, automate qualification, then owner approval | Automated qualification/NAS timings pass; owner approved all three; publishing qualified revisions |

An experiment ends as accepted, rejected, or explicitly unverified. A checklist
entry is not a performance promise. Do not retain neutral/noisy work merely to
complete an item. After two sound neutral/negative probes, stop that candidate.

## Acceptance gates

1. Exact full-copy reference versus candidate raster bytes and rendered pixels:
   constant memory; first/last-word writes; changes on consecutive lines; ABA
   changes; every line changing; palette/OAM/register changes; frame restart;
   capture round-trip; invalid/truncated input.
2. Render original captured frames repeatedly at native, wide and ultrawide
   widths, changing view order and warming caches differently. Rendering must
   never change guest state or captured source data.
3. ROM-backed Magic Carpet in native, 16:9 and 32:9, plus the no-shot negative
   control. Preserve actual heart collection, slot reuse, ride/altitude and
   deterministic restore; do not set success flags. The existing console-lag
   difference is a documented limitation, not a newly passing timing test.
4. Current-build state save/load, rewind and corrected-input resimulation.
   Present zero, once or repeatedly between identical simulation steps and
   compare simulation snapshots/digest partitions. Discard derived renderer
   history after discontinuities and reconstruct from restored authoritative
   state. Never serialize host pointers or cache warmth as simulation state.
5. Existing custom-renderer, co-op, independent-camera, menu, color/window,
   sprite-priority and capture checks. Keep the native framebuffer fallback.
6. Audio-enabled deterministic routes with muted host output and matching
   simulation/audio results. Audio-disabled throughput is reported separately.
7. Interleaved/order-balanced A/B runs with warmups, repeated samples and an
   A/A noise control. Include capture/draw attribution, copied bytes, resident
   memory or storage footprint and executable/code-size deltas.
8. Build the portable implementation and rollback-linked title. Two local
   processes exercise actual delay/rollback episodes if available; report
   missing network, hardware or cross-OS coverage explicitly. Preserve strict
   game/build/config identity checks.

## Ownership and abstraction

The snapshot store, exact tile/row decode, generic PPU composition and safe bus
or dispatch primitives can belong to snesrecomp. MMX's source-frame orchestration,
Storm Eagle previews, allocator/spawn policy, private sprite assets and camera
projection stay in MMX. Super Metroid has the same 64 KiB-per-line capture pattern
and is a second consumer for the storage contract. F-Zero currently takes one
VRAM image per source frame and uses Mode 7-specific presentation; it should not
be forced through MMX's compositor.

Derived presentation optimizations need no rbengine/recomp-net wire or scheduler
change. Faster simulation can help catch-up, but that is measured separately
from drawing. Guest-affecting CPU/device optimizations need deterministic replay
and qualified simulation profiles, with actual capabilities agreed before frame
0 under the existing host-optimization policy. No live backend renegotiation.

## Evidence and results

Raw private evidence is under ignored `build/performance/` and
`F:/Temp/Codex/snesrecomp/native-performance-20261022`. ROMs, generated sources,
save files and images are deliberately absent from Git.

### P0: reference

Regenerated USA code with the Rust analyzer: 242 roots, 4,707 AOT variants,
78 LLE variants, 16 banks. GCC 15.2, Release, SDL3; cached host-frame staging,
small audio history, production diagnostics, rollback linked. Dependencies:

- recomp-net: `8c76fb643339144a3a84d5ba8f5defe6250dcd26`.
- retcomm-rbengine: `2a03e73693acee0fb78076ea058642c931bef12e`.
- recomp-ui: `7c635c6b366a6e0d5d3c913a7fce6ec04dd9f256`.

Archived the original app, state-test and capture-renderer executables before
editing. Initial custom-renderer and four Magic Carpet checks passed.

### P1: immutable raster storage

The shared header owns stable immutable byte images, uses exact comparison for
consecutive equality, and reuses its allocation high-water across frames. MMX
owns scanline-to-image references. Its hardware loop can also reuse an image
when the existing VRAM port-write count proves no intervening writes. Every
frame starts uncertain; every IRQ forces comparison; a line callback disables
counter reuse for the remaining raster. Trace builds and direct/mock captures
always compare bytes. No PPU state layout or device write path changed.

The partner's writable CHR preview has separate backing. Capture files use
build-specific versions 24/25 with image indices; file pointers never become
usable host pointers. Failure rejects the custom frame and retains the stock
presentation path. No guest work is skipped.

In the final Magic Carpet frame, VRAM capture fell from 224 copies (14,680,064
bytes) to one copy (65,536 bytes) plus one conservative comparison. Frame
metadata is 662,296 bytes. Static BSS fell by 14,678,176 bytes; executable text
grew by 664 bytes. Heap images are allocated as needed, up to 224 versions for
a fully changing raster. This is a workload-dependent memory saving, not a
fixed 14 MiB reduction in every scene; allocations retain their high-water.

The comparator-only prototype had noisy/negative route results. A custom word
comparison was slower than libc `memcmp` and was removed. Only the scoped
counter implementation below is retained.

Five balanced pairs per aspect, warmups, and two same-executable pairs produced:

| Headless Magic Carpet route | Median CPU throughput gain | Equivalent CPU-time reduction | Median wall throughput gain |
|---|---:|---:|---:|
| Native | 14.9% | 13.0% | 8.4% |
| 16:9 | 27.5% | 21.6% | 41.7% |
| 32:9 | 48.0% | 32.4% | 61.4% |

Same-executable CPU differences were -4.9% and -3.5%; wall differences -9.8%
and -6.2%. Other host jobs were active. Native included one slower CPU pair;
all wide/ultrawide CPU pairs improved. Treat these as route-specific desktop
evidence, not whole-game FPS, presentation/audio-delivery timing or Xbox speed.
Every run had matching guest CSV and setup/active save SHA-256 hashes. Details:
`build/performance/route-ab-p1-final/results.json`.

Nine original-versus-candidate BMP comparisons also matched byte-for-byte:
Storm Eagle samples 60, 900 and 1500, each at native, 16:9 and 32:9. Each
executable loaded its own build's capture format; no old-capture migration was
added. Evidence: `build/performance/route-pixels-p1/results.json`.

### P2: rejected screening candidates

A 64-entry tile-row cache used 2 KiB including its expansion table. Exhaustive
low-plane values and 2/4bpp checks matched scalar decoding. Balanced spatial
and random-access probes were slower with the cache, including repeated runs
with hit/miss instrumentation removed. Reject this cache design; do not infer
that every possible decoder or a different host would behave the same way.

Sharing a combined 1,056-byte CGRAM/OAM image could save at most 235,488 bytes
in a constant frame. Two balanced probes were slower than copying, both for
constant and changing images. Retain simple copies for now. Revisit this as a
memory tradeoff only if Xbox measurements justify it. Synthetic evidence is
in scratch `p2-probes.json` and `p2-probes-uninstrumented.json`.

### P3: bypass and abstraction decision

Do not disable `MmxDrawPpuFrame`: HDMA and IRQs execute within its loop.
`ppu_runLine` also advances frame/mosaic/OAM history and sprite evaluation;
the latter sets the overflow flags read through $213E. MMX consumes stock
pixels for non-Mode-1 lines and scenes outside its supported custom paths.
The stock-frame accessor, capture utility and native-pixel checks are additional
consumers. A pixel-only bypass is therefore a separate implementation, not a
safe switch in this change.

Before attempting it, specify per-line custom-pixel eligibility, retain
device/status behavior, and supply valid stock pixels to fallback, debugging
and comparison consumers, either eagerly or by correct materialization. Then
qualify actual scene/mode transitions and rollback. This bypass remains
unimplemented and unqualified. Do not describe the SNES optimization work as
exhausted or start the next system on that assumption.

Keep MMX's renderer title-local. The snapshot mechanism is shared now; Super
Metroid's matching per-line capture is a concrete second-consumer candidate,
not yet an integrated/tested consumer. F-Zero's one-image Mode 7 path does not
justify a common MMX scene compositor.

### P4: correctness and runtime qualification

Optimized, forced-full-copy and trace renderer suites pass, including live
memory edits, consecutive changes, ABA, counter wrap, raw IRQ edits, sticky
callback fallback, immutable views, capture round-trip and invalid/truncated
input. Generic store tests include allocation failures and an independent
full-copy oracle. A Windows i386 object also compiles; no Xbox SDK/runtime or
other-OS build has been qualified.

All four Magic Carpet cases pass, including the no-shot negative control.
Repeated native/wide/ultrawide rendering leaves the full rollback snapshot
byte-identical and advances no guest clocks. Current-build memory/file saves,
rewind and fresh-process replay pass. Original native save data is untouched.

Full CTest initially passed 27 checks, skipped two asset-dependent saber checks,
and failed the existing trace-writer queue-saturation assertion. That unchanged
writer test passed twice on rerun. Keep this initial failure in the record.
Reset in both LLE/HLE modes and Boss Rush reports also passed.
After adding the fresh-fixture test mode, all eight focused renderer, Magic
Carpet and shared-store gates pass again (19.53 seconds). No production change
was made after the qualified snapshot implementation; the fixture switch is
test-executable initialization only. Timing JSON records the exact measured
executable hashes; scratch `final-manifest.json` records the final sources and
app/test executable hashes.

The general state-test slot was unsuitable as a grounded gameplay fixture;
it failed partner placement before simulation/network qualification. The new
test-only `MMX_NETPLAY_FIXTURE_OUT` route instead boots normally and waits for
real Highway gameplay, a living player and standing ground contact. No guest
success flags or owner saves are edited to construct this fixture.

Using that fresh fixture, both real lobby/runtime peer tests pass 480 gameplay
frames with a rollback episode and replay tick on each peer, no digest fork,
both players alive, matching final positions/health, and imported X2/X3 weapon
contexts. Independent views end at X=128/478 with different actual rendered
views; unified views end at X=128/352 within the shared separation limit.

Two actual desktop hosts pass 180-frame rollback and delay sessions with audio
enabled and the dummy output device. A further 480-frame audio-enabled rollback
test passes save/load/cancel, deliberate guest-state and saved-slot mismatch
repair, new epoch agreement, and identical 326,777-byte peer slots. No offline
autosave pollution. These cold-boot/menu checks supplement the actual gameplay
pair; they do not constitute an audio-enabled whole-campaign audit.

No rbengine/recomp-net code or protocol changed. Local tests used the same
candidate on both peers and retained existing compatibility checks. This does
not claim cross-build, cross-OS or Xbox interoperability. Capture allocation
history and host pointers remain outside authoritative snapshots/digests.

The scalar helper compiles with `-m32 -march=i686 -mno-sse -mno-sse2 -mno-mmx
-mno-avx` to a PE-i386 object. That is not an Xbox executable. No nxdk tools
were found on this process's PATH and no original-Xbox build target was found
in the inspected project build files. Xbox needs a separate target/dependency
qualification: [nxdk](https://github.com/XboxDev/nxdk) documents original-Xbox
support and SDL2, while [SDL3's GDK backend](https://wiki.libsdl.org/SDL3/README-gdk)
documents Xbox One/Series. Do not mistake that SDL3 platform entry for a
qualified original-Xbox backend. No SDK was installed or hardware run attempted.

### Reproduction and remaining gates

Use a private working/output directory and non-C: TEMP/TMP, application data,
cache and log overrides for every command. Keep background output muted. Build
with `MMX_STATE_TESTS=ON`; the full-copy build option is
`MMX_RASTER_FULL_COPY=ON`. The small shared test directory is also standalone
CMake, independent of ROM generation and desktop dependencies.

```text
ctest --test-dir build/performance --parallel 1 --output-on-failure
python tools/benchmark_raster_route.py --reference <original-state-tests.exe> --candidate <candidate-state-tests.exe> --rom <mmx.sfc> --inputs tests/data/magic_carpet_inputs.csv --output <new-private-directory> --pairs 5
```

Run `mmx_state_tests <rom>` from an empty directory with
`MMX_NETPLAY_FIXTURE_OUT=<private-highway.sav>` to create the current-build
fixture. Then run `tools/test_netplay_pair.py` with both owned source ROMs,
that fixture, and `--independent-views` or `--unified-views`; each test needs its
own output directory. `tools/test_netplay_desktop.py --audio` covers real desktop
hosts; add `--delay-sync` or `--savestate-menu --force-mismatch` for the other
checked modes. The tools' `--help` lists their required file arguments.

The first tranche is locally qualified. Remaining work before claiming broad
SNES/target qualification:

1. Publish/review the shared helper, then update the title's gitlink to a
   reachable framework commit. Current changes remain isolated and unmerged.
2. Integrate/qualify a second consumer, such as Super Metroid, under that
   consumer's actual capture/save/view contracts.
3. Establish an original-Xbox executable and host dependency path, then measure
   the full code, heap, rollback/rewind, graphics and audio memory budget and
   frame costs there. Desktop x64 results do not establish feasibility or speed.
4. If pursuing pixel-only bypass, implement the explicit eligibility and stock
   framebuffer contract above and audit mode/scene transitions. No bypass has
   passed that gate yet.
5. Cross-OS/CPU peer qualification needs actual target builds and matched
   simulation results; local two-process sessions cannot substitute for it.

### P5: compositor attribution and stage-map reuse

The new `MMX_FRAME_COST_TEST` harness brackets guest execution, raster capture,
composition and rollback saves. Each clocked sequence starts from the same
state as ordinary `frame()` execution and must finish with identical full
rollback bytes and sampled pixels. `tools/attribute_frame_costs.py` runs Highway
idle, Storm setup and active Magic Carpet at native, 16:9 and 32:9 in private
process profiles. It records process CPU time separately: the four stage
measurements are wall time and include preemption.

Two initial attribution runs used 22.25 and 22.09 CPU seconds for the complete
matrix. In the wide cases, composition accounted for about 83–91% of measured
stage time. This makes exact compositor work removal a stronger next candidate
than skipping stock pixels or changing CPU bus bookkeeping. These results do
not make the latter opportunities impossible or establish full-game FPS.

The stage-map trial reuses the last 8-by-8 map-cell lookup separately for each
background layer. That lookup resolves captured RAM tables and ROM metatiles;
it ignores the low three coordinate bits. It is more expensive than the small
bit-row decoder tested and rejected in P2. Only its result is reused: pixel
decoding, palette changes, priority, windows and composition still run normally.
Keys reset at every draw, so a changed view, restored capture or an edit to the
same external ROM pointer cannot carry a prior result into another draw.
Failed lookups retain their reference behavior. No live guest memory is cached.

This is MMX-specific map policy and stays in MMX. It needs no new shared PPU,
rbengine, capability profile or wire format. `MMX_STAGE_TILE_REUSE=OFF` retains
the portable reference; trace builds and reference renderer suites disable it.
The state-test binary adds 384 text bytes and 32 BSS bytes on this x64 build,
with no heap allocation. A four-quadrant map test checks every row at changing
widths and checks edits to the same ROM pointer between draws. All eight focused
renderer/reference/trace, Magic Carpet and shared-snapshot checks pass.

Retained after five balanced A/B pairs over the complete three-scene,
three-aspect matrix. Median process CPU-time reduction is about 4.9%
(about 5.1% more throughput), relative to P1 with stage-map reuse disabled.
Two quieter confirmation pairs agree at about 4.8–4.9% time reduction;
the third is a large scheduling/frequency outlier and is not the estimate.
Reference A/A throughput variation was about 2.1%. All nine sampled pixel
hashes match across binaries and every run passes its control-state comparison.
These percentages describe this headless rendering workload; do not add them
to P1's differently weighted raster-route percentages or promise full-game FPS.

Current-build memory/file save/load, rewind and fresh-process replay pass.
Actual gameplay peers pass independent and unified view tests, with real
rollback/replay. Audio-enabled desktop rollback, delay sync and forced
state/slot repair pass with muted output and no offline autosave pollution.
Nine ROM-backed captured-frame BMPs (three Storm route times by three aspects)
match the original pre-P1 renderer byte-for-byte. The cache stays outside
authoritative state and no rbengine/recomp-net changes were necessary.

Evidence lives under
`G:/Temp/Codex/snesrecomp/mmx-major-costs-20261022/`; the `attribution-p1`,
`map-cache-ab`, `map-cache-confirm`, `map-cache-regression` and `map-cache-pixels`
directories preserve separate reports.
The MMX candidate set is settled. Applicable snapshot changes have now been
integrated into isolated SMW and Super Metroid worktrees on G:.

### P6: combined SMW and Super Metroid candidates

The owner's original checkouts remain untouched. SMW is based on
`55628231b1fe683fe96062ce5a7f322ab05ed8b3` with engine `8dc869c`; Super
Metroid is based on `fe37c5f9b01ac87c4d44b08fd46d40638639bc54` with engine
`bb37c87`. Isolated title, engine and UI worktrees and their builds live under
`G:/Temp/Codex/snesrecomp/snes-bundle-20261022/`. All three consumers use the
same exact snapshot helper. MMX's map lookup specialization remains MMX-only.

The newer engine pins no longer have MMX's existing VRAM write counter. Their
initial exact-compare implementation passed correctness checks and saved
static storage, but its CPU measurements were mixed. It was not accepted as
a general CPU win. Both newer pins now have an optional, default-off
`SNESRECOMP_PPU_CAPTURE_REVISION` signal enabled by each adopting title's build.
The derived revision is outside guest snapshots. Actual VRAM port writes,
native word writes and native text updates advance it. The title-owned raster
compares its first line and after IRQ/native uncertainty; callbacks and direct
callers retain exact comparison. Palette, OAM and register values remain
independent per line. No rbengine or recomp-net implementation/wire changes
were needed. This does not qualify different builds or external CPU/OS peers.

The real PPU device-port revision test passes enabled and disabled, including
read-only operations, reset and counter wrap. SMW renderer tests pass tracked,
byte-compare, full-copy and trace-on configurations. SM passes all 11 CTests,
including the full-copy renderer and real-PPU revision test; its separate
byte-compare and trace-on renderer checks pass too. The final test-only oracle
edit was rebuilt and both affected SM renderer suites passed again.

SMW's final original/candidate routes run 4000 audio-enabled simulations at
4:3, 16:9 and 32:9. All 21 BMPs, draw lists, sprite telemetry and every common
frame telemetry row match. Each tested raster uses one 64KiB VRAM image,
replacing 224 copies (14MiB); IRQ fences require at most one exact comparison
per frame on this route. Ordinary file load checks compare the documented
functional partitions because file loads deliberately re-anchor chip clocks;
they are not a full-chip rollback equality claim. The invalid full-byte
file-replay expectation was closed as `beads-3s7n`, not treated as an engine
regression or used to weaken rollback checks.

SM stock/wide/120Hz runs preserve their per-simulation trace at 3200 frames;
the final 120Hz run produced 6424 presentations. Nine fixed-source artifacts
match the original across binaries. Independent live interpolated final BMPs
have uncontrolled phase and are excluded from that comparison. The Ceres
diagnostic positions escape prerequisites and reaches adjacent guest-produced
changing Mode-7 matrices; it does not validate natural escape completion.
Its 11000-frame stock/wide traces match. Original/new capture utilities render
that previous/current pair identically at alpha 0, 0.5 and 1 for 4:3, 16:9 and
32:9 (nine exact BMP pairs with active custom Mode-7). Sampled Landing Site and
Ceres captures each contain one consecutive VRAM version. Both runtime frame
stores retain independent ownership for interpolation.

The copied owner packages pass audio-emulating, speaker-muted startup checks.
SMW's actual Mods-selected adaptive Fit/screen-based-spawn package reproduces
the qualified 16:9 gameplay BMP exactly. SM's packaged Fit settings, normal
sprite configuration and enabled rewind also match the original for the
gameplay BMP, full source capture and 3200 simulation trace rows. The latter
route logs the same pre-existing unresolved-abandon warning at `$88D865`,
frame 2659, in original and candidate. This campaign establishes optimization
invariance there, not correctness of that unresolved dispatch.

Static BSS reductions versus originals are 14,678,176 bytes for SMW and
29,356,416 bytes for SM; text grows 1216 and 2856 bytes respectively. The
snapshot heap retains each store's version highwater, bounded by 224 images
per frame store. These are static storage savings, not fixed process RSS
reductions; rewind, SDL and other existing allocations remain separate.

The local Ryzen host became heavily contended, so balanced final CPU probes
use the owner's offered NAS via its existing SSH configuration: Windows 10,
Intel i5-4670, unchanged x64 GCC 15.2 Release binaries, audio emulation on,
dummy presentation/output devices. All remote bulk data/profile/cache/temp
paths are under `G:/Temp/Codex/snesrecomp/snes-perf-20261022/`; no services or
machine-wide settings are changed. The remote driver checks exact state
artifacts, warms both binaries, runs an A/A control and three balanced A/B
pairs per aspect. SMW's completed median CPU-time savings are 39.35% at 4:3,
21.14% at 16:9 and 15.93% at 32:9; its A/A difference is -0.047%. This is the
whole headless natural-input route including menu, YI2, death and map return.
SM's completed boot/attract-to-Landing-Site matrix saves a median 23.01% CPU
time at 16:9 and 22.96% at 32:9. Stock 4:3 has no qualified gain: its median
uses 0.22% more CPU, with paired time-savings samples +0.22%, -2.26%, -0.22%.
The SM A/A control has identical measured process CPU time. SMW's 4:3 case
keeps its adaptive renderer enabled; SM's 4:3 case uses the stock renderer.
All 44 runs pass their exact artifact checks, and their recorded binary
identities match local originals/candidates. Results and host metadata are
mirrored locally under `final-performance-nas`; full run artifacts remain
on the NAS. Neither route establishes whole-campaign FPS or original-Xbox
runtime performance. These percentages are time reductions; they must not
be added to MMX's differently weighted earlier workload percentages.

Detailed evidence: `smw-tracked-qualified`, `sm-tracked-qualified`,
`sm-ceres-tracked`, `sm-final-exact`, `snapshot-stats.json`, `playtest-smoke`
and `sm-fit-package-qualified` under the G: bundle root. Final packages are
under `playtests/`, with executable/ROM/DLL identities in `manifest.json`.
They have private progress, normal window/audio launchers, adaptive Fit and
SMW screen-based spawning. The earlier owner MMX process/folder is preserved.
Owner final feedback after launching all three combined candidates:
"all 3 pass." This approves the tested candidate set for general gameplay/
correctness integration; it supplements the automated evidence without
establishing full-campaign or Xbox qualification. The launcher template's
APP marker initially also replaced APPDATA variable names. Replacement now
matches the quoted marker only; all three corrected launchers were executed
and their actual window handles verified before this owner approval.

The exact qualified engine revisions are published as `bb156e1` (MMX's `7a67b85` base),
`4c9d7b0` (SMW's `8dc869c` base), and `53624eff` (SM's `bb37c87` base).
The game pins may now reference these reachable revisions. Current upstream
history has advanced since the candidate bases: MMX main still pins engine
`7a67b85`, SMW main pins `fa018f3` and has native co-op renderer changes, and
SM main pins `3cbd954` with newer runtime/release changes. Preserve those
upstream improvements during any later main-branch integration; the owner's
choice of landing target was requested while qualified commits were prepared.
Launch links: `playtests/SuperMarioWorld/Play.cmd`,
`playtests/SuperMetroid/Play.cmd`, and optional updated
`playtests/MegaManX/Play.cmd`. `playtests/RESULTS.txt` records scope and limits.
Central Beads updates are recorded locally; defer the next expensive sync
until competing ecosystem builds are quiet, then sync before integration.

### Current-main integration and Boss Rush follow-up

The owner selected current-main integration and automated requalification.
This supersedes the pending target choice above. The original approved
packages, progress and exact revisions remain preserved; current-main
integration uses separate worktrees in the private bundle's `current-main`.
Framework PR #209 landed the shared helper as merge `2c311b0`; all seven
active Windows/Linux/macOS/Lua CI checks passed. The helper alone is also
backported atop the current title pins: MMX `bb156e1`, SMW `4c5c7dc`, and
SM `7b8cc25`. Modern SMW/SM already provide `Ppu.vramWriteCount`, so their
new integration reuses it rather than adding a duplicate PPU counter.

Current-main qualification results (2026-10-22):

- MMX starts at main `99450ef`, preserving its newer Zero and camera fixes.
  GCC Release with recomp-net/rbengine and ICE builds successfully. All 31
  registered CTests have no failures (29 pass, two optional donor-asset
  checks skip). Magic Carpet passes at all three aspects and the no-shot
  negative control. Nine current-main full-copy/optimized Magic Carpet BMP
  pairs match exactly. Independent and unified gameplay peers agree after
  real rollback/replay with imported weapon contexts. Audio-enabled desktop
  rollback, delay sync and forced host-state/slot repair pass, without offline
  autosave pollution. Automated sound output is muted through SDL dummy audio.
- SMW starts at main `6c773d6`, preserving its co-op renderer. Two complete
  stock generations are byte-identical; the Release app and all three
  standalone renderer variants pass. At 4:3, 16:9 and 32:9, full-copy versus
  optimized routes match 21 BMPs, draw lists, sprite traces and 4000 semantic
  frame rows per aspect. Actual Mods Fit/adaptive spawning matches another
  seven captures and 4000 rows. Current-build file-load functional partitions
  pass under the documented clock-reanchoring contract, without claiming
  full-chip rollback equivalence. Title PR: mstan/SuperMarioWorldRecomp#24.
- SM starts at main `b4bedbd`, preserving runtime, diagnostic replay and
  release changes. Strict full regeneration is idempotent; Release and all
  14 CTests pass. Stock/wide/120 Hz simulation traces match over 3200 frames,
  with nine matching fixed source/trace/BMP artifacts. Actual Mods Fit and
  rewind match. Audio-enabled Ceres stock/wide routes match over 11000 frames
  and reach changing guest-generated Mode 7 matrices; nine fixed-alpha/aspect
  image pairs match. The fixture bypasses prerequisites/position, not proof
  of natural completion. Title PR: mstan/SuperMetroidRecomp#31.

The owner's slot 01 report was Spark Mandrill already outside the Boss Rush
arena. A separate correctness bug was reproduced: Start on the native boss
death frame enters the weapon menu, whose coroutine temporarily clears the
live camera before publishing its menu flags. The replacement timer could
then create the next boss at X=$00C0. Saved arena ownership now gates the
replacement timer/spawn; normal combat and Mammoth scrolling remain intact.
The regression exercises 90 pause offsets for each replacement seat (180
cases), confirms in-arena creation and actual entrance into combat. It passes
on both the approved base and current main. Baseline fix `0b8be10` is applied
to current main as `8e0524d`. The owner's original snapshot is untouched;
this prevents future bad creation and does not relocate its already escaped
actor. Details: `docs/boss-rush.md`, tracker `beads-8wg.1.145`.

Current-main evidence is in `mmx-ctest.log`, `mmx-pixels-qualified`,
`mmx-net-independent`, `mmx-net-unified`, `mmx-desktop-*`, `smw-qualified2`,
`smw-fit-qualified-current`, `sm-qualified`, `sm-ceres-qualified` and
`sm-fit-qualified-current`. Full-copy references use the documented build
options with otherwise identical current-main objects. These are correctness
reruns, not new performance estimates. Earlier balanced NAS timings remain
the headroom estimate for the approved older baseline. Xbox runtime and full
campaign coverage remain unverified. At this checkpoint the helper is merged;
the three current-main title branches await publication/CI/merge completion.

## Reusable discovery checklist

Carry this method into the later ecosystems; do not assume their bottlenecks
or safe substitutions match SNES. Accurate reference code can contain costly
host representations without requiring those costs for correctness.

1. Inventory work per guest frame: copied bytes, allocations, clears, repeated
   decoding, dispatch, device execution, synchronization and presentation.
   Check multiplication by scanlines, views and rollback replay count.
2. Separate required guest behavior from host representation. Preserve timing,
   ordering, side effects and readbacks; look first for redundant host work.
   Here, per-line VRAM values were required, but 224 independent copies were not.
3. Prove ownership and invalidation. Account for DMA, interrupts, direct mod
   writes, debugger edits, restore, reset and reused addresses. Use scoped
   proofs or exact comparisons where a global dirty counter is insufficient.
4. Keep shared mechanisms separate from title policy. Share storage/decoding
   contracts where there are real consumers; retain each game's scene, asset,
   allocator and camera rules. Keep a portable reference path.
5. Measure headroom with balanced real-workload comparisons and a noise control.
   Capped FPS hides savings. Report CPU/frame work separately from wall time,
   presentation and audio delivery. Record code, heap and allocation high-water;
   memory removed from static storage may reappear on the heap.
6. Verify identical guest state, pixels and current-build restore/replay behavior
   as applicable. Vary presentation count, view width and cache warmth; exercise
   real rollback and audio paths. Keep external hardware/OS coverage explicit.
7. Reject attractive ideas when they lose. The small tile-row cache and
   palette/OAM compare/reuse probes were slower here. Cache lookup, comparison
   and invalidation can cost more than the work they replace.

This result does not establish why previous agents did not pursue it. The
current optimization guidance explicitly permits exact host-cost removal;
correctness and determinism constrain the proof, rather than banning the work.
Use the findings to improve future discovery, without treating an unprofiled
subsystem as inherently necessary overhead or promising a gain in advance.

## Follow-up systems

Owner gameplay/feel handoff: ignored `build/playtest-raster-20261022/Play.cmd`.
Normal fresh boot, 1280x720 window, normal audio, adaptive widescreen enabled,
separate settings/progress, no Magic Carpet fixture required. The copied app
matches the qualified app SHA-256 and completed a muted 180-frame startup smoke
test with audio synthesis enabled. The launcher restores normal video/audio
drivers and keeps temporary data, profiles and caches within the playtest folder.

Owner feedback: "mmx seems fine." Record this as no observed regression in
the owner's general gameplay/feel playtest. This supplements the automated
evidence; it does not establish full-campaign or original-Xbox qualification.

Latest owner instruction (2026-10-22): the method is sufficiently settled to
provide self-contained prompts now for NDS, GBA, NES and Sega Genesis, plus a
tentative Virtual Boy prompt. The owner will paste them into separate windows.
This supersedes both the serial plan and the requirement to wait for final
SNES approval before issuing prompts. Work in this SNES window is explicitly
paused after delivering the prompts, until the owner acknowledges and asks
to resume. Carry forward the measured method, invariance/restore/netplay
gates, rejected probes and scope limits; each ecosystem must find its own
bottlenecks.

### Historical pause checkpoint (superseded by P6 above)

Historical checkpoint: the owner subsequently said "You may continue".
Work has resumed; see P6 above for the newer qualification/package results.

All work remains local and uncommitted; game gitlinks have not been changed
to unreachable engine commits. Preserve the owner's existing MMX playtest.

- Optional PPU capture revision is implemented in both newer pinned engines,
  after the guest snapshot range, default off unless a title enables it.
  Actual port writes/read/reset/wrap tests pass with tracking on and off.
  IRQ/native/callback fences retain exact comparison; direct captures compare.
- SMW write-tracked/full-copy/byte-compare renderer checks pass. Both full apps
  build. Updated SMW matches its original at 4:3, 16:9 and 32:9: all 21 BMPs,
  draw lists, sprite telemetry and 4000 common frame rows per aspect match.
  Functional file-load partitions pass under the documented clock-reanchoring
  contract; this is not a full-chip rollback-byte claim.
- SM passes all 11 CTests, including full-copy and real-PPU revision tests.
  The byte-compare renderer suite also passes. Updated stock/wide/120Hz runs
  match their simulation traces; 3200 simulations produced 6424 FPS-mode
  presentations. The Ceres fixture also passes stock/wide traces for 11000
  frames and reaches adjacent guest-produced changing Mode-7 captures.
  Escape prerequisites are positioned by the diagnostic script, not validated
  as a natural escape completion.
- Final BSS reductions versus originals: SMW 14,678,176 bytes; SM 29,356,416
  bytes. Text growth: SMW 1216 bytes; SM 2856 bytes. Heap highwater depends on
  distinct VRAM versions, so these are static storage differences, not fixed
  RSS reductions.

Resume from `G:/Temp/Codex/snesrecomp/snes-bundle-20261022/`:

1. Compile/run trace-on renderer variants after the test-oracle conditional
   edit, and rerun the affected SM renderer tests (only test sources changed
   since the successful app builds).
2. Compare updated SM fixed-source artifacts to the original and render the
   Ceres previous/current pair with original/new utilities at fixed alpha
   and multiple aspects. Source: `sm-ceres-tracked/sm-renderer-m_8de132/wide/`.
   The live FPS final BMP has uncontrolled interpolation phase and must not
   be asserted identical across independent runs.
3. Run `benchmark-final.py` only with a quiet host and no competing builds or
   Beads sync. It has been written but NOT run. It balances originals against
   final candidates with warmups/A-A checks and checks state artifacts. SMW
   scope includes menus, YI2, death and map return; SM scope is boot/attract to
   Landing Site. Earlier compare-only timings were mixed; no final gain claim.
4. Package separate updated MMX/SMW/SM playtests, normal window/audio, private
   F:/G: profiles and native saves, adaptive Fit, SMW adaptive spawning. Verify
   copied executables, muted startup smokes, and preserve old owner sessions.
5. Record exact findings in Beads/docs, sync only when heavy work is quiet,
   then hand off for the owner's combined gameplay/correctness approval.

Current completed evidence: `smw-tracked-qualified`,
`sm-tracked-qualified/sm-renderer-dedl6nz5`, and
`sm-ceres-tracked/sm-renderer-m_8de132`. No agent-started build or game test is
left running at this pause. Final speed estimates, Xbox runtime qualification,
the combined owner handoff and owner approval remain outstanding.
