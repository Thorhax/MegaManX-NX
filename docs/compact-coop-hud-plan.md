# Compact co-op HUD

Owner request, 2026-10-05. Tracker: beads-8wg.1.116.
Branch: feat/compact-coop-hud. Base: origin/main d409e26.

Owner requested the implementation preview on 2026-10-05.
Owner approved the third preview on 2026-10-05.

Layout confirmed by the owner, 2026-10-05:

| Upper player row | |
| --- | --- |
| X health | X ammo |
| Zero health | Zero ammo |

Both columns align vertically. Each player has two adjacent vertical bars.
X uses the current orientation, with the player/weapon icons at the bottom.
Zero's icons are at the top of his bars, facing X's icons across the row gap.
Zero's fill grows from bottom to top; recommended depletion is from the top
so the remaining fill stays at the bottom. Preserve readable numeric/tick
semantics if the existing renderer supplies them.

The intent is two bar widths instead of four, with more vertical stacking.
Choose bar height and spacing during visual design so the combined panel is
compact and does not obscure gameplay. Use the existing game pixel art and
HUD style. Preserve weapon identity and the current handling of absent ammo.

## Acceptance

Show the owner real gameplay screenshots or a live candidate before marking
this done or shipping it. Final owner visual review is required.

Review full, partial and empty health/ammo, health-only and weapon-equipped
states, 4:3 and widescreen, and both player character assignments. Preserve
single-player HUD behavior and keep boss health readable without overlap.

## Preview

The co-op renderer now anchors both health bars at x=8 and both ammo bars
at x=24. X badges remain at y=80; Zero badges move to y=100. Zero meter
frames are mirrored vertically, with upright badges and remaining fill at
the bottom. Placement follows character identity, independent of controller
seat. Single-player rendering is unchanged.

Full, partial, empty and health-only renderer captures were inspected in
4:3 and 16:9. A separate prefilled preview executable is in
`build-hud-preview/MegaManXSNESRecomp-hud-preview-v3-private/`. Slot 01 is a
HUD review checkpoint; the existing player build and its saves are intact.

The second preview replaces the detached footer corners with a mirrored
cap from the same meter frame. Upright character/weapon symbols are
composited inside that continuous frame using their original pixels and
palettes. Native Shotgun Ice and Fire Wave icons were also inspected.
Owner approved the third preview on 2026-10-05.

The third preview preserves the white top-cap highlight above the weapon
panel, moving the upright weapon symbol down one pixel to avoid covering
that bevel. Health and ammo cap thickness now matches.

## Layout option

The Co-op mod exposes a HUD layout dropdown. Compact is the default when
no preference is saved; Horizontal restores the original side-by-side meters.
The choice is applied by the co-op plugin and survives save-state loads.
Both choices were activated through the real launcher provider and rendered
with full, partial, empty and absent ammo in 4:3 and 16:9.
