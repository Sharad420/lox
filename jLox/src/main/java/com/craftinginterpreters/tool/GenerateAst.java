package com.craftinginterpreters.tool;

import java.io.IOException;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.List;

// Script to generate expr classes and subclasses(an AST) based on Lox grammar's productions.
public class GenerateAst {
    public static void main(String[] args) throws IOException {
        if (args.length != 1) {
            System.err.println("Usage: generate_ast <output_directory>");
            System.exit(64);
        }
        String outputDir = args[0];

//      Defining Expression Syntax tree
        defineAst(outputDir, "Expr", Arrays.asList(
                "Assign    : Token name, Expr value",
                "Binary    : Expr left, Token operator, Expr right",
                "Call      : Expr callee, Token paren, List<Expr> arguments",
                "Get       : Expr object, Token name",
                "Grouping  : Expr expression",
                "Literal   : Object value",
//                Not under binary because this is technically a control flow construct.
                "Logical   : Expr left, Token operator, Expr right",
                "Set       : Expr object, Token name, Expr value",
//                Getting a method from the superclass; note that it super.method does NOT involve calling the function, just like regular methods.
                "Super     : Token keyword, Token method",
//                This keyword for referencing the object a property is accessed from.
                "This      : Token keyword",
                "Unary     : Token operator, Expr right",
//                Lambda expression.
                "Lambda    : Token lambda, List<Token> params," + " List<Stmt> body",
//                Simple wrapper around token for variable name.
                "Variable  : Token name"
        ));

//        Defining Statement Syntax tree
        defineAst(outputDir, "Stmt", Arrays.asList(
                "Block      : List<Stmt> statements",
                "Class      : Token name, Expr.Variable superclass," + " List<Stmt.Function> methods, List<Stmt.Function> staticMethods",
                "Expression : Expr expression",
                "Function   : Token name, List<Token> params," + " List<Stmt> body," + " boolean isGetter",
                "If         : Expr condition, Stmt thenBranch," + " Stmt elseBranch",
                "While      : Expr condition, Stmt body",
                "Print      : Expr expression",
                "Return     : Token keyword, Expr value",
                "Var        : Token name, Expr initializer"
        ));
    }

    private static void defineAst(String outputDir, String baseName, List<String> types) throws IOException {
        String path = outputDir + "/" + baseName + ".java";
        PrintWriter writer = new PrintWriter(path, "UTF-8");

        writer.println("package com.craftinginterpreters.lox;");
        writer.println();
        writer.println("import java.util.List;");
        writer.println();
        writer.println("abstract class " + baseName + " {");

        defineVisitor(writer, baseName, types);

//        The AST classes.
        for (String type : types) {
            String className = type.split(":")[0].trim();
            String fields = type.split(":")[1].trim();
            defineType(writer, baseName, className, fields);
        }

        writer.println();
        writer.println("    abstract <R> R accept(Visitor<R> visitor);");

        writer.println("}");
        writer.close();
    }

    private static void defineVisitor(PrintWriter writer, String baseName, List<String> types) {
        writer.println("    interface Visitor<R> {");

        for (String type : types) {
            String typeName = type.split(":")[0].trim();
            writer.println("        R visit" + typeName + baseName + "(" + typeName + " " + baseName.toLowerCase() + ");");
        }
        writer.println("    }");
    }

    private static void defineType(PrintWriter writer, String baseName, String className, String fieldList) {
        writer.println("    static class " + className + " extends " + baseName + " {");
//        Fields.
        String[] fields = fieldList.trim().split(",");
        for (String field : fields) {
            writer.println("        final " + field + ";");
        }
//        Constructor.
        writer.println();
        writer.println("        " + className + "(" + fieldList + ") {");

//        Store parameters in fields.

        for (String field : fields) {
            String name = field.trim().split(" ")[1];
            writer.println("            this." + name + " = " + name + ";");
        }
        writer.println("        }");
//        Visitor pattern.
        writer.println();
        writer.println("        @Override");
        writer.println("        <R> R accept(Visitor<R> visitor) {");
        writer.println("            return visitor.visit" + className + baseName + "(this);");
        writer.println("        }");


        writer.println("    }");

    }
}



