#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

// Entry into a hash table. ObjString* instead of Value because the key is always a string in clox.
typedef struct {
    ObjString* key;
    Value value;
} Entry;

// Struct for a hash table.
typedef struct {
    int count;
    int capacity;
    Entry* entries;
} Table;

void initTable(Table* table);
void freeTable(Table* table);
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);

#endif