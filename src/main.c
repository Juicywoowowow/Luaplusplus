/*
 * main.c - CLI entry point for luap
 * 
 * Usage:
 *   luap                    - Start REPL
 *   luap <file>             - Run a .luapp or .lua file
 *   luap --verbose <file>   - Run with debug output
 */

#include "common.h"
#include "compiler.h"
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LUAPP_VERSION "0.1.0"

// Global debug flags
DebugFlags debugFlags = {false, false, false};

static void repl(void) {
    char line[1024];
    
    printf("Lua++ %s - Type 'exit' to quit\n", LUAPP_VERSION);
    if (debugFlags.printCode) {
        printf("[verbose mode: bytecode + execution trace enabled]\n");
    }
    
    for (;;) {
        printf("> ");
        
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        
        // Check for exit command
        if (strncmp(line, "exit", 4) == 0) break;
        
        interpret(line);
    }
}

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }
    
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    
    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }
    
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }
    
    buffer[bytesRead] = '\0';
    
    fclose(file);
    return buffer;
}

static void runFile(const char* path) {
    char* source = readFile(path);
    InterpretResult result = interpretWithFilename(source, path);
    free(source);
    
    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

static void showHelp(void) {
    printf("Lua++ %s\n", LUAPP_VERSION);
    printf("Usage: luap [options] [script]\n\n");
    printf("Options:\n");
    printf("  -h, --help       Show this help message\n");
    printf("  -V, --version    Show version\n");
    printf("  -v, --verbose    Enable debug output (bytecode dump + execution trace)\n");
    printf("  --dump-bytecode  Only dump bytecode, don't trace execution\n");
    printf("  --trace          Only trace execution, don't dump bytecode\n");
    printf("  --log-gc         Log garbage collection events\n\n");
    printf("If no script is provided, starts interactive REPL.\n");
}

static void enableVerbose(void) {
    debugFlags.printCode = true;
    debugFlags.traceExecution = true;
    debugFlags.logGC = true;
}

int main(int argc, const char* argv[]) {
    const char* scriptPath = NULL;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("Lua++ %s\n", LUAPP_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            showHelp();
            return 0;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            enableVerbose();
        } else if (strcmp(argv[i], "--dump-bytecode") == 0) {
            debugFlags.printCode = true;
        } else if (strcmp(argv[i], "--trace") == 0) {
            debugFlags.traceExecution = true;
        } else if (strcmp(argv[i], "--log-gc") == 0) {
            debugFlags.logGC = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Try 'luap --help' for usage.\n");
            return 64;
        } else {
            scriptPath = argv[i];
        }
    }
    
    initVM();
    
    if (scriptPath == NULL) {
        repl();
    } else {
        runFile(scriptPath);
    }
    
    freeVM();
    return 0;
}
