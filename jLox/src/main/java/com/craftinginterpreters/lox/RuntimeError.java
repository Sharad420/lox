package com.craftinginterpreters.lox;


// Naming is a little confusing between error and exception, but all we're doing is throwing the exception and also storing the token.
public class RuntimeError extends RuntimeException {
    final Token token;

    RuntimeError(Token token, String message) {
        super(message);
        this.token = token;
    }

//    For native functions.
    RuntimeError(String message) {
        super(message);
        this.token = null;
    }

}
