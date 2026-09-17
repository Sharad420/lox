package com.craftinginterpreters.lox;

import java.util.HashMap;

public class Environment {
    private final HashMap<String, Object> values = new HashMap<>();
    final Environment enclosing;

    Environment() {
        enclosing = null;
    }

//    Keeps a reference of enclosing environment, to handle scope.
    Environment(Environment enclosing) {
        this.enclosing = enclosing;
    }

    void define(String name, Object value) {
//        Allowing for redefinition.
        values.put(name, value);
    }

//    Goes up the parent chain and returns the environment present at that distance.
    Environment ancestor(int distance) {
        Environment environment = this;
        for (int i = 0; i < distance; i++) {
            environment = environment.enclosing;
        }
        return environment;
    }

//    Does not need to check if variable exists here, because we're trusting that resolver has done it's job.
    Object getAt(int distance, String name) {
        return ancestor(distance).values.get(name);
    }

    Object get(Token name) {
        if (values.containsKey(name.lexeme)) {
            return values.get(name.lexeme);
        }

//        Recursively checks enclosing envs to get variable.
        if (enclosing != null) {
            return enclosing.get(name);
        }

//        Runtime error to be handle mutual recursions, or more rudimentarily to be able to mention a variable before defining it.
        throw new RuntimeError(name, "Undefined variable '" + name.lexeme + "'");
    }

//    Assignment of a variable.
    void assign(Token name, Object value) {
        if (values.containsKey(name.lexeme)) {
            values.put(name.lexeme, value);
            return;
        }

        if (enclosing != null) {
            enclosing.assign(name, value);
            return;
        }

        throw new RuntimeError(name, "Undefined variable '" + name.lexeme + "'");
    }

//    Again, directly adding because we trust the resolver to be correct.
    void assignAt(int distance, Token name, Object value) {
        ancestor(distance).values.put(name.lexeme, value);
    }
}
