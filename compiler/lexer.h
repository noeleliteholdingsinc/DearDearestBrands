#ifndef LEXER_H
#define LEXER_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    /* Literals */
    TOK_NUMBER,
    TOK_STRING,
    TOK_IDENT,
    
    /* Keywords */
    TOK_IF,
    TOK_ELSE,
    TOK_ENDIF,
    TOK_WHILE,
    TOK_END,
    TOK_RETURN,
    TOK_SIGIL,
    TOK_PERSONA,
    TOK_RITUAL,
    TOK_SAY,
    TOK_FEEL,
    TOK_PERCEIVE,
    
    /* Operators */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_GT,
    TOK_LE,
    TOK_GE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_BITNOT,
    TOK_ASSIGN,
    TOK_ARROW,
    
    /* Delimiters */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_NEWLINE,
    
    /* Special */
    TOK_EOF,
    TOK_ERROR
} TokenType;

typedef struct {
    TokenType type;
    const char *value;
    size_t len;
    int line;
    int column;
} Token;

typedef struct Lexer Lexer;

Lexer* lexer_create(const char *source);
void lexer_destroy(Lexer *l);
Token lexer_next(Lexer *l);
void lexer_reset(Lexer *l);

#endif /* LEXER_H */
