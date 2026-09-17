#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "object.h"
#include "scanner.h"
#include "value.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

// Parser struct to hold tokens for single token lookahead.
typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

// Precedence struct to provide precedence order for Pratt's parser. Note that enum values are increasing as is precedence.
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // ! -
    PREC_CALL,       // . ()
    PREC_PRIMARY
} Precedence;

// Type which is a pointer to a function that returns void. Pretty ugly syntax but makes sense.
// C does not have first-class functions, but has function pointers.
typedef void (*ParseFn)(bool canAssign);

// Struct which contains:
// 1. Function to compile a prefix expression starting with a token of that type.
// 2. Function to compile an infix expression whose left operand is followed with a token of that type.
// 3. Precedence of an infix expression that uses that token as an operator.
// Read 17.5-1.76 for more.
typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

// Struct that holds the token, the depth and the 'finality' of the local variable.
// Challenge EDIT: Added support for single assignment local variables.
typedef struct {
    Token name;
    int depth;
    bool isFinal;
} Local;

// Struct that holds the local slot of the captured local or upvalue.
// isLocal flag determines if a given closure is a local variable or an upvalue from a surrounding function.
typedef struct {
    uint8_t index;
    bool isLocal;
} Upvalue;

// Struct that tells the compiler what is being compiled currently. Most of the compiler does not care about this, hence a useful abstraction.
typedef enum {
    TYPE_FUNCTION, 
    TYPE_SCRIPT
} FunctionType;

// Struct to handle local variable evaluation and store their stack positions, to be used as instruction operands.
// Struct which contains:
// 1. Local array to store all the local variables in scope, in order.
// 2. Count of the number of locals in scope.
// 3. Number of blocks surrounding the current bit of code being compiled.
// The structure of the compiler is changed. 
// Now supports top-level code as an implicit function and user defined functions. Converting top level code to an implicit function instead of adding mew machinery for different paths.
// A reference to the enclosing compiler.
typedef struct Compiler {
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;

    Local locals[UINT8_COUNT];
    int localCount;
    Upvalue upvalues[UINT8_COUNT];
    int scopeDepth;
} Compiler;

// Global variable.
Parser parser;
Compiler* current = NULL; // Global compiler to store locals.
Chunk* compilingChunk;

// Returns the pointer to current chunk.
static Chunk* currentChunk() {
    return &current->function->chunk;
}

// Utility function to report an error. It is common style to pass an address for a struct that is going to be read only. This function does not need it's own copy of Token.
// The panic mode error recovery in jlox is emulated by having a panic flag and ignoring all errors when the panic flag is set.
static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

// To handle the 2 common error cases of current and previous token.
static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

static void advance() {
    parser.previous = parser.current;

    // Loop to handle errors and update the parser.
    for (;;) {
        parser.current = scanToken();
        if (parser.current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser.current.start);
    }
}

// Matches token type and reports error if otherwise.
static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

// Helper to check if the parser matches to the given type. 
static bool check(TokenType type) {
    return parser.current.type == type;
}

// Matches a token and advances to the next one, like in scanner.
static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

// Writes the generated byte to the current chunk.
static void emitByte(uint8_t byte) {
    writeChunk(currentChunk(), byte, parser.previous.line);
}

// Writes the generated instruction and operand to the current chunk.
static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

// Writes a loop instruction and an operand comes back to a predetermined offset in the bytecode.
static void emitLoop(int loopStart) {
    emitByte(OP_LOOP);
    
    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

// Writes a jump if false instruction along with a placeholder offset for the amount of bytecode to jump. Placeholder takes 2 bytes, allowing 65,535 bytecodes to be jumped.
// Returns the offset of the emitted instruction in the chunk.
static int emitJump(uint8_t instruction) {
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

// Writes the return to the chunk.
static void emitReturn() {
    emitByte(OP_NIL);
    emitByte(OP_RETURN);
}

// Checks if the constant index < 8 bits(i.e not more than 256 constants), adds it to the constant pool and returns the index.
static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;

}

// Emits the intruction and operand which contains index of the constant in the constant pool.
static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}

