package com.craftinginterpreters.lox;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Scanner;

public class Interpreter implements Expr.Visitor<Object>, Stmt.Visitor<Void> {
//    Reference to global environment.
    final Environment globals = new Environment();
//    Reference to current environment.
    private Environment environment = globals;
//    Stores the enclosing static scope of each variable node.
    private final Map<Expr, Integer> locals = new HashMap<>();
//    Scanner for reading inputs. (Standard library function added by Sharad.)
    private final Scanner scanner = new Scanner(System.in);

    Interpreter() {
//        Defines a LoxCallable named clock, implemented by a Java anonymous class.
        globals.define("clock", new LoxCallable() {
            @Override
            public int arity() {
                return 0;
            }

            @Override
            public Object call(Interpreter interpreter, List<Object> arguments) {
                return (double)System.currentTimeMillis() / 1000.0;
            }

            @Override
            public String toString() {
                return "<native fn>";
            }
        });
//        Can add other native functions here.
        globals.define("type", new LoxCallable() {
            @Override
            public int arity() {
                return 1;
            }

            @Override
            public Object call(Interpreter interpreter, List<Object> arguments) {
                Object value = arguments.getFirst();
                return switch (value) {
                    case null -> "nil";
                    case Double v -> "number";
                    case String s -> "string";
                    case Boolean b -> "bool";
                    case LoxInstance loxInstance -> "instance";
                    case LoxClass loxClass -> "class";
                    case LoxCallable loxCallable -> "function";
                    default -> "unknown type";
                };
            }

            @Override
            public String toString() {
                return "<native fn>";
            }
        });

        globals.define("input", new LoxCallable() {
            @Override
            public int arity() {
                return 0;
            }

            @Override
            public Object call(Interpreter interpreter, List<Object> arguments) {
                return scanner.nextLine();
            }

            @Override
            public String toString() {
                return "<native fn>";
            }
        });

        globals.define("readFile", new LoxCallable() {
            @Override
            public int arity() {
                return 1;
            }

            @Override
            public Object call(Interpreter interpreter, List<Object> arguments) {
                Object arg = arguments.getFirst();

                if (!(arg instanceof String)) {
                    throw new RuntimeError("readFile() expects a string.");
                }
                String path = (String)arg;
                try {
                    String content = Files.readString(Path.of(path));
                    return content;
                } catch (IOException e) {
                    throw new RuntimeError("Could not read file '" + path + "': " + e.getMessage());
                }
            }
        });

    }

//    Public API for Interpreter.
    void interpret (List<Stmt> statements) {
        try {
            for (Stmt statement : statements) {
                execute(statement);
            }
        } catch (RuntimeError error) {
//            Catches the exception here.
            Lox.runtimeError(error);
        }
    }


    @Override
    public Object visitLiteralExpr(Expr.Literal expr) {
        return expr.value;
    }

    @Override
    public Object visitGroupingExpr(Expr.Grouping expr) {
        return evaluate(expr.expression);
    }

    @Override
    public Object visitUnaryExpr(Expr.Unary expr) {
        Object right = evaluate(expr.right);

        switch(expr.operator.type) {
            case MINUS:
                checkNumberOperand(expr.operator, right);
                return -(double)right;
            case BANG:
                return !isTruthy(right);
        }
//        Unreachable.
        return null;
    }

    private void checkNumberOperand(Token operator, Object operand) {
        if (operand instanceof Double) return;
        throw new RuntimeError(operator, "Operand must be a munber.");
    }

//    Handles the variable AST via static scoping and the resolver.
    @Override
    public Object visitVariableExpr(Expr.Variable expr) {
        return lookUpVariable(expr.name, expr);
    }

    private Object lookUpVariable(Token name, Expr expr) {
        Integer distance = locals.get(expr);
        if (distance != null) {
            return environment.getAt(distance, name.lexeme);
        }
        return globals.get(name);
    }

