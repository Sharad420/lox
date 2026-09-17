package com.craftinginterpreters.lox;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Stack;

// Semantic analysis to resolve variables for static scoping.
// Resolving produces no side effects, and has no control flow.
public class Resolver implements Expr.Visitor<Void>, Stmt.Visitor<Void>{
    private final Interpreter interpreter;
//    The nest of scopes which are nested via a linked list in the interpreter.
    private final Stack<Map<String, Variable>> scopes = new Stack<>();
    private FunctionType currentFunction = FunctionType.NONE;
    private ClassType currentClass = ClassType.NONE;

    Resolver(Interpreter interpreter) {
        this.interpreter = interpreter;
    }

//    Inner classes can use the static keyword so that they do not contain a reference to the enclosing class and do not need the enclosing class to be instantiated to instantiate.
    private static class Variable {
        boolean defined;
        boolean used;
        Token token;

        Variable(Token token) {
            defined = false;
            used = false;
            this.token = token;
        }

        Variable(Token token, boolean defined, boolean used) {
            this.token = token;
            this.defined = defined;
            this.used = used;
        }
    }

//    To handle return statements outside of scope, and initializers in the resolver pass.
    private enum FunctionType {
        NONE,
        FUNCTION,
        INITIALIZER,
        STATIC_METHOD,
        METHOD
    }

//    To handle this keyword outside a class, like FunctionType, by keeping track of a 'stack' of nested class/function types.
    private enum ClassType {
        NONE,
        CLASS,
        SUBCLASS
    }

    void resolve(List<Stmt> statements) {
        for (Stmt statement : statements) {
            resolve(statement);
        }
    }

//    Overloading methods which apply the visitor pattern to the given syntax node.
    private void resolve(Stmt stmt) {
        stmt.accept(this);
    }

    private void resolve(Expr expr) {
        expr.accept(this);
    }


    private void beginScope() {
        scopes.push(new HashMap<String, Variable>());
    }

    private void endScope() {
        Map<String, Variable> scope = scopes.peek();
        for (Variable variable : scope.values()) {
            if (!variable.used) {
                Lox.error(variable.token, "This variable has been defined but never used!");
            }
        }
        scopes.pop();
    }

    private void declare(Token name) {
        if (scopes.empty()) return;

        Map<String, Variable> scope = scopes.peek();
//        Not allowing variable redeclaration in the same LOCAL scope.
        if (scope.containsKey(name.lexeme)) {
            Lox.error(name,
                    "Already a variable with this name in this scope.");
        }
//        Add the uninitialized variable.
        scope.put(name.lexeme, new Variable(name));
    }

//    Marks this variable as initialized.
    private void define(Token name) {
        if (scopes.empty()) return;

        scopes.peek().get(name.lexeme).defined = true;
    }

//    Resolving the function's body.
    private void resolveFunction(Stmt.Function stmt, FunctionType type) {
        FunctionType enclosingFunction = currentFunction;
        currentFunction = type;
        beginScope();
        for (Token param : stmt.params) {
            declare(param);
            define(param);
        }

        resolve(stmt.body);
        endScope();
//        Go back to the enclosing FunctionType after the scope has ended.
        currentFunction = enclosingFunction;
    }

//    resolveFunction for lambda.
    private void resolveFunction(Expr.Lambda expr) {
        beginScope();
        for (Token param : expr.params) {
            declare(param);
            define(param);
        }

        resolve(expr.body);
        endScope();
    }

//    Tells the interpreter the number of scopes 'away' this variable is present in. Since static scope, it cannot change.
    private void resolveLocal(Expr expr, Token name) {
        for (int i = scopes.size() - 1; i >= 0; i--) {
            if (scopes.get(i).containsKey(name.lexeme)) {
                scopes.get(i).get(name.lexeme).used = true;
                interpreter.resolve(expr, scopes.size() - i - 1);
                return;
            }
        }
    }

    @Override
    public Void visitBlockStmt(Stmt.Block stmt) {
        beginScope();
        resolve(stmt.statements);
        endScope();
        return null;
    }

    @Override
    public Void visitClassStmt(Stmt.Class stmt) {
        ClassType enclosingClass = currentClass;
        currentClass = ClassType.CLASS;
        declare(stmt.name);
        define(stmt.name);

//        Resolver pass to check for same subclass.
        if (stmt.superclass != null && stmt.name.lexeme.equals(stmt.superclass.name.lexeme)) {
            Lox.error(stmt.superclass.name, "A class can't inherit from itself.");
        }

//        Usually does nothing because classes are mostly declared at top level, however Lox allows ClassDecl inside blocks too.
        if (stmt.superclass != null) {
            currentClass = ClassType.SUBCLASS;
            resolve(stmt.superclass);
        }

//        To create a new scope surrounding methods and assigning super.
        if (stmt.superclass != null) {
            beginScope();
            scopes.peek().put("super", new Variable(stmt.superclass.name, true, true));
        }

        beginScope();
//        "this" is a special variable that should exist in scope and should not trigger unused variable error, so it must be defined and true.
        scopes.peek().put("this",
                new Variable(
                        new Token(
                                TokenType.THIS, "this", null, stmt.name.line
                        ),
                        true,
                        true
                )
        );

        for (Stmt.Function method : stmt.methods) {
            FunctionType declaration = FunctionType.METHOD;
            if (method.name.lexeme.equals("init")) {
                declaration = FunctionType.INITIALIZER;
            }
            resolveFunction(method, declaration);
        }

        endScope();
//        Outside the scope where 'this' is put.
        for (Stmt.Function method : stmt.staticMethods) {
            FunctionType declaration = FunctionType.METHOD;
            resolveFunction(method, declaration);
        }

        if (stmt.superclass != null) {
            endScope();
        }

        currentClass = enclosingClass;
        return null;
    }