// Backpatches the operand at the given location with the calculated jump offset.
static void patchJump(int offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = currentChunk()->count - offset - 2;

    if (jump > UINT16_MAX) {
        error("Too much code to jump over");
    }

    // Stores the jump offset in big endian format.
    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}

// Initializes a compiler, and associates a new function to it.
static void initCompiler(Compiler* compiler, FunctionType type) {
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->function = newFunction(); // Garbage collection related paranoia as Nystrom calls it.
    current = compiler;
    if (type != TYPE_SCRIPT) {
        // Copy of the string after fun declaration, so the compiler has ownership of the string.
        current->function->name = copyString(parser.previous.start, parser.previous.length);
    }

    // Manual creation of an implicit variable that the VM uses. Stores the function associated with this compiler.
    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
    local->isFinal = false;
}

// Signals the end of the compilation process, returns an addresss to the function object created by the compiler.
static ObjFunction* endCompiler() {
    emitReturn();
    ObjFunction* function = current->function;
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        // Top level function created does not have a name.
        disassembleChunk(currentChunk(), function->name != NULL ? function->name->chars : "<script>");
    }
#endif
    current = current->enclosing;
    return function;
}

// Helper function to begin a local scope.
static void beginScope() {
    current->scopeDepth++;
}

// Helper function to end a scope and remove all local variables from the recently ended scope.
static void endScope() {
    current->scopeDepth--;

    // EDIT: Handles all the pops together as one instruction, instead of multiple OP_POP instructions.
    int numPops = 0;
    while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth) {
        numPops++;
        current->localCount--;
    }
    if (numPops > 0) emitBytes(OP_POPN, numPops);
}


// Couple of forward declarations to handle cyclic dependencies.
static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);


// Helper function to make a constant and return it's index in the constant pool.
static uint8_t identifierConstant(Token* name) {
    return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

// Helper function to check if identifier tokens are equal.
static bool identifiersEqual(Token* a, Token* b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

// Adds an upvalue to the upvalue array, and keeps count of how many upvalues a function uses.
// Mirrors the stack slot indexes of the closed over variables for each closure at runtime.
// Function stores upvalue count because it is needed at runtime.
static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
    int upvalueCount = compiler->function->upvalueCount;

    for (int i = 0; i < upvalueCount; i++) {
        Upvalue* upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    if (upvalueCount == UINT8_COUNT) {
        error("Too many closure variables in function.");
        return 0;
    }


    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

// Heart of resolving a local variable. Helper function to find a local variable and calculate the stack slot index.
// The VM stack layout is the same as the locals array at runtime!
// Guardrail: Local variable values at declaration are not removed from the stack until the scope ends!
// Work out {var a = 10; var b = 12; print 1 + b;} if confused.
static int resolveLocal(Compiler* compiler, Token* name) {
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local* local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (local->depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

// Helper function to resolve an upvalue. Called after failing to resolve variable in current function's scope.
// If upvalue doesn't exist in immediately enclosing function, the recursion ensures that it checks local variables too!!
// See 25.2 for a clear example.
static int resolveUpvalue(Compiler* compiler, Token* name) {
    if (compiler->enclosing == NULL) return -1;

    // Captures the local variable of the immediately enclosing function and creates an upvalue.
    int local = resolveLocal(compiler->enclosing, name);
    if (local != -1) {
        return addUpvalue(compiler, (uint8_t)local, true);
    }

    // Captures an upvalue from the immediately enclosing function and creates an upvalue.
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t)upvalue, false);
    }

    return -1;
}

// Helper function to add a local variable and it's scope to the compiler's list of variables.
// Initializes the next available Local.
static void addLocal(Token name, bool isFinal) {
    if (current->localCount == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }

    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1; // Emulates the "uninitialized" state of a local variable declaration.
    local->isFinal = isFinal;
}

// Helper function to check if local variable is final.
static bool isFinalVar(int arg) {
    return current->locals[arg].isFinal;
}

// Helper function to record the existence of a local variable only.
// Challenge EDIT: Added support for single assignment local variables.
static void declareVariable(bool isFinal)  {
    if (current->scopeDepth == 0) return;

    Token* name = &parser.previous;
    // Check for variable redeclaration in the SAME scope.
    for (int i = current->localCount - 1; i >= 0; i--) {
        Local* local = &current->locals[i];
        if (local->depth != -1 && local->depth < current->scopeDepth) {
            break;
        }

        if (identifiersEqual(name, &local->name)) {
            error("Already a variable with this name in this scope.");
        }
    }
    addLocal(*name, isFinal);
}

// Helper function to parse a variable name.
// Challenge EDIT: Added support for single assignment local variables.
static uint8_t parseVariable(const char* errorMessage, bool isFinal) {
    consume(TOKEN_IDENTIFIER, errorMessage);

    declareVariable(isFinal);
    if (current->scopeDepth > 0) return 0; // dummy return for local variables, bears no significance.

    return identifierConstant(&parser.previous);
}

// Helper function to mark a local variable as "initialized" after ensuring it doesn't reference itself in it's initializer.
static void markInitialized() {
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount - 1].depth = current->scopeDepth;
}

// Helper function to define a global variable using it's index in the constant table. Local variables are not looked up by name in runtime, so they are not stored in the constant pool.
// The value is ITSELF the variable, there is no need to look it up via a table at runtime.
// Challenge EDIT: Added support for single assignment variables(local and global).
static void defineVariable(uint8_t global, bool isFinal) {
    if (current->scopeDepth > 0) {
        markInitialized();
        return;
    }

    emitBytes(isFinal ? OP_DEFINE_FINAL_GLOBAL : OP_DEFINE_GLOBAL, global);
}

// Helper function to compile the arguments and return the argCount.
static uint8_t argumentList() {
    uint8_t argCount = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argCount == 255) {
                error("Can't have more than 255 arguments.");
            }
            argCount++;
        }while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argCount;
}

