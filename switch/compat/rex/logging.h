#pragma once
// The ReXGlue SDK's log macros, which the Thorhax intro (switch/thorhax_intro) is written with, on this port's log
// (stderr, captured into logs/mmx-switch-err.log by switch_main.c).
#include <cstdio>
#include <format>

#define REXLOG_INFO(...) std::fprintf(stderr, "[intro] %s\n", std::format(__VA_ARGS__).c_str())
#define REXLOG_WARN(...) std::fprintf(stderr, "[intro] warning: %s\n", std::format(__VA_ARGS__).c_str())
