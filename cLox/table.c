#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

// Function to initialize a hash table.
void initTable(Table *table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

// Frees a table and it's entries
void freeTable(Table *table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

// Core of the hash table. Finds an entry and returns a pointer to it. Handles open addressing(linear probing sequence) as well.
// Load factor ensures there will always be empty buckets.
// Takes pointer to entry instead of table because it helps during resizing.
static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
    uint32_t index = key->hash % capacity;
    Entry* tombstone = NULL;

    for(;;) {
        Entry* entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // Truly empty entry, return the first tombstone or emtpy array.
                return tombstone != NULL ? tombstone : entry;
            } else {
                // We found a tombstone.
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
                return entry;
        }

        index = (index + 1) % capacity; // This modulo handles wrap around while linear probing.
    }
}

// Gets a value given a key and the table, and assigns it to the output address.
bool tableGet(Table* table, ObjString* key, Value* value) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

// Helper function to adjust an array. Bucket placement can change after resizing due to modulo, so using those macros doesn't work.
// Tombstones are not inserted, because the entries may be in different places.
static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key == NULL) continue;

        Entry* dest = findEntry(entries, capacity,entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    // Frees the old entry array.
    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}

// Places a key-value pair and returns a boolean if a pair was added.
bool tableSet(Table *table, ObjString *key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }
    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;

    // Increment only if it is a truly empty bucket.
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

// Deletes an entry and sets a tombstone in that place.
bool tableDelete(Table *table, ObjString *key) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    // Place a tombstone in that entry. Tombstone entry can be represented by anything unique.
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

// Copies all of the entries of one table to another, mainly for method inheritance.
void tableAddAll(Table *from, Table *to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}

// Checks if the string exists in the internal runtime collection.
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash % table->capacity;
    for (;;) {
        Entry* entry = &table->entries[index];
        if (entry->key == NULL) {
            // Stop if empty non-tombstone entry is found.
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length && 
            entry->key->hash == hash &&
            memcmp(entry->key->chars, chars, length) == 0) {
                // Intern string found.
                return entry->key;
            }

        index = (index + 1) % table->capacity;
    }
}