#include <stdio.h>
#include <string.h>
#include <sys/syslimits.h>

#include "chunk.h"
#include "common.h"
#include "scanner.h"


// Scanner structure defined here and not in headers since it is used only here.
typedef struct {
    const char* start;
    const char* current;
    int line;
} Scanner;

// Top level static variable created.
Scanner scanner;

// Initializes the scanner.
void initScanner(const char *source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

// Checks if current character is an alpha.
static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

// Checks if current character is a number.
static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

// Checks if scanner is currently pointing to the end of file.
static bool isAtEnd() {
    return *scanner.current == '\0';
}

// Consumes the current character and returns it.
static char advance() {
    scanner.current++;
    return scanner.current[-1]; // *(scanner.current - 1)
}

// Returns the current character but does not consume it.
static char peek() {
    return *scanner.current;
}

// Returnns the next character but does not consume it.
static char peekNext() {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

// Checks if the current character matches the expected character and consumes it if true.
static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

// Constructor-like function to create a token.
Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int)(scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

// Sister function for returning error token. token.start points to a string literal, which exists for the entire duration of the program.
static Token errorToken(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int)strlen(message);
    token.line = scanner.line;
    return token;
}

// Skips whitespace until a non-whitespace character is found.
// Is a seperate function because we do not have a loop in our scanner for clox.
static void skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ' : 
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    // A comment goes until the end of the line. Does not consume new line.
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

// Tests the rest of a potential lexeme and returns the appropriate TokenType.
static TokenType checkKeyword(int start, int length, const char* rest, TokenType type) {
    if (scanner.current - scanner.start == start + length &&
    memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

// Implements a DFA(Trie) to consume and recognize identifiers/keywords. DFA is a very interesting read. 16.4 for more.
// Not the most elegant way to check, but it is quite pragmatic for a language of 16 keywords.
static TokenType identifierType() {
    switch(scanner.start[0]) {
        case 'a' : return checkKeyword(1, 2, "nd", TOKEN_AND);
        case 'c' : 
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a' : return checkKeyword(2, 2, "se", TOKEN_CASE);
                    case 'l' : return checkKeyword(2, 3, "ass", TOKEN_CLASS);
                }
            }
            break;
        case 'd' : return checkKeyword(1, 6, "efault", TOKEN_DEFAULT);
        case 'e' : return checkKeyword(1, 3, "lse", TOKEN_ELSE);
        case 'f' :
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a' : return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'i' : return checkKeyword(2, 3, "nal", TOKEN_FINAL); // EDIT: Checks for final keyword.
                    case 'o' : return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u' : return checkKeyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i' : return checkKeyword(1, 1, "f", TOKEN_IF);
        case 'n' : return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o' : return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'p' : return checkKeyword(1, 4, "rint", TOKEN_PRINT);
        case 'r' : return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
        case 's' : 
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'u' : return checkKeyword(2, 3, "per", TOKEN_SUPER);
                    case 'w' : return checkKeyword(2, 4, "itch", TOKEN_SWITCH);
                }
            }
            break;
        case 't' :
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h' : return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r' : return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                }
            }
            break;
        case 'v' : return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w' : return checkKeyword(1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

// Consumes an identifier/keyword and returns a token.
static Token identifier() {
    while(isAlpha(peek()) || isDigit(peek())) advance();
    return makeToken(identifierType());
}

// Consumes a number and returns a token.
static Token number() {
    while(isDigit(peek())) advance();

    // Look for a fractional part.
    if (peek() == '.' && isDigit(peekNext())) {
        // Consume the ".".
        advance();

        while (isDigit(peek())) advance();
    }

    return makeToken(TOKEN_NUMBER);
}

// Consumes a string literal and returns a token.
static Token string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    // The closing quote.
    advance();

    return makeToken(TOKEN_STRING);
}

// Scans the code to return a token or an error token on demand unlike jlox which eagerly scans the entire code and returns a list of tokens.
Token scanToken() {
    skipWhitespace();
    scanner.start = scanner.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        case '(' : return makeToken(TOKEN_LEFT_PAREN);
        case ')' : return makeToken(TOKEN_RIGHT_PAREN);
        case '{' : return makeToken(TOKEN_LEFT_BRACE);
        case '}' : return makeToken(TOKEN_RIGHT_BRACE);
        case ';' : return makeToken(TOKEN_SEMICOLON);
        case ':' : return makeToken(TOKEN_COLON);
        case ',' : return makeToken(TOKEN_COMMA);
        case '.' : return makeToken(TOKEN_DOT);
        case '-' : return makeToken(TOKEN_MINUS);
        case '+' : return makeToken(TOKEN_PLUS);
        case '/' : return makeToken(TOKEN_SLASH);
        case '*' : return makeToken(TOKEN_STAR);
        case '!' : 
            return makeToken(
                match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG
            );
        case '=' : 
            return makeToken(
                match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL
            );
        case '<' : 
            return makeToken(
                match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS
            );
        case '>' : 
            return makeToken(
                match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER
            );
        case '"' : return string();
    }

    return errorToken("Unexpected character.");
}