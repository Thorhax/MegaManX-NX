# Issue 110: attachment review and co-op regressions

Boss Rush PR #99 was merged to `main` as `bfe362f530f33a27f7f01b808e5fb2d634dda85e`
before this investigation branched from it. Framework PR #157 remains separate;
the game already pins its tested engine commit.

## Attachment coverage

The review covers all 18 archives in [the six-session comment](https://github.com/mstan/MegaManXSNESRecomp/issues/110#issuecomment-6202989597)
and the two earlier Penguin archives. [The audit manifest](issue-110-attachment-audit.json)
records every URL, SHA-256, size and file count. Archive CRCs were checked on
extraction. The 20 archives contain 4,037 files (376,001,179 compressed bytes,
6,830,270,947 expanded bytes), 762 replay sets and 12,866,835 CSV records.
Every replay and sidecar was inspected; every CSV row was parsed. These are
logs, configurations and checkpoints, with no attached videos, screenshots or
crash dumps. Raw checkpoints, configs, ROM-derived caches and network logs stay
outside the repository.

All replay sets have their matching checkpoint and settings sidecars. None has
a malformed record or an unfinished final input. The latest recordings use
2.2.4-alpha, Saber co-op, independent views and a 256-pixel viewport. The earlier
Penguin pair uses 2.2.2-alpha. Retained input, WRAM and co-op hashes match across
both peers for each of the six new sessions and the older pair. This rules out
a retained simulation desync in those rows; it does not prove correct gameplay
or presentation.

Targeted native playback was used in addition to the complete metadata/CSV
audit. Replaying a broken sequence can match every hash: the Eagle lift
softlock does exactly that. Fix regressions therefore check the resulting
gameplay, and preserve strict checkpoint verification in the diagnostic player.

## Causes and changes

| Report | Finding and correction | Validation |
| --- | --- | --- |
| Eagle lift freeze / “bird crash” | The nearest-player enemy update transfers scene ownership to the rider, then restores the previous body. Capturing that body on the following frame overwrites the partner's advancing teleport timer. Keep the new scene owner projected on enemy return; recover already-stuck saves before advancing transport. | Session 4's stuck frame-19200 checkpoint fails before the fix. Both rider seats now complete departure, ascent, Eagle arrival and partner return, with HP retained and byte-exact save/replay. |
| Strips/garble above a distant peer or Ride Armor | Hidden OAM slots retain old tile data at native Y=224. Reprojecting them into a distant viewport exposes stale strips; 8-bit Y wrapping can also duplicate visible pieces. Integrate the fix from draft PR #103, discarding hidden slots before translation and unwrapping real top-edge pieces once. | Pixel checks cover both anchors and views, horizontal separation, vertical offsets through +/-512, hidden slots, real sprites, wrapped top-edge sprites and fixed HUD positions. |
| Mandrill beam lighting | Fading beams retain a native screen-space top from `$87:A6ED..A6F7`. The compositor applied the peer's camera offset to moving beams, but omitted it during their fade. Reproject the cached fade position too. | Pixel regression fails before the change and passes for both seats at vertical offsets -64/0/+64, alongside the existing native beam tests. Sampled native recordings complete through both lighting sections without an exception. This fixes the identified lighting error; an additional process crash was not reproduced. |
| Independent camera rails / Armadillo false pit death | A player on solid lower-path ground at Y=1423 dies at recorded frame 29010 after the other player backtracks uphill and the shared lower camera limit becomes 1024. Keep per-seat history of the original camera-region commands (`$81:F6DC..F71D`, ROM tables `$86:E4E2`/`ECD0`) and use it for the companion's view, movement limits, culling and pit check. Scripted scenes and encounters retain their shared bounds. | The 600 recorded input pairs from session 2's frame-28799 checkpoint now leave either lower-path seat alive with its own camera while the native view follows the upper path. History survives byte-exact save/replay. |
| Armadillo coordinate wrap | `$81:F9B7` relocates only the projected native body. At recorded frame 49129 the partner keeps the old Y; on the next frame horizontal clamping hides the missing X relocation. Apply the native mapping to the stored partner, native charge objects and shots in the same frame, then reset camera-region history for the new map coordinates. | Dedicated replay regression checks relocation on the wrap frame for either driver, subsequent independent view placement and save/replay. |

Camera history is simulation state because it changes spawn/cull decisions and
pit checks. New game chunks use version 22; earlier game chunks still load.
Render captures 22/23 retain an immutable copy, while captures through 21 keep
their existing decoding. Diagnostic format 3 includes active camera history in
the view hash; formats 1/2 keep their original hash contract on playback.

## Reports still requiring gameplay confirmation

- Penguin cave scrolling and camera switches: the shared-limit error can affect
  these transitions too. Scene transport, a dead seat watching the survivor,
  and Ride Armor taking over the native actor intentionally share or transfer
  the camera. The attachments do not identify which particular switch was
  unwanted; do not count all such switches as bugs or claim all are resolved.
- Octopus invisible fish: sampled Gulpfer/Angler encounters replay and render;
  a persistently missing live fish has not been isolated. The camera-region
  change addresses one source of a live actor being outside its peer viewport.
- Chameleon missing birds: the owner cannot yet distinguish ambient birds from
  attacking enemies. No specific missing bird actor or erroneous retirement
  was isolated. No speculative spawn or sprite replacement was added.
- Mammoth session 5 is useful comparison evidence: the reporter labels it
  “camera bug avoided.” Its peer inputs/hashes agree; it is not a reproduced
  failure by itself.

Issue 110 stays open for these checks. Validation uses local replay/native
simulation and muted output; no new two-peer netplay campaign is required.

## Focused private-fixture checks

`mmx_state_tests` accepts the reporter's extracted checkpoint with
`MMX_COOP_LIFT_REPLAY_FIXTURE`, `MMX_COOP_REGION_REPLAY_FIXTURE`, or
`MMX_COOP_WRAP_REPLAY_FIXTURE`. Set `MMX_ZERO_TEST_ASSETS` to a locally derived
X3 Zero pack. Region/wrap checks also take `MMX_COOP_REGION_INPUTS`: one pair
of hexadecimal, 12-bit player inputs per line, in recorded execution order.
The fixtures are never committed. `MMX_ZERO_TEST_CAPTURE` optionally writes
both peer views after each region/wrap regression.

For unchanged recordings, use `tools/replay_diagnostic.py` with the game's ROM
and the recorded shared X3 source. The helper restores recorded active lobby
options even if offline preferences differ. A checkpoint mismatch after a
gameplay fix must remain a reported divergence; it is not a passing replay.

## Sessions 7-9: Sigma 1 intro and Vile (2.2.7-alpha)

[The third attachment set](https://github.com/mstan/MegaManXSNESRecomp/issues/110#issuecomment-6227175746)
(Saber co-op, independent views, 256-pixel viewport) replays on the
published 2.2.7 build with every checkpoint matched: 59 P1 segments, P1/P2
per-tick hashes equal. No crash; each session ended with the host quitting.

| Report | Cause | Change |
| --- | --- | --- |
| Session 7, X frozen after the Sigma 1 intro | Story Zero (`$33`) locks an idle world actor (`$88:A415` → `$84:9FEB`), dashes off, then releases (`$88:A4EE` → `$84:A003`). Independent views give each update the nearest player; at frame 3258 the release reset P2's walk while X kept `$0C16=$1E`. Highway already pinned `$33` to the world actor; Sigma 1 did not. | `$33` is a world-actor script on every stage. Saves already holding an orphaned `$1E` lock are repaired when no lock-capable object remains. |
| Session 8 Vile softlock, session 9 "Vile skip" | The view spawn pass gave armored Vile (`$67`, `$85:9E5B`, x=`$0B30`) the 320-pixel enemy look-ahead, spawning him at frame 10078 before the stage trigger at x=`$0A80` (`$88:EBC1`, event `$0C`) released the door limit. His lock (`$83:E842`, `$1E5E=$1E60=$0A80`) was then reopened by that trigger at 10139 (`$1E60=$0F00`, pinned with hardware watchpoints). Session 9 walked out of the arena; session 8's unarmored Vile waited forever at `$88:DBF5` for `$1E60==$1E5E`. Single-player widescreen already forced native timing for this room. | `MmxWidePolicy_IsScriptedEncounter` (bosses, `$66`, `$67`, `$69`) keeps native spawn timing in both the view pass and the widescreen split scan; the view pass also honours `ForceNativeSpawnTiming` for every seat's camera; Vile's bodies suspend per-seat camera history like bosses, so neither view leaves his script-set arena. |

Fixed-build replays diverge first exactly where the changes act (story Zero's
`.2=$0E` state, frame 2213-2830; Vile's deferred spawn, frame 10078); other
segments still match. Later Vile-room segments differ from their first frame
because per-seat camera history is now suspended while Vile is alive.

## Object-script audit

`tools/audit_object_scripts.py` walks every object class of the seven pools
from the ROM, every stage's spawn records (stage 9 cross-checked against a
replay WRAM image), and the current policy in `src/`. It reports classes that
lock or pose the player or write camera bounds, records the look-ahead can
spawn out of native order, and lock/pose classes left exposed to nearest-player
projection. Run `py -3 -I tools/audit_object_scripts.py --out audit.json`.

Results applied:

- Projection (independent views): Light capsule `$4D` and its child `$5C`
  are world-actor scripts, and X beside an unentered capsule takes the world in
  both camera modes. A ridden RT-55J (`$09`) and its pilot effect (`$07`) stay
  with the rider. Every other lock-capable class keeps a held seat projected
  until its release (`lock_capable` + `forced_seat`), so bosses still target
  the nearest player. Gulpfer, bounce pads and Eagle's lift keep their own
  ownership. The audit now reports no uncovered class.
- Spawn timing: `$66` (`$85:9E43`) locks the player on spawn and waits for
  x ≥ `$0900`; a 16:9 co-op view reached it while the door pinned the camera.
- Not changed: Vile's paralysis shot (`$2F`) — the fight check shows its
  release reaches whichever seat it held; Anglerge (`$21`) waits on its own
  camera test. Remaining ordering rows (capsule, Kuwanger's elevator) write
  camera bounds only on entry/boarding; `$48`'s x test selects sprite priority.

Private checks (fixtures and traces never committed), each run with either seat
driving and repeated with `MMX_COOP_SIGMA1_UNIFIED=1`:
`MMX_COOP_SIGMA1_INTRO_FIXTURE` + `MMX_COOP_SIGMA1_INPUTS` (session 7,
frame 3000), `MMX_COOP_SIGMA1_LOCKED_FIXTURE` (frame 4200, X already locked),
and `MMX_COOP_SIGMA1_VILE_FIXTURE` (session 8 frame 9600 with its inputs; or
session 9 frame 11999 with `MMX_COOP_SIGMA1_ZERO_WORLD=1`). The Vile check
requires the arena lock to hold from Vile's initialization through unarmored
Vile's entrance, both players back, and no seat held over 300 frames in the
fight. All three were RED on 2.2.7 and are GREEN here.
