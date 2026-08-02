#pragma once
#include "common.h"

typedef enum {
    TOK_EOF, TOK_IDENTIFIER, TOK_NUMBER, TOK_STRING, TOK_CHAR,
    TOK_LBRACE, TOK_RBRACE, TOK_LPAREN, TOK_RPAREN, TOK_LBRACKET,
    TOK_RBRACKET, TOK_SEMICOLON, TOK_COMMA, TOK_DOT, TOK_COLON, TOK_SCOPE,
    TOK_LT, TOK_GT, TOK_ARROW, TOK_RANGE, TOK_RANGE_INCLUSIVE, TOK_OTHER
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *begin;
    size_t length;
    const char *ws_begin;
    size_t ws_length;
    size_t line, column;
} Token;

typedef struct {
    const char *src;
    size_t pos, line, column;
} Lexer;

typedef struct {
    Token *items;
    size_t count, capacity;
} TokenList;

void lexer_init(Lexer *l, const char *s);
Token lexer_next(Lexer *l);

void tokens_init(TokenList *l);
void tokens_push(TokenList *l, Token t);
void tokens_free(TokenList *l);

bool token_is(const Token *t, const char *s);
bool token_char(const Token *t, char c);
char *token_text(const Token *t);
bool is_c_keyword(const Token *t);