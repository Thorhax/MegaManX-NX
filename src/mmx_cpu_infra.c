#include "common_cpu_infra.h"
#include "mmx_rtl.h"
#include "mmx_replay.h"
#include "mmx_dev_menu.h"

/* MMX_VARIANT_JP is a per-target compile-def (RockmanXSNESRecomp sets =1;
 * the USA MegaManX target leaves it undefined). Guard so this file compiles
 * standalone and defaults to the USA identity. */
#ifndef MMX_VARIANT_JP
#define MMX_VARIANT_JP 0
#endif

/* Per-variant game identity. Title drives coverage-artifact naming
 * (tier2_<title>_*.json) so Mega Man X (USA) and Rockman X (JP) never
 * collide — was previously mislabeled "smw" (a clone-from-SMW leftover). */
/* The developer menu's upkeep runs before replay capture sees the frame. */
static bool MmxFrameBegin(uint32_t *inputs) {
  extern uint8_t g_ram[0x20000];
  MmxDevMenuFrame(g_ram);
  return MmxReplayBegin(inputs);
}

const RtlGameInfo kMmxGameInfo = {
#if MMX_VARIANT_JP
  .title = "rockmanx",
#else
  .title = "mmx",
#endif
  .initialize = &MmxReplayRomLoaded,
  .run_frame = &RunOneFrameOfGame,
  .draw_ppu_frame = &MmxDrawPpuFrame,
  .save_name_prefix = "save",
  /* The versioned title chunk includes scheduler, widescreen and complete
   * shared execution state for file saves and rewind snapshots. */
  .state_save_extra = &MmxStateSaveExtra,
  .state_load_extra = &MmxStateLoadExtra,
  .on_state_loaded = &MmxOnStateLoaded,
  .hardware_reset = &MmxOnHardwareReset,
  .frame_begin = &MmxFrameBegin,
  .frame_end = &MmxReplayEnd,
};
