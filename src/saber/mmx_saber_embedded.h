#ifndef MMX_SABER_EMBEDDED_H
#define MMX_SABER_EMBEDDED_H

#include <stddef.h>
#include <stdint.h>

/* Saber Zero sidecars built from the tracked donor art (assets/saber-zero)
 * and compiled into the executable by cmake/embed_saber_sidecars.cmake.
 * None of this is ROM-derived; the X3 wave cache is still extracted at
 * runtime from the player's own X3 ROM. */
extern const uint8_t kMmxSaberEmbeddedSprites[];
extern const size_t kMmxSaberEmbeddedSpritesSize;
extern const uint8_t kMmxSaberEmbeddedRide[];
extern const size_t kMmxSaberEmbeddedRideSize;
extern const uint8_t kMmxSaberEmbeddedSfx[];
extern const size_t kMmxSaberEmbeddedSfxSize;

#endif
