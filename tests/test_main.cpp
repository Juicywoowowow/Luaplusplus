/*
 * test_main.cpp - Test setup and debugFlags definition
 * 
 * Since we exclude main.c from tests, we need to define debugFlags here.
 */

extern "C" {
#include "common.h"
}

// Define the global debug flags (normally in main.c)
DebugFlags debugFlags = {false, false, false};
