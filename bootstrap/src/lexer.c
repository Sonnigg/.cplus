#include "lexer.h"

void lexer_init(Lexer *l, const char *s)
{
    l->src = s;
    l->pos = 0;
    l->line = 1;
    l->column = 1;
}

static char lpeek(const Lexer *l)
{
    return l->src[l->pos];
}

static char lpeek2(const Lexer *l)
{
    return l->src[l->pos] ? l->src[l->pos + 1] : 0;
}

static char ladvance(Lexer *l)
{
    char c = l->src[l->pos];
    if (!c) return 0;
    ++l->pos;
    if (c == '\n') {
        ++l->line;
        l->column = 1;
    } else {
        ++l->column;
    }
    return c;
}

static bool ident_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

static bool ident_continue(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static void lexer_ws(Lexer *l)
{
    for (;;) {
        if (isspace((unsigned char)lpeek(l))) {
            ladvance(l);
            continue;
        }
        if (lpeek(l) == '/' && lpeek2(l) == '/') {
            while (lpeek(l) && lpeek(l) != '\n') ladvance(l);
            continue;
        }
        if (lpeek(l) == '/' && lpeek2(l) == '*') {
            ladvance(l);
            ladvance(l);
            while (lpeek(l) && !(lpeek(l) == '*' && lpeek2(l) == '/')) ladvance(l);
            if (lpeek(l)) {
                ladvance(l);
                ladvance(l);
            }
            continue;
        }
        return;
    }
}

Token lexer_next(Lexer *l)
{
    Token t;
    char c;
    size_t start, ws;

    ws = l->pos;
    lexer_ws(l);
    memset(&t, 0, sizeof(t));
    t.ws_begin = l->src + ws;
    t.ws_length = l->pos - ws;
    t.begin = l->src + l->pos;
    t.line = l->line;
    t.column = l->column;
    t.kind = TOK_EOF;

    c = lpeek(l);
    if (!c) return t;

    if (ident_start(c)) {
        start = l->pos;
        ladvance(l);
        while (ident_continue(lpeek(l))) ladvance(l);
        t.kind = TOK_IDENTIFIER;
        t.begin = l->src + start;
        t.length = l->pos - start;
        return t;
    }

    if (isdigit((unsigned char)c)) {
        start = l->pos;
        ladvance(l);
        while (isalnum((unsigned char)lpeek(l)) || lpeek(l) == '.' || lpeek(l) == '_') ladvance(l);
        t.kind = TOK_NUMBER;
        t.begin = l->src + start;
        t.length = l->pos - start;
        return t;
    }

    if (c == '"' || c == '\'') {
        char q = c;
        start = l->pos;
        ladvance(l);
        while (lpeek(l)) {
            char x = ladvance(l);
            if (x == '\\' && lpeek(l)) {
                ladvance(l);
                continue;
            }
            if (x == q) break;
        }
        t.kind = q == '"' ? TOK_STRING : TOK_CHAR;
        t.begin = l->src + start;
        t.length = l->pos - start;
        return t;
    }

    ladvance(l);
    switch (c) {
        case '{': t.kind = TOK_LBRACE; break;
        case '}': t.kind = TOK_RBRACE; break;
        case '(': t.kind = TOK_LPAREN; break;
        case ')': t.kind = TOK_RPAREN; break;
        case '[': t.kind = TOK_LBRACKET; break;
        case ']': t.kind = TOK_RBRACKET; break;
        case ';': t.kind = TOK_SEMICOLON; break;
        case ',': t.kind = TOK_COMMA; break;
        case '.': t.kind = TOK_DOT; break;
        case '<': t.kind = TOK_LT; break;
        case '>': t.kind = TOK_GT; break;
        case '=':
            if (lpeek(l) == '>') {
                ladvance(l);
                if (lpeek(l) == '>') {
                    ladvance(l);
                    t.kind = TOK_RANGE_INCLUSIVE;
                } else {
                    t.kind = TOK_RANGE;
                }
            } else {
                t.kind = TOK_OTHER;
            }
            break;
        case ':':
            if (lpeek(l) == ':') {
                ladvance(l);
                t.kind = TOK_SCOPE;
            } else {
                t.kind = TOK_COLON;
            }
            break;
        default:
            t.kind = TOK_OTHER;
            break;
    }
    t.length = l->pos - (size_t)(t.begin - l->src);
    return t;
}

void tokens_init(TokenList *l)
{
    memset(l, 0, sizeof(*l));
}

void tokens_push(TokenList *l, Token t)
{
    if (l->count == l->capacity) {
        l->capacity = l->capacity ? l->capacity * 2 : 256;
        l->items = xrealloc(l->items, l->capacity * sizeof(*l->items));
    }
    l->items[l->count++] = t;
}

void tokens_free(TokenList *l)
{
    free(l->items);
    memset(l, 0, sizeof(*l));
}

bool token_is(const Token *t, const char *s)
{
    return t->kind == TOK_IDENTIFIER && t->length == strlen(s) && memcmp(t->begin, s, t->length) == 0;
}

bool token_char(const Token *t, char c)
{
    return t->kind == TOK_OTHER && t->length == 1 && t->begin[0] == c;
}

char *token_text(const Token *t)
{
    char *s = xmalloc(t->length + 1);
    memcpy(s, t->begin, t->length);
    s[t->length] = 0;
    return s;
}

bool is_c_keyword(const Token *t)
{
    static const char *const words[] = {
        "static", "inline", "extern", "const", "volatile", "restrict", "void", "char", "short", "int", "long", "float", "double", "bool", "unsigned", "signed", "struct", "union", "enum", "typedef", "sizeof", "return", "if", "else", "while", "for", "do", "switch", "case", "default", "break", "continue", "goto", "namespace", "static_cast", "nullptr"
    };
    size_t i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (token_is(t, words[i])) return true;
    }
    return false;
}