    @Override
    public Object visitBinaryExpr(Expr.Binary expr) {
        Object left = evaluate(expr.left);
        Object right = evaluate(expr.right);

        switch(expr.operator.type) {
            case MINUS:
                checkNumberOperands(expr.operator, left, right);
                return (double)left - (double)right;
            case SLASH:
                checkNumberOperands(expr.operator, left, right);
                return (double)left / (double)right;
            case STAR:
                checkNumberOperands(expr.operator, left, right);
                return (double)left * (double)right;
            case PLUS:
//                Dynamically check the type and then choose the appropriate operation for +, instead of implicitly assuming.
                if (left instanceof Double && right instanceof Double) {
                    return (double)left + (double)right;
                }

                if (left instanceof String && right instanceof String) {
                    return (String)left + (String)right;
                }

                throw new RuntimeError(expr.operator, "Operands must be two numbers or two strings.");
            case GREATER:
                checkNumberOperands(expr.operator, left, right);
                return (double)left > (double)right;
            case GREATER_EQUAL:
                checkNumberOperands(expr.operator, left, right);
                return (double)left >= (double)right;
            case LESS:
                checkNumberOperands(expr.operator, left, right);
                return (double)left < (double)right;
            case LESS_EQUAL:
                checkNumberOperands(expr.operator, left, right);
                return (double)left <= (double)right;
//                Equality operator supports operands of any kind.
            case BANG_EQUAL:
                return !isEqual(left, right);
            case EQUAL_EQUAL:
                return isEqual(left, right);
        }

        return null;
    }

    private void checkNumberOperands(Token operator, Object left, Object right) {
        if (left instanceof Double && right instanceof Double) return;

        throw new RuntimeError(operator, "Operands must be the same.");
    }

    @Override
    public Object visitCallExpr(Expr.Call expr) {
        Object callee = evaluate(expr.callee);

        List<Object> arguments = new ArrayList<>();
        for (Expr argument : expr.arguments) {
            arguments.add(evaluate(argument));
        }

//        To ensure only a callable callee is present, not something like a Lox String.
        if (!(callee instanceof LoxCallable)) {
            throw new RuntimeError(expr.paren, "Can call only functions and classes.");
        }

        LoxCallable function = (LoxCallable)callee;
        if (arguments.size() != function.arity()) {
            throw new RuntimeError(expr.paren, "Expected " + function.arity() + " arguments but got " + arguments.size() + ".");
        }
        return function.call(this, arguments);
    }

    @Override
    public Object visitGetExpr(Expr.Get expr) {
        Object object = evaluate(expr.object);
        if(object instanceof LoxInstance) {
            Object value = (((LoxInstance)object).get(expr.name));
            if (value instanceof LoxFunction) {
//                Already bound LoxFunction is returned.
                LoxFunction function = (LoxFunction) value;
//                Getter is directly called.
                if (function.isGetter()) return function.call(this, List.of());
            }
            return value;
        }

        if (object instanceof LoxClass) {
            return (((LoxClass)object).findStaticMethod(expr.name));
        }

        throw new RuntimeError(expr.name, "Only instances have properties.");
    }

    @Override
    public Object visitSetExpr(Expr.Set expr) {
        Object object = evaluate(expr.object);
        if (!(object instanceof LoxInstance)) {
            throw new RuntimeError(expr.name, "Only instances have fields");
        }

        Object value = evaluate(expr.value);
        ((LoxInstance)object).set(expr.name, value);
        return value;
    }

    @Override
    public Object visitSuperExpr(Expr.Super expr) {
        int distance = locals.get(expr);
        LoxClass superclass = (LoxClass)environment.getAt(distance, "super");
//        Get the "this" reference that the method from the superclass is invoked on i.e the object.
        LoxInstance object = (LoxInstance) environment.getAt(distance - 1, "this");
        LoxFunction method = superclass.findMethod(expr.method.lexeme);
        if (method == null) {
            throw new RuntimeError(expr.method, "Undefined property '" + expr.method.lexeme + "'.");
        }
        return method.bind(object);
    }

    @Override
    public Object visitThisExpr(Expr.This expr) {
        return lookUpVariable(expr.keyword, expr);

    }

    @Override
    public Object visitLambdaExpr(Expr.Lambda expr) {
        LoxLambda lambda = new LoxLambda(expr, environment);
        return lambda;
    }

//    Makes use of resolver for assign AST node.
    @Override
    public Object visitAssignExpr(Expr.Assign expr) {
        Object value = evaluate(expr.value);

        Integer distance = locals.get(expr);
        if (distance != null) {
            environment.assignAt(distance, expr.name, value);
        } else {
            globals.assign(expr.name, value);
        }
        return value;
    }