    //    Resolver for function declaration.
    @Override
    public Void visitFunctionStmt(Stmt.Function stmt) {
        declare(stmt.name);
//        Eagerly define before resolving function's body because we want to enable recursion.
//        Unlike variables, which cannot read themselves in their own initializer.
        define(stmt.name);

        resolveFunction(stmt, FunctionType.FUNCTION);
        return null;
    }

    @Override
    public Void visitVarStmt(Stmt.Var stmt) {
        declare(stmt.name);
        if (stmt.initializer != null) {
            resolve(stmt.initializer);
        }
        define(stmt.name);
        return null;
    }

    @Override
    public Void visitLambdaExpr(Expr.Lambda expr) {
        resolveFunction(expr);
        return null;
    }

    //    Remaining statements which do not deal with variables directly.

    @Override
    public Void visitExpressionStmt(Stmt.Expression stmt) {
        resolve(stmt.expression);
        return null;
    }

    @Override
    public Void visitIfStmt(Stmt.If stmt) {
        resolve(stmt.condition);
        resolve(stmt.thenBranch);
        if (stmt.elseBranch != null) resolve(stmt.elseBranch);
        return null;
    }

    @Override
    public Void visitPrintStmt(Stmt.Print stmt) {
        resolve(stmt.expression);
        return null;
    }

//    Handles return statement with semantic analysis to forbid top level return.
    @Override
    public Void visitReturnStmt(Stmt.Return stmt) {
        if (currentFunction == FunctionType.NONE) {
            Lox.error(stmt.keyword, "Can't return from top level code.");
        }
        if (stmt.value != null) {
            if (currentFunction == FunctionType.INITIALIZER) {
                Lox.error(stmt.keyword, "Can't return a value from an initializer.");
            }
            resolve(stmt.value);
        }
        return null;
    }

    @Override
    public Void visitWhileStmt(Stmt.While stmt) {
        resolve(stmt.condition);
        resolve(stmt.body);
        return null;
    }

//  Small bug in Nystrom's program. Does not check if the variable is actually in the local scope. Added a check. The boolean check worked but now for Variable it throws a NullPointerException.
    @Override
    public Void visitVariableExpr(Expr.Variable expr) {
//        If variable exists in scope and is not initialized, throw error. Read 11.3.2 for why this choice was made.
        if (!scopes.isEmpty() && scopes.peek().containsKey(expr.name.lexeme) && scopes.peek().get(expr.name.lexeme).defined == Boolean.FALSE) {
            Lox.error(expr.name, "Can't read local variable in its own initializer");
        }

        resolveLocal(expr, expr.name);
        return null;
    }

    @Override
    public Void visitAssignExpr(Expr.Assign expr) {
        resolve(expr.value);
        resolveLocal(expr, expr.name);
        return null;
    }

//    Remaining expressions that do not deal with variables directly.


    @Override
    public Void visitBinaryExpr(Expr.Binary expr) {
        resolve(expr.left);
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitCallExpr(Expr.Call expr) {
        resolve(expr.callee);

        for (Expr argument : expr.arguments) {
            resolve(argument);
        }
        return null;
    }

//    Property lookup is dynamic, so there is no resolution.
    @Override
    public Void visitGetExpr(Expr.Get expr) {
        resolve(expr.object);
        return null;
    }

    @Override
    public Void visitSetExpr(Expr.Set expr) {
        resolve(expr.value);
        resolve(expr.object);
        return null;
    }

//    Superclass lookup is dynamic, so there is no resolution.
    @Override
    public Void visitSuperExpr(Expr.Super expr) {
        if (currentClass == ClassType.NONE) {
            Lox.error(expr.keyword, "Can't use 'super' outside of a class.");
        }

        if (currentClass != ClassType.SUBCLASS) {
            Lox.error(expr.keyword, "Can't use 'super' in a class with no superclass.");
        }
        resolveLocal(expr, expr.keyword);
        return null;
    }

    @Override
    public Void visitThisExpr(Expr.This expr) {
        if (currentClass == ClassType.NONE) {
            Lox.error(expr.keyword, "Can't use 'this' outside of a class.");
            return null;
        }

        if (currentFunction == FunctionType.STATIC_METHOD) {
            Lox.error(expr.keyword, "Can't use 'this' in a static method.");
        }
        resolveLocal(expr, expr.keyword);
        return null;
    }

    @Override
    public Void visitGroupingExpr(Expr.Grouping expr) {
        resolve(expr.expression);
        return null;
    }

//    Literals contain no variables and no subexpressions so there is nothing to resolve.
    @Override
    public Void visitLiteralExpr(Expr.Literal expr) {
        return null;
    }

    @Override
    public Void visitLogicalExpr(Expr.Logical expr) {
        resolve(expr.left);
        resolve(expr.right);
        return null;
    }

    @Override
    public Void visitUnaryExpr(Expr.Unary expr) {
        resolve(expr.right);
        return null;
    }
}
