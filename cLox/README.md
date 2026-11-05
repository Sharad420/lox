# clox

A bytecode virtual machine for Lox, built following *Crafting Interpreters*, with extensions.

## Design notes

### Pipeline

Source → scanner → tokens → compiler → bytecode → VM.

- The scanner produces tokens on demand, not as a full list.
- Single-pass compiler: a Pratt parser emits bytecode directly, with no AST. Simpler and lighter in C.
- Limit: the compiler only sees code it has already read. That's fine for a small, dynamically typed language like Lox.

### Strings and hash tables

- All strings are interned, so string equality is a pointer compare. This makes name lookups fast.
- Hash tables use FNV-1a, open addressing with linear probing, and grow at a 0.75 load factor.
- Deletes leave tombstones so probe chains stay intact.

### Variables

- Globals live in a hash table and are looked up by name at runtime, so a function can use a global declared after it.
- Final globals are also tracked in `vm.finalGlobals`. Assigning to one is a runtime error.
- Locals are resolved to stack slots at compile time. The compiler's locals array mirrors the stack.
- The innermost declaration wins. Slot 0 holds the callee, or `this` in methods.

### Control flow

- Forward jumps are backpatched once the target is known. Offsets are 16 bits.
- `for` loops compile the increment before the body, so the code jumps over it to the body, then loops back to it.
- `switch` stores its subject in a hidden local and tests each case in order.
- No fallthrough and no `break`. `default` must be last.
- Declarations in a case body must be inside a block, as in C.

### Functions and natives

- Functions are first-class values, each compiled to its own chunk.
- A call frame is a window on the shared stack: slot 0 is the callee, then the arguments. Nothing is copied.
- Calls don't recurse in C. `run()` switches frames and keeps looping. Max depth is 64 frames.
- `ip` is cached in a local in `run()` and saved to the frame before calls and errors.
- Natives are `bool fn(int argCount, Value* args)`. They write the result, or an error message, to `args[-1]`. `callValue` raises the error, never the native.
- Natives: `clock`, `type`, `input`, `num`, `str`, `len`, `readFile`, plus field access and list helpers.

### Closures and upvalues

- Every function value is a closure, even if it captures nothing.
- Captured variables are resolved at compile time through the enclosing compilers.
- An open upvalue points at the variable's stack slot. When the variable leaves scope, its value moves into the upvalue (closed).
- Open upvalues are kept in a list sorted by slot, so closures capturing the same variable share one upvalue.

### Garbage collection

- Mark and sweep. Roots: the stack, call frame closures, open upvalues, globals, functions being compiled, and `initString`.
- Marking uses a gray stack instead of recursion.
- The string table is weak: unmarked strings are removed from it before the sweep.
- The next collection runs when the heap reaches twice what survived the last one.
- A new object must be reachable, usually by pushing it on the stack, before the next allocation.

### Runtime errors

- Line numbers are run-length encoded. Lookup is slow, but only needed on errors.
- Stack traces print one line per frame from each frame's saved `ip`.
- Errors in `run()` go through `RUNTIME_ERROR`, which saves the cached `ip` first. Without it, the top frame's line is wrong.
- Natives return false instead of calling `runtimeError`, because it resets the stack.

### Numbers and operators

- Values are NaN-boxed: doubles, with nil, bools and object pointers packed into quiet NaN bits.
- `valuesEqual` compares numbers as doubles, so NaN != NaN.
- `%` uses `fmod` and has the same precedence as `*`. The result takes the left operand's sign, as in C. `x % 0` is NaN, while `x / 0` is inf.
- `**` uses `pow` and is right associative. It binds tighter than `*` but looser than unary minus, so `-2 ** 2` is 4.

### Classes

- Methods are copied down from the superclass at declaration, so method lookup is one table lookup.
- `super` is a hidden local captured as an upvalue.
- `OP_INVOKE` calls a method without creating a bound method.
- `init` always returns `this`.

#### Idea: BETA-style `inner`

- A call runs the highest definition in the chain; `inner` runs the next one down, or does nothing.
- Subclass methods don't overwrite inherited ones; each class keeps its own methods in a second table.
- `inner` walks up from `this`'s class to the method's class and picks the nearest class that defines the name.
- Needs a superclass pointer on each class, marked by the GC.

### Fixed limits

Most limits come from one-byte operands; jumps use two bytes.

| Limit | Maximum | Why |
| --- | --- | --- |
| Constants per chunk | 256 | `OP_CONSTANT` has a one-byte index |
| Locals in scope per function | 256 | one-byte slot in `OP_GET_LOCAL` and `OP_SET_LOCAL` |
| Upvalues per function | 256 | one-byte index in `OP_CLOSURE` and the upvalue instructions |
| Arguments or parameters | 255 | one-byte count; slot 0 holds the callee |
| Items in a list literal | 255 | one-byte count in `OP_LIST` |
| Jump distance | 65,535 bytes | 16-bit jump offset |
| Call depth | 64 frames | `FRAMES_MAX` |
| Value stack | 16,384 slots | `STACK_MAX` = `FRAMES_MAX` × 256 |
| Cases per switch | 256 | fixed-size end-jump array |
