package com.craftinginterpreters.lox;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class Lox {
    private static final Interpreter interpreter = new Interpreter();
//    Error checker.
    static boolean hadError = false;
    static boolean hadRuntimeError = false;

//    Executes file else runs REPL.
    public static void main(String[] args) throws IOException {
        if (args.length > 1) {
            System.out.println("Usage: jlox [script]");
            System.exit(64);
        } else if (args.length == 1) {
            runFile(args[0]);
        } else {
            runPrompt();
        }
    }

    private static void runFile(String path) throws IOException {
        byte[] bytes = Files.readAllBytes((Paths.get(path)));
        run(new String(bytes, Charset.defaultCharset()));

//        Indicate an error in the exit code.
        if (hadError) System.exit(65);
        if (hadRuntimeError) System.exit(70);
    }


    private static void runPrompt() throws IOException {
        InputStreamReader input = new InputStreamReader(System.in);
        BufferedReader reader = new BufferedReader(input);

        for (;;) {
            System.out.print("> ");
            String line = reader.readLine();
//            Ctrl + D returns null.
            if (line == null) break;
            run(line);
//            Reset flag in interactive loop.
            hadError = false;
//            Does not care about runtime error since it should just loop around and let user input enw code.
        }
    }

    private static void run(String source) {
        Scanner scanner = new Scanner(source);
        List<Token> tokens = scanner.scanTokens();

        Parser parser = new Parser(tokens);
        List<Stmt> statements = parser.parse();

        if (hadError) return;

        Resolver resolver = new Resolver(interpreter);
        resolver.resolve(statements);

//        Stop if there is a resolution error.
        if (hadError) return;

//        System.out.println(new AstPrinter().print(expression));

        interpreter.interpret(statements);
    }

//    Error handling
    static void error(int line, String message) {
        report(line, "", message);
    }

    static void error(Token token, String message) {
        if (token.type == TokenType.EOF) {
            report(token.line, " at end", message);
        } else {
            report(token.line, " at '" + token.lexeme + "'", message);
        }
    }

    static void runtimeError(RuntimeError error) {
//        Tradeoff for reporting errors on native functions: Cannot access line number. Can introduce a synthetic token, but feels unnatural.
        if (error.token != null) {
            System.err.println(error.getMessage() + "\n[line " + error.token.line + " ]");
        } else {
            System.err.println(error.getMessage());
        }
        hadRuntimeError = true;
    }

    private static void report(int line, String where, String message) {
        System.err.println(
                "[line " + line + "] Error" + where + ": " + message
        );
        hadError = true;
    }
}
