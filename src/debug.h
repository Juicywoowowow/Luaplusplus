/*
 * debug.h - Bytecode disassembler for debugging
 */

#ifndef luapp_debug_h
#define luapp_debug_h

#include "chunk.h"

void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset);

#endif
