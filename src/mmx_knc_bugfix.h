#pragma once
#include <stdbool.h>
#include <stdint.h>

/* KNC Bugfix */
typedef struct MmxKncBugfixState {
  uint16_t previous[2], clock;
  uint8_t progress[2], pending[2], active[2];
} MmxKncBugfixState;
void MmxKncBugfixReset(void);
void MmxKncBugfixTick(const uint8_t *ram, uint16_t p1, uint16_t p2);
MmxKncBugfixState MmxKncBugfixGetState(void);
bool MmxKncBugfixValidState(const MmxKncBugfixState *state);
void MmxKncBugfixSetState(MmxKncBugfixState state);
bool MmxKncBugfixActive(unsigned seat);
unsigned MmxKncBugfixPhase(void);
