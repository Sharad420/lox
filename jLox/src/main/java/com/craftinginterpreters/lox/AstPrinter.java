package com.craftinginterpreters.lox;

// Type parameter is baked in, so no need to declare it while instantiation.
// Prints the AST, in preorder traversal.
public class AstPrinter implements Expr.Visitor<String> {
    String print(Expr expr) {
        return expr.accept(this);
    }

    @Override
    public String visitBinaryExpr(Expr.Binary expr) {
        return parenthesize(expr.operator.lexeme, expr.left, expr.right);
    }

    @Override
    public String visitCallExpr(Expr.Call expr) {
        return parenthesize("call " + expr.callee.accept(this),
                expr.arguments.toArray(new Expr[0]));
    }

    @Override
    public String visitGetExpr(Expr.Get expr) {
        return parenthesize("." + expr.name.lexeme, expr.object);
    }

    @Override
    public String visitSetExpr(Expr.Set expr) {
        return parenthesize("= " + expr.name.lexeme, expr.value);
    }

    @Override
    public String visitThisExpr(Expr.This expr) {
        return expr.keyword.lexeme;
    }

    @Override
    public String visitGroupingExpr(Expr.Grouping expr) {
        return parenthesize("group", expr.expression);
    }

    @Override
    public String visitLiteralExpr(Expr.Literal expr) {
        if (expr.value == null) return "nil";
        return expr.value.toString();
    }

    @Override
    public String visitLogicalExpr(Expr.Logical expr) {
        return parenthesize(expr.operator.lexeme, expr.left, expr.right);
    }

    @Override
    public String visitUnaryExpr(Expr.Unary expr) {
        return parenthesize(expr.operator.lexeme, expr.right);
    }

    @Override
    public String visitVariableExpr(Expr.Variable expr) {
        return expr.name.lexeme;
    }

    @Override
    public String visitAssignExpr(Expr.Assign expr) {
        return parenthesize("= " + expr.name.lexeme, expr.value);
    }

    @Override
    public String visitLambdaExpr(Expr.Lambda expr) {
        return "lambda";
    }

    @Override
    public String visitSuperExpr(Expr.Super expr) {
        return ("." + expr.keyword.lexeme + ", " + expr.method.lexeme);
    }

    //    Recursively prints the parenthesized, preorder traversal of the expression provided.
    private String parenthesize(String name, Expr... exprs) {
        StringBuilder builder = new StringBuilder();

        builder.append("(").append(name);
        for (Expr expr : exprs) {
//            Causes the recursion.
            builder.append(" ").append(expr.accept(this));
        }
        builder.append(")");
        return builder.toString();
    }

    public static void main(String[] args) {
//        Creating an Expression, using syntactic grammar.
        Expr expr = new Expr.Binary(
                new Expr.Unary(
                        new Token(TokenType.MINUS, "-", null, 1),
                        new Expr.Literal(123)
                ),
                new Token(TokenType.STAR, "*", null, 1),
                new Expr.Grouping(
                        new Expr.Literal(45.67)
                )
        );
        System.out.println(new AstPrinter().print(expr));
    }
}
