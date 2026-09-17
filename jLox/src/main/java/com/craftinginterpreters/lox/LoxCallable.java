package com.craftinginterpreters.lox;

import java.util.List;

public interface LoxCallable {
//    Checks the arity of the function.
    int arity();
//    Calls the Lox object that can be called as a function given the interpreter and arguments.
    Object call(Interpreter interpreter, List<Object> arguments);
}
