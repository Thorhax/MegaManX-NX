#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Bounded, frame-boundary diagnostic recording, shared by desktop and tests. */
void MmxReplayEnable(bool enabled);
void MmxReplaySetConfigPath(const char *path);
void MmxReplayStateLoaded(void);
void MmxReplayClose(void);
const char *MmxReplayPath(void);
bool MmxReplayBegin(uint32_t *inputs);
void MmxReplayEnd(void);
void MmxReplayViewport(int *width, int *extra);
/* Playback uses the same executable, ROM and enabled mods as the recording.
 * The caller configures those before the first frame. */
bool MmxReplayPlayback(const char *path);
bool MmxReplayPlaying(void);
bool MmxReplayFinished(void);
bool MmxReplayFailed(void);
