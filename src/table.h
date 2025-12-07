/*
 * table.h - Hash table for strings -> values
 * 
 * Used for globals, object fields, methods, and string interning.
 * Keys are always ObjString* (interned, so pointer comparison works).
 */

#ifndef luapp_table_h
#define luapp_table_h

#include "common.h"
#include "value.h"

typedef struct {
    ObjString* key;     // NULL = empty slot, tombstone if key=NULL but value!=NIL
    Value value;
} Entry;

typedef struct {
    int count;          // Entries + tombstones
    int capacity;
    Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);

/* Returns true if key was new (inserted), false if updated */
bool tableSet(Table* table, ObjString* key, Value value);

/* Returns true if found, stores value in *value */
bool tableGet(Table* table, ObjString* key, Value* value);

/* Returns true if deleted */
bool tableDelete(Table* table, ObjString* key);

/* Copy all entries from src to dest */
void tableAddAll(Table* from, Table* to);

/* Find interned string by content (for string deduplication) */
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);

/* GC: mark all keys and values */
void markTable(Table* table);

/* GC: remove unmarked string keys (for string interning table) */
void tableRemoveWhite(Table* table);

#endif
