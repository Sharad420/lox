package com.craftinginterpreters.lox;

import java.util.List;

public class LoxLambda implements LoxCallable {
    private final Expr.Lambda lambdaDecl;

    private final Environment closure;

    public LoxLambda(Expr.Lambda lambdaDecl, Environment closure) {
        this.lambdaDecl = lambdaDecl;
        this.closure = closure;
    }

    @Override
    public int arity() {
        return lambdaDecl.params.size();
    }

    @Override
    public Object call(Interpreter interpreter, List<Object> arguments) {
        Environment environment = new Environment(closure);
        for (int i = 0; i < lambdaDecl.params.size(); i++) {
            environment.define(lambdaDecl.params.get(i).lexeme, arguments.get(i));
        }

        try {
            interpreter.executeBlock(lambdaDecl.body, environment);
        } catch (Return returnValue) {
            return returnValue.value;
        }
        return null;
    }

    public String toString() {
        return "<lambda fn>";
    }
}
