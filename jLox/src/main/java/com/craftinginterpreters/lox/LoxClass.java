package com.craftinginterpreters.lox;

import java.util.List;
import java.util.Map;

// Since it has been decided to create a new instance by directly calling the class instead of using a keyword like new.
public class LoxClass implements LoxCallable {
    final String name;
    final LoxClass superclass;
    private final Map<String, LoxFunction> methods;
    private final Map<String, LoxFunction> staticMethods;

    LoxClass(String name, LoxClass superclass, Map<String, LoxFunction> methods, Map<String, LoxFunction> staticMethods) {
        this.name = name;
        this.superclass = superclass;
        this.methods = methods;
        this.staticMethods = staticMethods;
    }

    @Override
    public Object call(Interpreter interpreter, List<Object> arguments) {
        LoxInstance instance = new LoxInstance(this);
        LoxFunction initializer = findMethod("init");
        if (initializer != null) {
//            If an initializer is found, bind it to the instance and invoke it like a normal method call.
            initializer.bind(instance).call(interpreter, arguments);
        }
        return instance;
    }

    @Override
    public int arity() {
        LoxFunction initializer = findMethod("init");
        if (initializer == null) return 0;
        return initializer.arity();
    }

    LoxFunction findMethod(String name) {
        if (methods.containsKey(name)) {
            return methods.get(name);
        }

        if (superclass != null) {
            return superclass.findMethod(name);
        }
        return null;
    }

//    Can use metaclasses, but went for simpler implementation. 'super.' cannot access static methods, because 'super.method', is invoked on an instance.
    LoxFunction findStaticMethod(Token name) {
        if (staticMethods.containsKey(name.lexeme)) {
            return staticMethods.get(name.lexeme);
        }
        return null;
    }

    @Override
    public String toString() {
        return name;
    }
}
