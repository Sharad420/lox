package com.craftinginterpreters.lox;

public class Return extends RuntimeException {
    final Object value;

    public Return(Object value) {
//        Constructor to basically handle some JVM machinery.
        super(null, null, false, false);
        this.value = value;
    }
}
