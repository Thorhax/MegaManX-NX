/* Test-only attribution. Repeat the normal frame sequence with clocks around
 * its stages, then compare against frame() from the same authoritative state.
 * These are wall-time stages (preemption included), not CPU-time speedups. */
static uint64_t frame_cost_pixels(const uint32_t *pixels, size_t count) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i < count; ++i) {
    hash ^= pixels[i]; hash *= UINT64_C(1099511628211);
  }
  return hash;
}
static void frame_cost_draw(uint32_t *pixels) {
  if (g_mmx_custom_renderer) {
    if (!MmxRendererDraw(pixels, g_mmx_custom_view, true))
      check(0, "attributed frame renders");
  }
  else memcpy(pixels, g_my_pixels, 256 * 224 * sizeof(*pixels));
}
static void frame_cost_checks(uint8_t *start, uint8_t *expected,
                              uint8_t *actual, size_t cap) {
  enum { N = 160 };
  static uint32_t pixels[MMX_RENDER_MAX_WIDTH * 224];
  const char *names[] = {"highway-idle", "storm-setup", "magic-carpet"};
  const char *paths[] = {getenv("MMX_FRAME_COST_HIGHWAY"),
      getenv("MMX_FRAME_COST_STORM"), getenv("MMX_FRAME_COST_CARPET")};
  const char *aspects[] = {"native", "16:9", "32:9"};
  const double scale = 1000.0 / (double)SDL_GetPerformanceFrequency();
  FILE *csv = fopen("frame-costs.csv", "w");
  check(csv != NULL, "frame attribution output opens");
  fputs("scene,aspect,custom,frames,guest_ms,raster_ms,compose_ms,rollback_save_ms,"
        "vram_images,vram_copied_bytes,rollback_bytes,pixel_hash\n", csv);
  for (unsigned scene = 0; scene < 3; ++scene) {
    check(paths[scene] != NULL, "attribution has a current-build scene fixture");
    for (unsigned view = 0; view < 3; ++view) {
      check(RtlLoadSnapshot(paths[scene]), "attribution fixture loads");
      g_config.display_aspect = kSnesDisplayAspect_Crt4x3;
      g_config.widescreen = view != 0;
      g_mmx_custom_aspect = view == 2 ? MMX_ASPECT_32_9 : MMX_ASPECT_16_9;
      int width, height;
      MmxPrepareFrame(1280, 720, &width, &height);
      for (unsigned i = 0; i < 24; ++i) { frame(0); frame_cost_draw(pixels); }
      size_t n = RtlSaveSnapshotToMemory(start, cap);
      check(n != 0, "attribution start saves");
      uint64_t reference_pixels = 0;
      size_t en = 0;
      for (unsigned i = 0; i < N; ++i) {
        frame(0); frame_cost_draw(pixels);
        en = RtlRollbackSaveToMemory(expected, cap);
        if (!en) check(0, "reference rollback snapshot saves");
        if (!(i % 16)) reference_pixels = (reference_pixels ^ frame_cost_pixels(pixels,
            (size_t)(g_mmx_custom_renderer ? g_mmx_custom_view.width : 256) * 224)) *
            UINT64_C(1099511628211) + i;
      }
      check(RtlLoadSnapshotFromMemory(start, n), "attribution start restores");
      double elapsed[4] = {0};
      uint64_t measured_pixels = 0;
      size_t an = 0;
      for (unsigned i = 0; i < N; ++i) {
        uint64_t t0 = SDL_GetPerformanceCounter();
        MmxBeforeFrame();
        RtlRunFrame(1u << 30);
        MmxBossRushAudioPresent();
        uint64_t t1 = SDL_GetPerformanceCounter();
        CaptureSimulationFrame(1);
        uint64_t t2 = SDL_GetPerformanceCounter();
        frame_cost_draw(pixels);
        uint64_t t3 = SDL_GetPerformanceCounter();
        an = RtlRollbackSaveToMemory(actual, cap);
        uint64_t t4 = SDL_GetPerformanceCounter();
        if (!an) check(0, "attributed rollback snapshot saves");
        elapsed[0] += (t1 - t0) * scale;
        elapsed[1] += (t2 - t1) * scale;
        elapsed[2] += (t3 - t2) * scale;
        elapsed[3] += (t4 - t3) * scale;
        if (!(i % 16)) measured_pixels = (measured_pixels ^ frame_cost_pixels(pixels,
            (size_t)(g_mmx_custom_renderer ? g_mmx_custom_view.width : 256) * 224)) *
            UINT64_C(1099511628211) + i;
      }
      same(expected, en, actual, an, "attributed sequence preserves full rollback state");
      check(reference_pixels == measured_pixels, "attributed sequence preserves sampled pixels");
      MmxRasterStats storage = g_mmx_custom_renderer ?
          MmxRendererGetRasterStats() : (MmxRasterStats){0};
      fprintf(csv, "%s,%s,%u,%u,%.6f,%.6f,%.6f,%.6f,%u,%llu,%zu,%016llx\n",
          names[scene], aspects[view], g_mmx_custom_renderer, N,
          elapsed[0] / N, elapsed[1] / N, elapsed[2] / N, elapsed[3] / N,
          storage.vram_images, (unsigned long long)storage.vram_copied_bytes,
          an, (unsigned long long)measured_pixels);
      fflush(csv);
    }
  }
  check(fclose(csv) == 0, "frame attribution output writes");
  puts("MMX FRAME COST CHECKS PASSED");
}
