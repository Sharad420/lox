#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "line.h"
#include "value.h"

#include <stdint.h>

// OpCode enum defines the kind of instuction being dealt with.
typedef enum {
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_POP,
    // EDIT: To pop multiple times.
    OP_POPN,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    // Challenge EDIT: To handle declaration of single-assignment global variables.
    OP_DEFINE_FINAL_GLOBAL,
    OP_SET_GLOBAL,
    OP_FALSE,
    // No <=, >= opcodes because they can be desugared by =, <, > & !.
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_GET_SUPER,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MOD,
    OP_EXP,
    OP_NOT,
    OP_NEGATE,
    OP_PRINT,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_LIST,
    OP_GET_INDEX,
    OP_SET_INDEX,
    OP_CALL,
    OP_INVOKE,
    OP_SUPER_INVOKE,
    OP_CLOSURE,
    OP_CLOSE_UPVALUE,
    OP_RETURN,
    OP_CLASS,
    OP_INHERIT,
    OP_METHOD,
} OpCode;

// Array to hold the bytecode.
// Dynamic arrays to hold constants and line run information.
// Amortized time for appending elements to the array is O(1). Remember 6.006?
typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    LineRunArray lines;
    ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif