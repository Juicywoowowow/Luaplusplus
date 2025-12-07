/*
 * diagnostic.c - Rich error reporting implementation
 */

#include "diagnostic.h"
#include <stdio.h>
#include <string.h>

void initDiagContext(DiagContext* ctx, const char* source, const char* filename) {
    ctx->source = source;
    ctx->filename = filename ? filename : "<stdin>";
    ctx->errorCount = 0;
    ctx->warningCount = 0;
    ctx->useColors = true;  /* Default to colors on */
}

/* Get a specific line from source (1-indexed) */
const char* getSourceLine(const char* source, int lineNum, int* lineLength) {
    if (source == NULL || lineNum < 1) {
        *lineLength = 0;
        return NULL;
    }
    
    const char* lineStart = source;
    int currentLine = 1;
    
    /* Find the start of the requested line */
    while (currentLine < lineNum && *lineStart != '\0') {
        if (*lineStart == '\n') {
            currentLine++;
        }
        lineStart++;
    }
    
    if (*lineStart == '\0' && currentLine < lineNum) {
        *lineLength = 0;
        return NULL;
    }
    
    /* Find the end of the line */
    const char* lineEnd = lineStart;
    while (*lineEnd != '\0' && *lineEnd != '\n') {
        lineEnd++;
    }
    
    *lineLength = (int)(lineEnd - lineStart);
    return lineStart;
}

/* Print the colored level prefix */
static void printLevel(DiagContext* ctx, DiagLevel level, int code) {
    if (ctx->useColors) {
        switch (level) {
            case DIAG_ERROR:
                fprintf(stderr, ANSI_BOLD ANSI_RED "error" ANSI_RESET);
                fprintf(stderr, ANSI_BOLD "[E%03d]" ANSI_RESET, code);
                break;
            case DIAG_WARNING:
                fprintf(stderr, ANSI_BOLD ANSI_YELLOW "warning" ANSI_RESET);
                fprintf(stderr, ANSI_BOLD "[W%03d]" ANSI_RESET, code);
                break;
            case DIAG_NOTE:
                fprintf(stderr, ANSI_BOLD ANSI_CYAN "note" ANSI_RESET);
                break;
        }
    } else {
        switch (level) {
            case DIAG_ERROR:   fprintf(stderr, "error[E%03d]", code); break;
            case DIAG_WARNING: fprintf(stderr, "warning[W%03d]", code); break;
            case DIAG_NOTE:    fprintf(stderr, "note"); break;
        }
    }
}

/* Print the source location */
static void printLocation(DiagContext* ctx, int line, int column) {
    if (ctx->useColors) {
        fprintf(stderr, ANSI_BOLD ANSI_BLUE "  --> " ANSI_RESET);
    } else {
        fprintf(stderr, "  --> ");
    }
    fprintf(stderr, "%s:%d:%d\n", ctx->filename, line, column);
}

/* Print the source line with caret indicator */
static void printSourceContext(DiagContext* ctx, int line, int column, int length,
                                DiagLevel level) {
    int lineLength;
    const char* sourceLine = getSourceLine(ctx->source, line, &lineLength);
    
    if (sourceLine == NULL) return;
    
    /* Line number gutter */
    if (ctx->useColors) {
        fprintf(stderr, ANSI_BOLD ANSI_BLUE "%4d | " ANSI_RESET, line);
    } else {
        fprintf(stderr, "%4d | ", line);
    }
    
    /* Print the source line */
    fprintf(stderr, "%.*s\n", lineLength, sourceLine);
    
    /* Print the caret line */
    if (ctx->useColors) {
        fprintf(stderr, ANSI_BOLD ANSI_BLUE "     | " ANSI_RESET);
    } else {
        fprintf(stderr, "     | ");
    }
    
    /* Spaces up to the column */
    for (int i = 1; i < column && i <= lineLength; i++) {
        /* Preserve tabs */
        if (sourceLine[i - 1] == '\t') {
            fprintf(stderr, "\t");
        } else {
            fprintf(stderr, " ");
        }
    }
    
    /* Print carets with color */
    const char* caretColor = "";
    if (ctx->useColors) {
        switch (level) {
            case DIAG_ERROR:   caretColor = ANSI_BOLD ANSI_RED; break;
            case DIAG_WARNING: caretColor = ANSI_BOLD ANSI_YELLOW; break;
            case DIAG_NOTE:    caretColor = ANSI_BOLD ANSI_CYAN; break;
        }
        fprintf(stderr, "%s", caretColor);
    }
    
    /* Print the carets */
    int caretLen = length > 0 ? length : 1;
    for (int i = 0; i < caretLen; i++) {
        fprintf(stderr, "^");
    }
    
    if (ctx->useColors) {
        fprintf(stderr, ANSI_RESET);
    }
    fprintf(stderr, "\n");
}

void reportDiagnostic(DiagContext* ctx, DiagLevel level, int code,
                      int line, int column, int length,
                      const char* message, const char* help) {
    /* Update counts */
    if (level == DIAG_ERROR) {
        ctx->errorCount++;
    } else if (level == DIAG_WARNING) {
        ctx->warningCount++;
    }
    
    /* Print the header: error[E001]: message */
    printLevel(ctx, level, code);
    fprintf(stderr, ": %s\n", message);
    
    /* Print location */
    printLocation(ctx, line, column);
    
    /* Print empty gutter line */
    if (ctx->useColors) {
        fprintf(stderr, ANSI_BOLD ANSI_BLUE "     |" ANSI_RESET "\n");
    } else {
        fprintf(stderr, "     |\n");
    }
    
    /* Print source context */
    printSourceContext(ctx, line, column, length, level);
    
    /* Print empty gutter line */
    if (ctx->useColors) {
        fprintf(stderr, ANSI_BOLD ANSI_BLUE "     |" ANSI_RESET "\n");
    } else {
        fprintf(stderr, "     |\n");
    }
    
    /* Print help if provided */
    if (help != NULL) {
        if (ctx->useColors) {
            fprintf(stderr, ANSI_BOLD ANSI_CYAN "help" ANSI_RESET ": %s\n", help);
        } else {
            fprintf(stderr, "help: %s\n", help);
        }
    }
    
    fprintf(stderr, "\n");
}

void reportNote(DiagContext* ctx, int line, int column, int length,
                const char* message) {
    /* Print note header */
    if (ctx->useColors) {
        fprintf(stderr, ANSI_BOLD ANSI_CYAN "note" ANSI_RESET ": %s\n", message);
    } else {
        fprintf(stderr, "note: %s\n", message);
    }
    
    /* Print location */
    printLocation(ctx, line, column);
    
    /* Print empty gutter line */
    if (ctx->useColors) {
        fprintf(stderr, ANSI_BOLD ANSI_BLUE "     |" ANSI_RESET "\n");
    } else {
        fprintf(stderr, "     |\n");
    }
    
    /* Print source context */
    printSourceContext(ctx, line, column, length, DIAG_NOTE);
    
    fprintf(stderr, "\n");
}

bool shouldStopCompiling(DiagContext* ctx) {
    return ctx->errorCount >= MAX_ERRORS;
}