// Parses an and expression and sets appropriate control flow.
static void and_(bool canAssign) {
    int endJump = emitJump(OP_JUMP_IF_FALSE);

    emitByte(OP_POP);
    parsePrecedence(PREC_AND);

    patchJump(endJump);
}

// Parsing function for binary expressions.
static void binary(bool canAssign) {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:       emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:          emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
        default: return; // Unreachable.     
    }
}

// Parsing function for calling a function, treated as an infix expression.
// The callee's Frame perfectly aligns with the caller's frame, where the function and it's arguments are already present!
static void call(bool canAssign) {
    uint8_t argCount = argumentList();
    emitBytes(OP_CALL, argCount);
}

// Parsing function for literals.
static void literal(bool canAssign) {
    switch(parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL:   emitByte(OP_NIL); break;
        case TOKEN_TRUE:  emitByte(OP_TRUE); break;
        default: return; // Unreachable.
    }
}

// Parsing function for grouping prefix expression.
// Assumes these single prefix tokens have been advanced already.
static void grouping(bool canAssign) {
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// Converts the token's string to a double(Value).
static void number(bool canAssign) {
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

// Parses the or expression and sets appropriate control flow. A jump if true instruction is better, but playing with the instruction sequence here.
static void or_(bool canAssign) {
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    int endJump = emitJump(OP_JUMP);

    patchJump(elseJump);
    emitByte(OP_POP);

    parsePrecedence(PREC_OR);
    patchJump(endJump);
}

// Parsing function for a string.
static void string(bool canAssign) {
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

// Helper function to generate bytecode for assignment or variable access.
// Challenge EDIT: Added support for single assignment local variables.
static void namedVariable(Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(current, &name);
    bool isLocal = arg != -1;

    if (isLocal) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(current, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    }else {
        arg = identifierConstant(&name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }
    
    if (canAssign && match(TOKEN_EQUAL)) {
        if (isLocal && isFinalVar(arg)) {
            error("Can't assign to a final local variable.");
        }
        expression();
        emitBytes(setOp, (uint8_t)arg);
    } else {
        emitBytes(getOp, (uint8_t)arg);
    }
}

// Parses a variable by it's identifier and for retreival of it's value.
static void variable(bool canAssign) {
    namedVariable(parser.previous, canAssign);
}

// Parsing function for unary negation.
static void unary(bool canAssign) {
    TokenType operatorType = parser.previous.type;

    parsePrecedence(PREC_UNARY);

    switch(operatorType) {
        case TOKEN_BANG: emitByte(OP_NOT); break;
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return;
    }
}

// ParseRules table.
ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
    [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
    [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
    [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
    [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
    [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
    [TOKEN_BANG]          = {unary,     NULL,   PREC_NONE},
    [TOKEN_BANG_EQUAL]    = {NULL,     binary,   PREC_EQUALITY},
    [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EQUAL_EQUAL]   = {NULL,     binary,   PREC_EQUALITY},
    [TOKEN_GREATER]       = {NULL,     binary,   PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL,     binary,   PREC_COMPARISON},
    [TOKEN_LESS]          = {NULL,     binary,   PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]    = {NULL,     binary,   PREC_COMPARISON},
    [TOKEN_IDENTIFIER]    = {variable,     NULL,   PREC_NONE},
    [TOKEN_STRING]        = {string,     NULL,   PREC_NONE},
    [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
    [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
    [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FALSE]         = {literal,     NULL,   PREC_NONE},
    [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
    [TOKEN_NIL]           = {literal,     NULL,   PREC_NONE},
    [TOKEN_OR]            = {NULL,     or_,   PREC_OR},
    [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
    [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
    [TOKEN_TRUE]          = {literal,     NULL,   PREC_NONE},
    [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
    [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
    [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

// Starts at the current token and parses any expression at given precedence level or higher.
static void parsePrecedence(Precedence precedence) {
    advance();
    ParseFn prefixRule = getRule(parser.previous.type)->prefix;
    if (prefixRule == NULL) {
        error("Expect expression");
        return;
    }

    // To tell the variable() function that the precedence is low enough to allow assignment. (i.e not right hand side of infix operator or the operand of a unary operator.)
    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(canAssign);

    while (precedence <= getRule(parser.current.type)->precedence) {
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule(canAssign);
    }

    // Try for a + b = c * d if confused as to why canAssign field is checked if true. The outer level check ensures context is present.
    if (canAssign && match(TOKEN_EQUAL)) {
        error("Invalid assignment target.");
    }
}

// Returns a pointer to the rule of the specified type.
// Handles the declaration cycle.
ParseRule* getRule(TokenType type) {
    return &rules[type];
}

// Parses any expression of precedence >= assignment.
static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

// Parses a block statement.
static void block() {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
        declaration();
    }

    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

// Helper function to parse a function, write body bytecode to ObjFunction's chunk, and push the created function object on to the constant pool.
// Emits instructions to push the function object onto the stack.
static void function(FunctionType type) {
    Compiler compiler;
    initCompiler(&compiler, type);
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    // Each parameter is a local variable in the outermost lexical scope of the funciton body.
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) {
                error("Can't have more than 255 parameters.");
            }
            uint8_t constant = parseVariable("Expect parameter name", false);
            defineVariable(constant, false);
        } while (match(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block();

    // No end block because the compiler ends, deleting the function specific locals array, thus making endScope redundant.
    // Does not affect the execution, because the function object now contains the bytecode for accessing locals with relative offset.
    ObjFunction* function = endCompiler();
    // Stored as a constant in the surrounding function's constant pool. Tells the VM to wrap the ObjFunction with ObjClosure.
    emitBytes(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

    // For every closure, all the upvalues captured are emitted.
    // Specifies the type of upvalue and the local slot/upvalue index.
    for (int i = 0; i < function->upvalueCount; i++) {
        emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
        emitByte(compiler.upvalues[i].index);
    }
}


// Parses a function declaration.
static void funDeclaration() {
    uint8_t global = parseVariable("Expect function name.", false);
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global, false);
}


// Parses a variable declaration and generates code for defining global variables
// Challenge EDIT: Added support for initializing single assignment variables(local and global).
static void varDeclaration(bool isFinal) {
    uint8_t global = parseVariable("Expect variable name.", isFinal);

    if (match(TOKEN_EQUAL)) {
        expression();
    } else {
        if (isFinal) {
            error("Final variable must have an initializer.");
        }
        emitByte(OP_NIL);
    }
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    defineVariable(global, isFinal);
}

// Parses an expression statement.
static void expressionStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression." );
    emitByte(OP_POP);
}

// Parses a for statement, desugaring it into while and block statements. Adds control flow as well.
// Since this is a single pass compilation (usually languages have an AST generation and a code generation pass), no restructuring possible, increasing complexity.
static void forStatement() {
    // Regulator: Declared variables are scoped to the loop body.
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    // Initializer clause.
    if (match(TOKEN_SEMICOLON)) {
        // No initializer.
    } else if (match(TOKEN_FINAL)) {
        error("Initializer to for loop cannot be a final variable.");
    } else if (match(TOKEN_VAR)) {
        varDeclaration(false);
    } else {
        expressionStatement(); // Statement to discard the value and consume the semicolon.
    }



    int loopStart = currentChunk()->count;
    // Condition clause.
    int exitJump = - 1;
    if (!match(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // Jump out of the loop if the condition is false.
        exitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }

    // Pretty cool increment clause.
    // Regulator: Weaved in such that it runs AFTER the body using loops. Increment happens at the END of the body.
    // Jumps over the clause code, then the body jumps back to the start of the increment, which is executed and jumps to the top of the for loop.
    // Regulator: Think of loop as jumping back the bytecode, not as actual looping.
    if (!match(TOKEN_RIGHT_PAREN)) {
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;

        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(loopStart);
        loopStart = incrementStart;
        patchJump(bodyJump);
    }

    statement();
    emitLoop(loopStart);
    // Only if there is a condition clause, backpatching and popping the value on the stack happens.
    if (exitJump != -1) {
        patchJump(exitJump);
        emitByte(OP_POP);
    }

    endScope();
}

// Parses an if statement and adds jump bytecode after the condition and the then branch.
// Ensures the condition is popped out of the stack regardless of the branch taken.
static void ifStatement() {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after 'if'.");

    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();

    // Jump the else statement if truthy.
    int elseJump = emitJump(OP_JUMP);

    patchJump(thenJump);
    emitByte(OP_POP);

    if (match(TOKEN_ELSE)) statement();

    patchJump(elseJump);
}

// Parses a print statement.
static void printStatement() {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

// Parses a return statement and it's value.
static void returnStatement() {
    // Lox specifies that returning from top level code is compile error.
    if (current->type == TYPE_SCRIPT) {
        error("Can't return from top-level code.");
    }
    if (match(TOKEN_SEMICOLON)) {
        emitReturn();
    } else {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(OP_RETURN);
    }
}

// Parses a while statement.
static void whileStatement() {
    int loopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(loopStart);

    patchJump(exitJump);
    emitByte(OP_POP);
}


// Parses the case body until the next 'case' or 'default' occurs.
static void caseBody() {
    while (!check(TOKEN_CASE) &&
    !check(TOKEN_DEFAULT) &&
    !check(TOKEN_RIGHT_BRACE) &&
    !check(TOKEN_EOF)) {
        statement();
    }
}

// Challenge EDIT: parses a switch statement.
static void switchStatement() {
    // Expression value is scoped to switch body. 
    beginScope();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after value expression.");

    // Stored string which does not collide with Lox user.
    static const char hiddenSwitchName[] = "<switch value>";

    // Manual creation of hidden variable token. String is stored for the duration of compilation.
    Token hidden = {
        .type = TOKEN_IDENTIFIER,
        .start = hiddenSwitchName,
        .length = sizeof(hiddenSwitchName) - 1,
        .line = parser.previous.line,
    };

    // Adds the hidden switch variable to the locals array and resolves it. No popping of expression(), since it is part of a synthetic var declaration.
    addLocal(hidden, true);
    markInitialized();
    int switchSlot = resolveLocal(current, &hidden);

    consume(TOKEN_LEFT_BRACE, "Expect '{'. before switch cases.");
    
    // Restricting to a maximum of 256 to avoid dynamic array allocation.
    // Multiple variables needed to be stored, therefore an array is used.
    int endJumps[256];
    int endJumpCount = 0;

    int previousFalseJump = -1;
    bool sawDefault = false;

    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {

        // Patching false jumps right before the next case, from the first case onwards.
        if (previousFalseJump != -1) {
            patchJump(previousFalseJump);
            emitByte(OP_POP);
            previousFalseJump = -1;
        }

        if (match(TOKEN_CASE)) {
            if (sawDefault) {
                error("Cannot have a case after default.");
            }
            emitBytes(OP_GET_LOCAL, switchSlot);
            expression();
            consume(TOKEN_COLON, "Expect ':' after case value.");
            emitByte(OP_EQUAL);


            // Jumps case if condition is false.
            previousFalseJump = emitJump(OP_JUMP_IF_FALSE);
            emitByte(OP_POP);

            caseBody();

            // Offset stored in end jump array.
            if (endJumpCount > 255) {
                error("Cannot have more than 256 switch cases.");
            } else {
                endJumps[endJumpCount++] = emitJump(OP_JUMP);
            }

        } else if (match(TOKEN_DEFAULT)) {
            if (sawDefault) {
                error("Cannot have more than one default case.");
            }
            sawDefault = true;

            consume(TOKEN_COLON, "Expect ':' after default.");

            caseBody();
        } else {
            errorAtCurrent("Expect 'case' or 'default' in switch.");
        }   
    }
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after switch cases.");

    // To sidestep the problem of having only a default case in the switch body. previousFalseJump remains -1.
    // In that case, not having a check here leads to trying to patch a non existent jump, fatal.
    if (previousFalseJump != -1) {
        patchJump(previousFalseJump);
        emitByte(OP_POP);
    }

    // Patching every jump for a true case here.
    for (int i = 0; i < endJumpCount; i++) {
        patchJump(endJumps[i]);
    }

    // Pops the hidden switch value.
    endScope();
}

// Synchronizes a parser to the next synchronization point if we are in panic mode.
static void synchronize() {
    parser.panicMode = false;

    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;

            default:
                ; // Do nothing.
        }

        advance();
    }
}

// Parses a single declaration.
// Challenge EDIT: Added support for single assignment variables.
static void declaration() {
    if (match(TOKEN_FUN)) {
        funDeclaration();
    } else if (match(TOKEN_VAR)) {
        varDeclaration(false);
    } else if (match(TOKEN_FINAL)) {
        varDeclaration(true);
    } else {
        statement();
    }

    if (parser.panicMode) synchronize();
}

// Parses a non declaration statement.
static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    } else if (match(TOKEN_FOR)) {
        forStatement();
    } else if (match(TOKEN_IF)) {
        ifStatement();
    } else if (match(TOKEN_RETURN)) {
        returnStatement();
    } else if (match(TOKEN_WHILE)) {
        whileStatement();
    } else if (match(TOKEN_LEFT_BRACE)) {
        beginScope();
        block();
        endScope();
    } else if (match(TOKEN_SWITCH)) {
        switchStatement();
    } else {
        expressionStatement();
    }
}

// Compiles the scanned token into bytecode using Pratt's parser and handles errors.
// Does not take in a chunk anymore, instead creates a top level implicit ObjFunction.
// ObjFunction, a runtime representation, is created at compile time much like other values.
ObjFunction* compile(const char* source) {
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler, TYPE_SCRIPT);

    parser.hadError = false;
    parser.panicMode = false;

    advance();
    while (!match(TOKEN_EOF)) {
        declaration();
    }
    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;  
}