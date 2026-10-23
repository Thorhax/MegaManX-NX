#ifndef MMX_DEV_MENU_H
#define MMX_DEV_MENU_H

#include <stdbool.h>
#include <stdint.h>

/* Developer menu for testers (Select + X; on Switch, - + X). It freezes the
 * game and offers stage warps, inventory edits and respawn options. */
void MmxDevMenuRegister(void);
/* Per-frame upkeep before the guest runs: pending warps, infinite health
 * and the respawn options. */
void MmxDevMenuFrame(uint8_t *ram);
/* A loaded state carries its own inventory; forget hidden-flag tracking. */
void MmxDevMenuStateLoaded(void);

#endif
