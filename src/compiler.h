/*
 * compiler.h - Pratt parser + bytecode compiler
 * 
 * Parses Lua++ source and emits bytecode. Uses Pratt parsing for
 * expressions (precedence climbing) and recursive descent for statements.
 */

#ifndef luapp_compiler_h
#define luapp_compiler_h

#include "object.h"
#include "vm.h"

/* Compiler options */
typedef struct {
    bool eliminateDeadCode;     /* Remove unused variables */
    bool warnUnusedVariables;   /* Warn about unused variables (default: true) */
} CompilerOptions;

/* Default compiler options */
extern CompilerOptions compilerOptions;

/* Compile source to a function. Returns NULL on error. */
ObjFunction* compile(const char* source);

/* Compile with filename for better error messages */
ObjFunction* compileWithFilename(const char* source, const char* filename);

/* GC: mark compiler roots during collection */
void markCompilerRoots(void);

#endif