    @Override
    public Object visitLogicalExpr(Expr.Logical expr) {
        Object left = evaluate(expr.left);

        if (expr.operator.type == TokenType.OR) {
            if (isTruthy(left)) return left;
        } else {
            if (!isTruthy(left)) return left;
        }

//        Works because only nil and false are falsey, which this will anyway return if there is no short circuiting.
        return evaluate(expr.right);
    }



//    Recursively evaluates the expression inside the Expr.Grouping object.
    private Object evaluate(Expr expr) {
        return expr.accept(this);
    }

//    Following the truthy rules of a dynamically typed language(Ruby)
//    If false or nil, it's falsey, else truthy.
    private boolean isTruthy(Object object) {
        if (object == null) return false;
        if (object instanceof Boolean) return (boolean)object;
        return true;
    }

//    Lox follows Java's notion of equality.
    private boolean isEqual(Object a, Object b) {
        if (a == null && b == null) return true;
        if (a == null) return false;

        return a.equals(b);
    }

//    Stringifies the object returned after interpretation.
    private String stringify(Object object) {
        if (object == null) return "nil";

//        Lox does not have floating points.
        if (object instanceof Double) {
            String text = object.toString();
            if (text.endsWith(".0")) {
                text = text.substring(0, text.length() - 2);
            }
            return text;
        }

        return object.toString();
    }

    @Override
    public Void visitExpressionStmt(Stmt.Expression stmt) {
        evaluate(stmt.expression);
//        Null needed to satisfy Void.
        return null;
    }

//    Handles Function declaration.
    @Override
    public Void visitFunctionStmt(Stmt.Function stmt) {
        LoxFunction function = new LoxFunction(stmt, environment, false);
        environment.define(stmt.name.lexeme, function);
        return null;
    }

//    Handles class declaration. Two step binding exists here because we want to allow references to the class during the binding of closures to the methods inside the class.
//    The closures that the methods contain while being cast to a Lox Callable would not contain a reference to the class if this didn't happen.
    @Override
    public Void visitClassStmt(Stmt.Class stmt) {
        Object superclass = null;
        if (stmt.superclass != null) {
            superclass = evaluate(stmt.superclass);
            if (!(superclass instanceof LoxClass)) {
                throw new RuntimeError(stmt.superclass.name, "Superclass must be a class.");
            }
        }

        environment.define(stmt.name.lexeme, null);
        if (stmt.superclass != null) {
            environment = new Environment(environment);
            environment.define("super", superclass);
        }
        Map<String, LoxFunction> methods = new HashMap<>();
        Map<String, LoxFunction> staticMethods = new HashMap<>();
        for (Stmt.Function method : stmt.methods) {
            LoxFunction function = new LoxFunction(method, environment, method.name.lexeme.equals("init"));
            methods.put(method.name.lexeme, function);
        }
        for (Stmt.Function staticMethod : stmt.staticMethods) {
            LoxFunction function = new LoxFunction(staticMethod, environment, false);
            staticMethods.put(staticMethod.name.lexeme, function);
        }
        LoxClass klass = new LoxClass(stmt.name.lexeme, (LoxClass)superclass, methods, staticMethods);
        if (superclass != null) {
            environment = environment.enclosing;
        }
        environment.assign(stmt.name, klass);
        return null;
    }

    //    Conditional control flow.
    @Override
    public Void visitIfStmt(Stmt.If stmt) {
        if (isTruthy(evaluate(stmt.condition))) {
            execute(stmt.thenBranch);
        } else if (stmt.elseBranch != null) {
            execute(stmt.elseBranch);
        }
        return null;
    }

    @Override
    public Void visitWhileStmt(Stmt.While stmt) {
        while (isTruthy(evaluate(stmt.condition))) {
            execute(stmt.body);
        }
        return null;
    }

    @Override
    public Void visitPrintStmt(Stmt.Print stmt) {
        Object value = evaluate(stmt.expression);
        System.out.println(stringify(value));
        return null;
    }

    @Override
    public Void visitReturnStmt(Stmt.Return stmt) {
        Object value = null;
        if (stmt.value != null) value = evaluate(stmt.value);

        throw new Return(value);
    }

    @Override
    public Void visitVarStmt(Stmt.Var stmt) {
        Object value = null;
        if (stmt.initializer != null) {
            value = evaluate(stmt.initializer);
        }
        environment.define(stmt.name.lexeme, value);
        return null;
    }

    @Override
    public Void visitBlockStmt(Stmt.Block stmt) {
        executeBlock(stmt.statements, new Environment(environment));
        return null;
    }

//    Changes the interpreters environment to the current one and interprets the block.
    void executeBlock(List<Stmt> statements, Environment environment) {
        Environment previous = this.environment;
//         Good practice to restore prev environment using finally clause.
        try {
            this.environment = environment;

            for (Stmt statement : statements) {
                execute(statement);
            }
        } finally {
            this.environment = previous;
        }
    }


//    Analogous to execute.
    public void execute(Stmt stmt) {
        stmt.accept(this);
    }

//    Resolver hands over the number of scopes away a certain variable is.
    public void resolve(Expr expr, int depth) {
        locals.put(expr, depth);
    }


}
