// Plays the Thorhax intro (switch/thorhax_intro) before the shared host starts. The intro's output hold has nothing to
// hold here: SDL creates the window and opens audout only once mmx_desktop_main runs, so the intro simply plays first.
#include <atomic>

#include <switch.h>

#include "thorhax_intro.h"

namespace {
std::atomic<bool> g_held{false};
}  // namespace

extern "C" void RexSwitchHoldOutput(void) { g_held.store(true, std::memory_order_release); }
extern "C" void RexSwitchReleaseOutput(void) { g_held.store(false, std::memory_order_release); }

extern "C" void MmxSwitchPlayIntro(void) {
  thorhax::intro::Start();
  // The player thread releases the hold when the video ends or is skipped; then close it, so a chain-loaded restart
  // (the in-game launcher) inherits no thread.
  thorhax::intro::Wait();
  while (g_held.load(std::memory_order_acquire)) svcSleepThread(10'000'000);
}
