# Lox

Two interpreters for Lox, the language from Robert Nystrom's [*Crafting Interpreters*](https://craftinginterpreters.com/).

- [jlox](jlox/README.md): a tree-walking interpreter in Java.
- [clox](clox/README.md): a bytecode virtual machine in C, with extensions.

## The language

Lox is a small, dynamically typed scripting language with C-style syntax, first-class functions, closures and classes with single inheritance.

```lox
fun makeCounter() {
  var count = 0;
  fun increment() {
    count = count + 1;
    return count;
  }
  return increment;
}

var counter = makeCounter();
print counter(); // 1
print counter(); // 2
```

## jlox vs clox

| | jlox | clox |
| --- | --- | --- |
| Language | Java | C |
| Execution | walks the syntax tree | compiles to bytecode, runs on a stack VM |
| Front end | scanner → parser → AST → resolver | single-pass Pratt compiler, no AST |
| Variables | chains of hash-map environments | stack slots, upvalues for captured variables |
| Values | Java objects | NaN-boxed 64-bit values |
| Memory | Java's garbage collector | its own mark-and-sweep collector |
| Method lookup | walks the superclass chain | methods copied down at declaration |
| `NaN == NaN` | true | false |

## clox extensions

Beyond the book:

- Lists, with indexing and `appendToList` / `deleteFromList`.
- Final globals.
- `switch` statements, without fallthrough.
- `%` (modulo) and `**` (exponent, right associative).
- Natives: `input`, `num`, `str`, `len`, `type`, `readFile`, and dynamic field access.
- Run-length encoded line numbers.

## Building and running

**clox:**

```sh
cd clox
make
./clox             # REPL
./clox script.lox  # run a file
```

On Linux, link with `-lm` for `fmod` and `pow`.

**jlox:**

```sh
cd jlox
javac -d out $(find . -name '*.java')
java -cp out com.craftinginterpreters.lox.Lox             # REPL
java -cp out com.craftinginterpreters.lox.Lox script.lox  # run a file
```
