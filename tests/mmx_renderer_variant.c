/* Build the same renderer suite against its maintained reference paths. */
#define MMX_STAGE_TILE_REUSE 0
#if defined(MMX_RENDERER_TEST_FULLCOPY)
#undef MMX_RASTER_FULL_COPY
#define MMX_RASTER_FULL_COPY 1
#endif
#if defined(MMX_RENDERER_TEST_TRACE)
#undef SNESRECOMP_TRACE
#define SNESRECOMP_TRACE 1
#endif
#include "../src/mmx_renderer.c"
