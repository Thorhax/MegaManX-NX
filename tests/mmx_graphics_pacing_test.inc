/* ROM-backed regression: native streaming must yield, preserve its output,
 * and resume identically after a snapshot taken inside the coroutine. */
static bool graphics_yield_seen;
static void graphics_checkpoint(uint32_t pc, int m, int x) {
  (void)m; (void)x;
  if ((pc & 0x7fffff) == 0x008127) graphics_yield_seen = true;
}
static void graphics_walk(unsigned count) {
  while (count--) frame(SNES_PAD_RIGHT | SNES_PAD_RIGHT << 12 | (1u << 31));
}
static size_t graphics_replay_state(uint8_t *out, size_t cap) {
  /* Include SPC/DSP and the carried guest clock as well as WRAM and tiles. */
  size_t n = RtlSaveSnapshotToMemory(out, cap);
  check(n != 0, "graphics pacing: complete replay snapshot saves");
  return n;
}
static void graphics_pacing_checks(const char *assets, uint8_t *start,
    uint8_t *expected, uint8_t *actual, size_t cap) {
  RtlEnableExtendedFrameTiming();
  if (assets) {
    check(MmxZeroLoad(assets), "graphics pacing: supplied Zero assets load");
    MmxZeroRegisterHooks();
    check(MmxCoopEnable(MMX_COOP_X), "graphics pacing: co-op starts");
  }
  g_config.widescreen = false;
  zero_replay(1400);
  for (unsigned n = 0; n < 3; ++n) {
    for (unsigned i = 0; i < 30; ++i) frame(SNES_PAD_START | (1u << 31));
    if (n < 2) zero_replay(181);
  }
  unsigned wait;
  for (wait = 0; wait < 3600; ++wait) {
    if (g_ram[0xd1] == 2 && g_ram[0xd2] == 4 && g_ram[0xd3] == 4 &&
        g_ram[0xba9] == 2) break;
    frame(1u << 31);
  }
  check(wait < 3600, "graphics pacing: fresh boot reaches Highway");
  for (unsigned n = 0; n < 180; ++n) frame(1u << 31);
  graphics_yield_seen = false;
  g_interp_bridge_pc_hook = graphics_checkpoint;
  double largest = 0;
  for (unsigned n = 0; n < 480 && !graphics_yield_seen; ++n) {
    graphics_walk(1);
    if (RtlLastFramePeriods() > largest) largest = RtlLastFramePeriods();
  }
  g_interp_bridge_pc_hook = NULL;
  check(graphics_yield_seen && g_ram[0xf4],
        "graphics pacing: graphics coroutine yields while loading");
  check(largest < 1.2, "graphics pacing: streamed graphics avoid multi-frame stall");
  unsigned asset = g_ram[0x98];
  unsigned table = 0x18000 + asset * 2;
  unsigned destination = g_ram[table] | g_ram[table+1] << 8;
  size_t size = RtlSaveSnapshotToMemory(start, cap);
  check(size != 0, "graphics pacing: in-flight coroutine snapshot saves");

  /* Finish this resource, then compare every output byte with an independent
   * decode of the user's ROM. Each group is a bitmask, repeated byte, and
   * optional literals; the table stores uncompressed size and LoROM pointer. */
  unsigned ticks = 0;
  while (g_ram[0xf4] && ticks < 60) { graphics_walk(1); ++ticks; }
  check(ticks && ticks < 60 && g_ram[0x98] == asset,
        "graphics pacing: same resource completes after bounded yields");
  const uint8_t *info = RomPtr(0x86f6f7 + asset * 5);
  unsigned length = info[0] | info[1] << 8;
  uint32_t address = info[2] | info[3] << 8 | info[4] << 16;
  check(destination + length <= 0x8000, "graphics pacing: output stays in tile cache");
  unsigned produced = 0;
  while (produced < length) {
    uint8_t bytes[2];
    /* Compressed streams cross from bank:$ffff to next-bank:$8000. */
    for (unsigned i = 0; i < 2; ++i) {
      bytes[i] = *RomPtr(address++);
      if (!(address & 0xffff)) address += 0x8000;
    }
    for (unsigned bit = 0; bit < 8 && produced < length; ++bit) {
      uint8_t value = bytes[1];
      if (bytes[0] & (0x80 >> bit)) {
        value = *RomPtr(address++);
        if (!(address & 0xffff)) address += 0x8000;
      }
      if (g_ram[0x10000 + destination + produced] != value) {
        fprintf(stderr, "asset %02x differs at output %u\n", asset, produced);
        check(0, "graphics pacing: decoded bytes match ROM");
      }
      ++produced;
    }
  }
  check(produced == length, "graphics pacing: all decoded bytes match ROM");
  size_t expected_size = graphics_replay_state(expected, cap);
  check(RtlLoadSnapshotFromMemory(start, size), "graphics pacing: in-flight snapshot restores");
  size_t rollback_size = RtlRollbackSaveToMemory(actual, cap);
  check(rollback_size && RtlRollbackLoadFromMemory(actual, rollback_size),
        "graphics pacing: in-flight rollback restores");
  /* Keep the rollback checkpoint while using actual for the replay result. */
  memcpy(start, actual, rollback_size);
  graphics_walk(ticks);
  size_t actual_size = graphics_replay_state(actual, cap);
  same(expected, expected_size, actual, actual_size,
       "graphics pacing: in-flight save/load reproduces gameplay, graphics and audio");
  check(RtlRollbackLoadFromMemory(start, rollback_size), "graphics pacing: rewind in-flight task");
  graphics_walk(ticks);
  actual_size = graphics_replay_state(actual, cap);
  same(expected, expected_size, actual, actual_size,
       "graphics pacing: rollback reproduces gameplay, graphics and audio");
  printf("MMX GRAPHICS PACING PASSED: asset=%02x bytes=%u ticks=%u max_periods=%.6f\n",
         asset, length, ticks, largest);
}
