/*
 * diagnostic.h - Error, warning, and note reporting system
 * 
 * Provides rich error messages with source context, colors,
 * and related notes for better developer experience.
 */

#ifndef luapp_diagnostic_h
#define luapp_diagnostic_h

#include "common.h"

/* ANSI color codes */
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_WHITE   "\033[37m"

/* Diagnostic severity levels */
typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE
} DiagLevel;

/* A single diagnostic location */
typedef struct {
    int line;
    int column;
    int length;     /* Length of the highlighted span */
} SourceLoc;

/* A diagnostic message (error, warning, or note) */
typedef struct {
    DiagLevel level;
    int code;               /* E001, W001, etc. */
    const char* message;
    SourceLoc loc;
    const char* help;       /* Optional help suggestion */
} Diagnostic;

/* Maximum notes per diagnostic */
#define MAX_NOTES 4
#define MAX_ERRORS 8

/* Diagnostic context - holds source and accumulated diagnostics */
typedef struct {
    const char* source;         /* Full source code */
    const char* filename;       /* Source filename */
    int errorCount;
    int warningCount;
    bool useColors;             /* Whether to use ANSI colors */
} DiagContext;

/* Initialize diagnostic context */
void initDiagContext(DiagContext* ctx, const char* source, const char* filename);

/* Report a diagnostic with source context */
void reportDiagnostic(DiagContext* ctx, DiagLevel level, int code,
                      int line, int column, int length,
                      const char* message, const char* help);

/* Report a note (attached to previous diagnostic) */
void reportNote(DiagContext* ctx, int line, int column, int length,
                const char* message);

/* Check if we should stop due to too many errors */
bool shouldStopCompiling(DiagContext* ctx);

/* Get line from source by line number (1-indexed) */
const char* getSourceLine(const char* source, int lineNum, int* lineLength);

/* Error codes */
#define E_UNEXPECTED_CHAR    1
#define E_UNTERMINATED_STR   2
#define E_EXPECT_EXPRESSION  3
#define E_EXPECT_TOKEN       4
#define E_UNDEFINED_VAR      5
#define E_REDECLARED_VAR     6
#define E_INVALID_ASSIGN     7
#define E_BREAK_OUTSIDE_LOOP 8
#define E_SELF_OUTSIDE_CLASS 9
#define E_SUPER_NO_SUPERCLASS 10
#define E_RETURN_TOP_LEVEL   11
#define E_TOO_MANY_CONSTANTS 12
#define E_TOO_MANY_LOCALS    13
#define E_TOO_MANY_PARAMS    14
#define E_TOO_MANY_ARGS      15
#define E_INHERIT_SELF       16

/* Warning codes */
#define W_UNUSED_VARIABLE    1
#define W_UNUSED_PARAMETER   2
#define W_SHADOWED_VARIABLE  3

#endif
