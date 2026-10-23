# Gravity Well port and source notes

Implemented on `feat/x2-x3-weapons`, tracking `beads-8wg.1.43`. Initial source
research is also preserved privately in `_wt_mmx_weapon_silk`; implementation
continued solo in `_wt_mmx_zero`.

Reference attacks are X's original X3 weapon ID 6. Zero will use adapted
original Zero body poses, with source projectile art and mechanics. User ROM
extraction remains mandatory.

Verified source findings:

- Normal projectile class `$0C` / group `$0F` starts at 4 pixels per update,
  decelerates by `$20`, forms a stationary well, then retracts toward X.
  Original well animation and phase traces are saved privately.
- Charged class `$15` uses X body group `$33`, sequence 35 (decimal), with an
  upward cast. The field lasts 300 simulation updates after the cast/orb
  ascent; the source keeps X in his casting action during this effect.
- These weapons' damage-table entries are **utility response IDs**, not HP
  damage: 0 immune, normal 1 pull/dissolve, 2 shake/dissolve, 3 shake-only;
  charged 4 lift, 3 shake-only, 5 special. `$82:DB3F..DDD2` implements the
  responses. Do not feed these bytes into the ordinary numeric damage adapter.

Private artifacts in that worktree's `_research` include `gravity_probe.py`,
`gravity-probe.json`, `gravity-probe-summary.txt`, and bounded player,
normal/charged, pull and victim/lift assembly notes. The private X3 source
process uses port 4397 and an isolated copy of fixture 6.

## Implemented behavior

Normal flight decelerates from four pixels/tick by `$20` to one pixel/tick,
forms after travelling about 64 pixels, holds for 180 ticks, collapses and
homes back at four pixels/tick. Group `$0F` sequences 0..3 supply every phase.
The original return/pull direction vectors at `$86:E18E/$E10E` are retained;
the adapter selects the closest of the 32 directions with integer arithmetic.

The charged orb waits for the original cast's `$40` flag, rises with `$C0`
upward acceleration, then runs a 300-tick field. X remains in the `$20`-flag
hold pose until the field ends, then completes recovery. Original group `$33`
sequences 35/36 supply ground/air casting; the cache aliases the air sequence
as group 52. The original upward particles use group `$0F`, sequences 5..8,
16 pixels/tick and one spawn per four ticks, alternating screen halves.

X3 `$84:A55A` and tables `$86:B352/$B368` charge **one energy for normal and
one for the charged effect** (the initial normal shot during the hold is a
separate one-unit debit). A private original-game run measured `$5C00` to
`$5A00` for that complete hold/release. X1 arms remain required; X3 armor's
energy discounts are outside this boss-weapon port.

## X1 enemy and Zero adaptations

Only visible, live ordinary actors in damage categories 0..5 with nonzero
native body bounds are eligible. Actors with half-width at most 16 and
half-height at most 24 are movable: normal pulls them slowly for 30 ticks,
then accelerates/flashes for 30 ticks before dissolving; charged accelerates
them upward until they leave the screen. Larger actors up to 32x32 half-bounds
shake without damage. Larger/protected/boss actors are excluded. This is an
explicit conservative X1 mapping, not a claim that X3 has response data for
X1's enemies. Enemy projectile conversion and X3-specific stage triggers are
not applied to unrelated X1 actors.

Victims retain their original native sprite arrangement/resource/palette;
the renderer rebuilds those pieces while the shared enemy hook suspends AI.
Only their saved position changes. Dissolve uses X1's ordinary state-4 death
handler and clears contact damage. Death is tested as native state 4 or slot
retirement, because death animations can reuse the HP byte. Cancellation
releases surviving victims to their own AI. Boss weakness tables are unchanged.

Zero uses his original X3 group `$4A` raised-arm body frames `$43..47` timed
to X's cast, including the held pose. This is an adaptation: X3 Zero never
performed this weapon. X uses original base-X art; X1 armor overlays are
hidden during the cast, as with the Triad punch. Original source sound import
remains shared fidelity work. The intermediate breakaway pieces created by
`$82:DC4D/$D96D` use X3 enemy-specific part indices from `$86:A69F`; those
indices do not describe X1 enemies. The adapter retains X1's native death
debris and drops rather than assigning unrelated X3 enemy parts.

The existing combat snapshot holds well/body actors, particles and enemy
controllers; its size/ABI is unchanged. ROM-derived art remains local. The
address-only manifest now extracts 658 combined weapon poses (483,877 bytes).
Native and Python extraction match byte for byte, including headered ROM and
wrong-ROM cache-preservation checks.

Focused checks pass for both characters: normal deceleration/formation/return,
costs, arms, original ground/air cast, upward particles, locked field and
movement recovery, and save/replay. Real enemy tests cover pull with AI held,
native dissolve, charged acceleration/offscreen death, protected-category
exclusion, and replay of both enemy paths. Original X3 and port orb/field/body
captures were visually reviewed.

## Boss Rush entrances

Boss Rush body identity excludes the boss from Gravity Well from the moment
its slot is allocated. Native bosses temporarily use state 2 and an ordinary
damage category while their entrance and health fill run; those transient
bytes alone cannot establish that an actor is an ordinary enemy. The same
body check rejects retained victims and their renderer/AI override, so
an older snapshot containing an illegal boss capture releases it without
moving it or entering the ordinary-enemy dissolve path.
Ordinary minions remain eligible when their ownership tag refers to that boss.

The ROM-backed regression runs with `MMX_BOSS_RUSH_TEST=1`,
`MMX_BOSS_RUSH_GRAVITY_TEST=1` and `MMX_WEAPONS_TEST_ASSETS` pointing to a
locally extracted combined weapon cache. It casts with actual Y input before
allocating each of the eight bosses in both seats, checks independent full
entrances and zero defeats, and covers an illegal-capture snapshot and replay.
Initial encounter removal is fixture setup, not a validated defeat.
