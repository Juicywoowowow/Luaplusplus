#ifndef luapp_common_h
#define luapp_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Compile-time debug flags (for development builds)
#define DEBUG_STRESS_GC 0

// Runtime debug flags (controlled via --verbose)
typedef struct {
    bool printCode;       // Dump bytecode after compilation
    bool traceExecution;  // Trace each instruction
    bool logGC;           // Log GC events
} DebugFlags;

extern DebugFlags debugFlags;

#define UINT8_COUNT (UINT8_MAX + 1)

#endif
