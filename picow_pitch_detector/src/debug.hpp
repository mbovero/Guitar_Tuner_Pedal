#pragma once

#include <cstdio>

// Debug prints disabled by default unless build defines otherwise
#ifndef TUNER_DEBUG
#define TUNER_DEBUG 0
#endif

#if TUNER_DEBUG
#define DEBUG_PRINT(...) \
    do { std::printf(__VA_ARGS__); } while (0)
#else
#define DEBUG_PRINT(...) \
    do { } while (0)
#endif