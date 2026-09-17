package com.craftinginterpreters.lox;

import java.util.List;

public class LoxFunction implements LoxCallable {
    private final Stmt.Function declaration;
//    To hold on to the surrounding variables where the function is declared.
    private final Environment closure;
//    To ensure an instance is ALWAYS returned when an intializer is called.
    private final boolean isInitializer;

    LoxFunction(Stmt.Function declaration, Environment closure, boolean isInitializer) {
        this.closure = closure;
        this.declaration = declaration;
        this.isInitializer = isInitializer;
    }

    @Override
    public int arity() {
        return declaration.params.size();
    }

    @Override
    public Object call(Interpreter interpreter, List<Object> arguments) {
//        Each function call gets it's own env, otherwise recursion would break.
//        Closures are basically structures which "closes over" and therefore holds a reference to the surrounding variables where the function has been DECLARED i.e the count function in the example.
//        Read 10.6 for more.
        Environment environment = new Environment(closure);
        for (int i = 0; i < declaration.params.size(); i++) {
            environment.define(declaration.params.get(i).lexeme, arguments.get(i));
        }

        try {
            interpreter.executeBlock(declaration.body, environment);
        } catch (Return returnValue) {
            if (isInitializer) return closure.getAt(0, "this");

            return returnValue.value;
        }

        if (isInitializer) return closure.getAt(0, "this");
        return null;
    }

//    Binds "this" to the instance in a new environment and returns a LoxFunction with this env as the closure.
    LoxFunction bind(LoxInstance instance) {
        Environment environment = new Environment(closure);
        environment.define("this", instance);
        return new LoxFunction(declaration, environment, isInitializer);
    }

//    Returns the getter boolean.
    public boolean isGetter() {
        return this.declaration.isGetter;
    }

    @Override
    public String toString() {
        return "<fn " + declaration.name.lexeme + ">";
    }
}
