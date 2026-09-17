package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.Map;

public class LoxInstance {
    private LoxClass klass;
    private final Map<String, Object> fields = new HashMap<>();

    LoxInstance(LoxClass klass) {
        this.klass = klass;
    }

//    Returns the property value.
    Object get(Token name) {
        if (fields.containsKey(name.lexeme)) {
            return fields.get(name.lexeme);
        }

//        Accessing methods from the class they are held in.
        LoxFunction method = klass.findMethod(name.lexeme);
//        Creating new environment to bind "this" to the instance when a method is found, to keep resolver and interpreter in sync.
        if (method != null) return method.bind(this);
//          Throw new runtime error because silently returning nil on undefined properties tends to cause bugs.
        throw new RuntimeError(name, "Undefined property '" + name.lexeme + "'.");
    }

//    Sets the field value.
    void set(Token name, Object value) {
        fields.put(name.lexeme, value);
    }

    @Override
    public String toString() {
        return klass.name + " instance";
    }
}
