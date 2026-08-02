/*
 * C+ compiler
 *
 * A deliberately small C+ -> C transpiler.  It uses a token stream rather
 * than a full AST, but does two lightweight passes: declaration discovery,
 * then resolution/lowering.  Names are resolved before they are mangled.
 */

// I should definitely split this across files... oh my god

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

typedef struct {
    char *data;
    size_t len, cap;
} Buffer;

bool ALLOW_WARNINGS = true;

static void die(const char *s)
{
    fprintf(stderr, "c+: error: %s\n", s);
    exit(1);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) die("out of memory");
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static void buffer_init(Buffer *b)
{
    memset(b, 0, sizeof(*b));
}

static void buffer_reserve(Buffer *b, size_t n)
{
    size_t need = b->len + n + 1, cap;
    if (need <= b->cap) return;
    cap = b->cap ? b->cap : 256;
    while (cap < need) cap *= 2;
    b->data = xrealloc(b->data, cap);
    b->cap = cap;
}

static void buffer_putc(Buffer *b, char c)
{
    buffer_reserve(b, 1);
    b->data[b->len++] = c;
    b->data[b->len] = 0;
}

static void buffer_puts(Buffer *b, const char *s)
{
    size_t n = strlen(s);
    buffer_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void buffer_free(Buffer *b)
{
    free(b->data);
    memset(b, 0, sizeof(*b));
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *s;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    s = xmalloc((size_t)n + 1);
    if (fread(s, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(s);
        return NULL;
    }
    fclose(f);
    s[n] = 0;
    return s;
}

static char *path_join(const char *a, const char *b)
{
    size_t len_a = strlen(a), len_b = strlen(b);
    char *s = xmalloc(len_a + len_b + 2);
    memcpy(s, a, len_a);
    if (len_a && a[len_a - 1] != '/' && a[len_a - 1] != '\\') s[len_a++] = '/';
    memcpy(s + len_a, b, len_b + 1);
    return s;
}

static char *path_join3(const char *a, const char *b, const char *c)
{
    char *tmp = path_join(a, b);
    char *out = path_join(tmp, c);
    free(tmp);
    return out;
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static char *get_cwd(void)
{
#ifdef _WIN32
    char buf[4096];
    if (!_getcwd(buf, sizeof(buf))) return NULL;
    return xstrdup(buf);
#else
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) return NULL;
    return xstrdup(buf);
#endif
}

static char *dir_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    size_t n;
    if (slash && back && back > slash) slash = back;
    else if (!slash) slash = back;
    if (!slash) return xstrdup(".");
    n = (size_t)(slash - path);
    if (!n) return xstrdup(".");
    {
        char *s = xmalloc(n + 1);
        memcpy(s, path, n);
        s[n] = 0;
        return s;
    }
}

static char *resolve_include_path(const char *from_file, const char *spec, bool angle)
{
    char *cwd = get_cwd();
    char *from_dir = dir_name(from_file);
    char *base = from_dir ? from_dir : xstrdup(".");
    size_t i;
    const char *variants[] = {"", ".hp", ".h+", ".h"};
    char *candidates[32];
    size_t count = 0;

    if (!cwd) cwd = xstrdup(".");

    for (i = 0; i < sizeof(variants) / sizeof(variants[0]); ++i) {
        char *filename = xmalloc(strlen(spec) + strlen(variants[i]) + 1);
        strcpy(filename, spec);
        strcat(filename, variants[i]);
        candidates[count++] = path_join(base, filename);
        free(filename);
    }
    if (angle) {
        for (i = 0; i < sizeof(variants) / sizeof(variants[0]); ++i) {
            char *filename = xmalloc(strlen(spec) + strlen(variants[i]) + 1);
            strcpy(filename, spec);
            strcat(filename, variants[i]);
            candidates[count++] = path_join(cwd, filename);
            candidates[count++] = path_join3(cwd, "libc+", filename);
            free(filename);
        }
    }

    for (i = 0; i < count; ++i) {
        if (file_exists(candidates[i])) {
            char *resolved = candidates[i];
            size_t j;
            for (j = i + 1; j < count; ++j) free(candidates[j]);
            free(cwd);
            free(base);
            return resolved;
        }
    }

    for (i = 0; i < count; ++i) free(candidates[i]);
    free(cwd);
    free(base);
    return NULL;
}

static void rewrite_line(Buffer *out, const char *line)
{
    buffer_puts(out, line);
}

static char *preprocess_source(const char *path, const char *src, int depth)
{
    Buffer out;
    const char *line_start = src;
    const char *end = src + strlen(src);
    buffer_init(&out);
    if (depth > 32) die("include nesting too deep");
    while (line_start < end) {
        const char *line_end = line_start;
        while (line_end < end && *line_end != '\n') ++line_end;
        {
            size_t n = (size_t)(line_end - line_start);
            char *line = xmalloc(n + 1);
            memcpy(line, line_start, n);
            line[n] = 0;
            if (n > 0 && line[0] == '#' && strncmp(line, "#include", 8) == 0) {
                char *spec = NULL;
                bool angle = false;
                char *p = line + 8;
                while (*p && isspace((unsigned char)*p)) ++p;
                if (*p == '<') {
                    const char *q = ++p;
                    angle = true;
                    while (*q && *q != '>') ++q;
                    if (*q == '>') {
                        size_t len = (size_t)(q - p);
                        spec = xmalloc(len + 1);
                        memcpy(spec, p, len);
                        spec[len] = 0;
                    }
                } else if (*p == '"') {
                    const char *q = ++p;
                    while (*q && *q != '"') ++q;
                    if (*q == '"') {
                        size_t len = (size_t)(q - p);
                        spec = xmalloc(len + 1);
                        memcpy(spec, p, len);
                        spec[len] = 0;
                    }
                }
                if (spec) {
                    char *resolved = resolve_include_path(path, spec, angle);
                    if (resolved) {
                        char *included = read_file(resolved);
                        if (included) {
                            char *child = preprocess_source(resolved, included, depth + 1);
                            buffer_puts(&out, child);
                            buffer_putc(&out, '\n');
                            free(child);
                            free(included);
                        }
                        free(resolved);
                    } else {
                        buffer_puts(&out, line);
                    }
                    free(spec);
                } else {
                    buffer_puts(&out, line);
                }
            } else {
                rewrite_line(&out, line);
            }
            free(line);
        }
        if (line_end < end) {
            buffer_putc(&out, '\n');
            line_start = line_end + 1;
        } else {
            break;
        }
    }
    return out.data;
}

/* ------------------------------------------------------------------------- */
/* Lexer                                                                     */

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

static void lexer_init(Lexer *l, const char *s)
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

static Token lexer_next(Lexer *l)
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

static void tokens_init(TokenList *l)
{
    memset(l, 0, sizeof(*l));
}

static void tokens_push(TokenList *l, Token t)
{
    if (l->count == l->capacity) {
        l->capacity = l->capacity ? l->capacity * 2 : 256;
        l->items = xrealloc(l->items, l->capacity * sizeof(*l->items));
    }
    l->items[l->count++] = t;
}

static void tokens_free(TokenList *l)
{
    free(l->items);
    memset(l, 0, sizeof(*l));
}

static bool token_is(const Token *t, const char *s)
{
    return t->kind == TOK_IDENTIFIER && t->length == strlen(s) && memcmp(t->begin, s, t->length) == 0;
}

static bool token_char(const Token *t, char c)
{
    return t->kind == TOK_OTHER && t->length == 1 && t->begin[0] == c;
}

static char *token_text(const Token *t)
{
    char *s = xmalloc(t->length + 1);
    memcpy(s, t->begin, t->length);
    s[t->length] = 0;
    return s;
}

static bool is_c_keyword(const Token *t)
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

/* ------------------------------------------------------------------------- */
/* Namespaces, symbols, and resolution                                       */

typedef struct {
    char **items;
    size_t count, cap;
} NamespaceStack;

typedef enum {
    SYM_TYPE = 1,
    SYM_ENUM = 2,
    SYM_ENUM_MEMBER = 4,
    SYM_FUNCTION = 8,
    SYM_METHOD = 16,
    SYM_VARIABLE = 32,
    SYM_FIELD = 64
} SymbolKind;

typedef struct {
    char *name, *qualified_name, *mangled_name, *owner_struct, *type_qualified;
    unsigned kind;
    int pointer_depth, receiver_pointer_depth;
    bool is_static;
} Symbol;

typedef struct {
    Symbol *items;
    size_t count, cap;
} SymbolRegistry;

static void ns_init(NamespaceStack *ns)
{
    memset(ns, 0, sizeof(*ns));
}

static void ns_push(NamespaceStack *ns, const Token *tok)
{
    if (ns->count == ns->cap) {
        ns->cap = ns->cap ? ns->cap * 2 : 8;
        ns->items = xrealloc(ns->items, ns->cap * sizeof(char *));
    }
    ns->items[ns->count++] = token_text(tok);
}

static void ns_pop(NamespaceStack *ns)
{
    if (ns->count) free(ns->items[--ns->count]);
}

static void ns_free(NamespaceStack *ns)
{
    while (ns->count) ns_pop(ns);
    free(ns->items);
    memset(ns, 0, sizeof(*ns));
}

static char *ns_prefix(const NamespaceStack *ns, size_t depth)
{
    Buffer b;
    size_t i;
    buffer_init(&b);
    if (depth > ns->count) depth = ns->count;
    for (i = 0; i < depth; ++i) {
        if (i) buffer_puts(&b, "::");
        buffer_puts(&b, ns->items[i]);
    }
    if (!b.data) return xstrdup("");
    return b.data;
}

static char *qualify(const NamespaceStack *ns, size_t depth, const char *name)
{
    char *p = ns_prefix(ns, depth);
    Buffer b;
    buffer_init(&b);
    if (*p) {
        buffer_puts(&b, p);
        buffer_puts(&b, "::");
    }
    buffer_puts(&b, name);
    free(p);
    return b.data;
}

/* The single canonical C+ -> C name mangler.  Collision checks live in the
 * registry, so the intentionally readable underscore spelling is safe. */
static char *mangle_qualified_name(const char *qualified)
{
    Buffer b;
    const char *p = qualified;
    buffer_init(&b);
    while (*p) {
        if (p[0] == ':' && p[1] == ':') {
            buffer_putc(&b, '_');
            p += 2;
        } else {
            buffer_putc(&b, *p++);
        }
    }
    return b.data ? b.data : xstrdup("");
}

static void symbols_init(SymbolRegistry *r)
{
    memset(r, 0, sizeof(*r));
}

static void symbols_free(SymbolRegistry *r)
{
    size_t i;
    for (i = 0; i < r->count; ++i) {
        free(r->items[i].name);
        free(r->items[i].qualified_name);
        free(r->items[i].mangled_name);
        free(r->items[i].owner_struct);
        free(r->items[i].type_qualified);
    }
    free(r->items);
    memset(r, 0, sizeof(*r));
}

static Symbol *symbol_exact(SymbolRegistry *r, const char *q, unsigned mask)
{
    size_t i;
    for (i = 0; i < r->count; ++i) {
        if ((r->items[i].kind & mask) && strcmp(r->items[i].qualified_name, q) == 0) {
            return &r->items[i];
        }
    }
    return NULL;
}

static Symbol *symbols_add(SymbolRegistry *r, const char *q, unsigned kind, const char *owner, const char *type, int depth, bool is_static)
{
    Symbol *old = symbol_exact(r, q, kind);
    size_t i;
    const char *last;
    Symbol *s;

    if (old) return old; /* a prototype plus a definition is one symbol */
    if (r->count == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 64;
        r->items = xrealloc(r->items, r->cap * sizeof(*r->items));
    }
    s = &r->items[r->count++];
    memset(s, 0, sizeof(*s));
    last = strrchr(q, ':');
    last = last ? last + 1 : q;
    s->name = xstrdup(last);
    s->qualified_name = xstrdup(q);
    s->mangled_name = mangle_qualified_name(q);
    s->kind = kind;
    s->owner_struct = owner ? xstrdup(owner) : NULL;
    s->type_qualified = type ? xstrdup(type) : NULL;
    s->pointer_depth = depth;
    s->is_static = is_static;

    for (i = 0; i + 1 < r->count; ++i) {
        if (strcmp(r->items[i].mangled_name, s->mangled_name) == 0 && strcmp(r->items[i].qualified_name, s->qualified_name) != 0) {
            fprintf(stderr, "c+: error: C name collision: '%s' and '%s' both mangle to '%s'\n", r->items[i].qualified_name, s->qualified_name, s->mangled_name);
            exit(1);
        }
    }
    return s;
}

/* Exact qualification wins.  If it is not a complete identity, each lexical
 * namespace prefix is tried from innermost to global. */
static Symbol *resolve_name(SymbolRegistry *r, const NamespaceStack *ns, const char *spelling, unsigned mask)
{
    Symbol *s;
    int d;
    char *q;

    if (strstr(spelling, "::")) {
        s = symbol_exact(r, spelling, mask);
        if (s) return s;
    }
    for (d = (int)ns->count; d >= 0; --d) {
        q = qualify(ns, (size_t)d, spelling);
        s = symbol_exact(r, q, mask);
        free(q);
        if (s) return s;
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* Lightweight scopes and compiler context                                   */

typedef struct {
    char *name, *type_qualified;
    int pointer_depth, depth;
} Local;

typedef struct {
    Local *items;
    size_t count, cap;
} LocalTable;

typedef struct {
    Buffer output;
} DeferScope;

typedef struct {
    DeferScope *items;
    size_t count, cap;
} DeferStack;

typedef struct {
    const char *source;
    TokenList tokens;
    SymbolRegistry symbols;
    Buffer output;
    LocalTable locals;
    int local_depth;
    const char *current_struct, *current_method;
    bool current_method_static;
    size_t next_temporary;
    DeferStack defers;
} Transpiler;

static void locals_init(LocalTable *v)
{
    memset(v, 0, sizeof(*v));
}

static void locals_free(LocalTable *v)
{
    size_t i;
    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
        free(v->items[i].type_qualified);
    }
    free(v->items);
    memset(v, 0, sizeof(*v));
}

static size_t locals_mark(const LocalTable *v)
{
    return v->count;
}

static void locals_restore(LocalTable *v, size_t mark)
{
    while (v->count > mark) {
        Local *x = &v->items[--v->count];
        free(x->name);
        free(x->type_qualified);
    }
}

static void local_add(LocalTable *v, const char *name, const char *type, int ptr, int depth)
{
    if (v->count == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 32;
        v->items = xrealloc(v->items, v->cap * sizeof(*v->items));
    }
    v->items[v->count].name = xstrdup(name);
    v->items[v->count].type_qualified = xstrdup(type);
    v->items[v->count].pointer_depth = ptr;
    v->items[v->count].depth = depth;
    ++v->count;
}

static Local *local_lookup(LocalTable *v, const char *name)
{
    size_t i;
    for (i = v->count; i > 0; --i) {
        if (strcmp(v->items[i - 1].name, name) == 0) {
            return &v->items[i - 1];
        }
    }
    return NULL;
}

static void locals_leave(LocalTable *v, int depth)
{
    while (v->count && v->items[v->count - 1].depth >= depth) {
        Local *x = &v->items[--v->count];
        free(x->name);
        free(x->type_qualified);
    }
}

static void die_at(const Token *t, const char *msg)
{
    fprintf(stderr, "c+: error:%zu:%zu: %s\n", t->line, t->column, msg);
    exit(1);
}

static void warn_at(const Token *t, const char *msg)
{
    if (ALLOW_WARNINGS)
        fprintf(stderr, "c+: warning:%zu:%zu: %s\n", t->line, t->column, msg);
}

static Token *at(Transpiler *t, size_t p)
{
    return p < t->tokens.count ? &t->tokens.items[p] : &t->tokens.items[t->tokens.count - 1];
}

static void emit_ws(Buffer *o, const Token *t)
{
    if (t->ws_length) {
        buffer_reserve(o, t->ws_length);
        memcpy(o->data + o->len, t->ws_begin, t->ws_length);
        o->len += t->ws_length;
        o->data[o->len] = 0;
    }
}

static void emit_raw(Buffer *o, const Token *t)
{
    buffer_reserve(o, t->length);
    memcpy(o->data + o->len, t->begin, t->length);
    o->len += t->length;
    o->data[o->len] = 0;
}

static void emit_full(Buffer *o, const Token *t)
{
    emit_ws(o, t);
    emit_raw(o, t);
}

static void buffer_insert(Buffer *b, size_t at_pos, const char *s)
{
    size_t n = strlen(s);
    if (at_pos > b->len) at_pos = b->len;
    buffer_reserve(b, n);
    memmove(b->data + at_pos + n, b->data + at_pos, b->len - at_pos + 1);
    memcpy(b->data + at_pos, s, n);
    b->len += n;
}

static size_t matching(Transpiler *t, size_t open, TokenKind left, TokenKind right, size_t end)
{
    int d = 0;
    size_t p;
    for (p = open; p < end; ++p) {
        Token *x = at(t, p);
        if (x->kind == left) {
            ++d;
        } else if (x->kind == right && --d == 0) {
            return p;
        }
    }
    die_at(at(t, open), "unclosed delimiter");
    return end;
}

static char *read_qualified(Transpiler *t, size_t p, size_t end, size_t *used)
{
    Buffer b;
    size_t n = 0;
    Token *x = at(t, p);
    if (p >= end || x->kind != TOK_IDENTIFIER) return NULL;
    buffer_init(&b);
    buffer_reserve(&b, x->length);
    memcpy(b.data, x->begin, x->length);
    b.len = x->length;
    b.data[b.len] = 0;
    ++n;
    while (p + n + 1 < end && at(t, p + n)->kind == TOK_SCOPE && at(t, p + n + 1)->kind == TOK_IDENTIFIER) {
        Token *next = at(t, p + n + 1);
        buffer_puts(&b, "::");
        buffer_reserve(&b, next->length);
        memcpy(b.data + b.len, next->begin, next->length);
        b.len += next->length;
        b.data[b.len] = 0;
        n += 2;
    }
    *used = n;
    return b.data;
}

static bool is_qualifier(const Token *x)
{
    return token_is(x, "static") || token_is(x, "extern") || token_is(x, "const") || token_is(x, "volatile") || token_is(x, "restrict") || token_is(x, "inline");
}

/* Resolve only user-defined types.  Built-in C types deliberately return
 * NULL: this remains a lightweight checker rather than a C type system. */
static Symbol *parse_resolved_type(Transpiler *t, const NamespaceStack *ns, size_t p, size_t end, size_t *after, int *pointer_depth)
{
    size_t used = 0;
    char *q;
    Symbol *type;

    while (p < end && is_qualifier(at(t, p))) ++p;
    if (p < end && (token_is(at(t, p), "struct") || token_is(at(t, p), "enum"))) ++p;
    q = read_qualified(t, p, end, &used);
    if (!q) return NULL;
    type = resolve_name(&t->symbols, ns, q, SYM_TYPE | SYM_ENUM);
    free(q);
    if (!type) return NULL;
    p += used;
    if (pointer_depth) {
        *pointer_depth = 0;
        while (p < end && token_char(at(t, p), '*')) {
            ++*pointer_depth;
            ++p;
        }
    }
    if (after) *after = p;
    return type;
}

typedef struct {
    bool ok, is_static;
    size_t name, lparen, rparen, body, close, after;
} Callable;

typedef struct {
    bool ok;
    size_t name, after_type;
    Symbol *type;
    int pointer_depth;
} VarDecl;

static bool parse_callable(Transpiler *t, size_t p, size_t end, Callable *c)
{
    size_t i, lp = end, rp, a;
    bool bad = false;
    memset(c, 0, sizeof(*c));
    for (i = p; i < end; ++i) {
        Token *x = at(t, i);
        if (x->kind == TOK_SEMICOLON || x->kind == TOK_LBRACE) return false;
        if (token_char(x, '=') || x->kind == TOK_COMMA) bad = true;
        if (x->kind == TOK_LPAREN) {
            lp = i;
            break;
        }
    }
    if (bad || lp == end || lp == p || at(t, lp - 1)->kind != TOK_IDENTIFIER) return false;
    rp = matching(t, lp, TOK_LPAREN, TOK_RPAREN, end);
    a = rp + 1;
    if (a >= end || (at(t, a)->kind != TOK_LBRACE && at(t, a)->kind != TOK_SEMICOLON)) return false;
    c->name = lp - 1;
    c->lparen = lp;
    c->rparen = rp;
    c->after = a;
    for (i = p; i < c->name; ++i) {
        if (token_is(at(t, i), "static")) c->is_static = true;
    }
    if (at(t, a)->kind == TOK_LBRACE) {
        c->body = a;
        c->close = matching(t, a, TOK_LBRACE, TOK_RBRACE, end);
        c->after = c->close + 1;
    } else {
        c->body = c->close = end;
        c->after = a + 1;
    }
    c->ok = true;
    return true;
}

static bool parse_var_decl(Transpiler *t, const NamespaceStack *ns, size_t p, size_t end, VarDecl *d)
{
    size_t i = p;
    Symbol *type;
    memset(d, 0, sizeof(*d));
    type = parse_resolved_type(t, ns, i, end, &i, &d->pointer_depth);
    if (!type) return false;
    d->after_type = i - (size_t)d->pointer_depth;
    if (i >= end || at(t, i)->kind != TOK_IDENTIFIER || is_c_keyword(at(t, i))) return false;
    if (i + 1 < end && at(t, i + 1)->kind == TOK_LPAREN) return false;
    d->name = i;
    d->type = type;
    d->ok = true;
    return true;
}

static bool parse_typedef_decl(Transpiler *t, const NamespaceStack *ns, size_t p, size_t end, size_t *alias)
{
    size_t i = p + 1;
    if (!token_is(at(t, p), "typedef")) return false;
    while (i < end && at(t, i)->kind != TOK_SEMICOLON) ++i;
    if (i >= end) return false;
    while (i > p + 1) {
        --i;
        if (at(t, i)->kind == TOK_IDENTIFIER && !is_c_keyword(at(t, i))) {
            *alias = i;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* First pass: discover declarations                                          */

static void discover_range(Transpiler *, NamespaceStack *, size_t, size_t);

static void discover_enum(Transpiler *t, NamespaceStack *ns, size_t start, size_t open, size_t close, Symbol *owner)
{
    size_t p;
    bool want = true;
    (void)start;
    for (p = open + 1; p < close; ++p) {
        Token *x = at(t, p);
        if (want && x->kind == TOK_IDENTIFIER) {
            char *n = token_text(x), *q;
            Buffer b;
            buffer_init(&b);
            buffer_puts(&b, owner->qualified_name);
            buffer_puts(&b, "::");
            buffer_puts(&b, n);
            q = b.data;
            symbols_add(&t->symbols, q, SYM_ENUM_MEMBER, owner->qualified_name, NULL, 0, false);
            free(n);
            free(q);
            want = false;
        }
        if (x->kind == TOK_COMMA) want = true;
    }
    (void)ns;
}

static void discover_struct(Transpiler *t, NamespaceStack *ns, size_t start, size_t open, size_t close, Symbol *owner)
{
    size_t p = open + 1;
    (void)start;
    while (p < close) {
        Callable c;
        if (parse_callable(t, p, close, &c)) {
            char *name = token_text(at(t, c.name));
            Buffer b;
            char *q;
            size_t ignored;
            int return_depth = 0;
            Symbol *return_type = parse_resolved_type(t, ns, p, c.name, &ignored, &return_depth);
            buffer_init(&b);
            buffer_puts(&b, owner->qualified_name);
            buffer_puts(&b, "::");
            buffer_puts(&b, name);
            q = b.data;
            symbols_add(&t->symbols, q, SYM_METHOD, owner->qualified_name, return_type ? return_type->qualified_name : NULL, return_depth, c.is_static);
            free(name);
            free(q);
            p = c.after;
            continue;
        }
        {
            size_t semi = p, name = close;
            while (semi < close && at(t, semi)->kind != TOK_SEMICOLON) ++semi;
            if (semi < close) {
                size_t i = semi;
                while (i > p) {
                    --i;
                    if (at(t, i)->kind == TOK_IDENTIFIER && !is_c_keyword(at(t, i))) {
                        name = i;
                        break;
                    }
                }
                if (name < close) {
                    char *field = token_text(at(t, name));
                    Buffer b;
                    char *q;
                    buffer_init(&b);
                    buffer_puts(&b, owner->qualified_name);
                    buffer_puts(&b, "::");
                    buffer_puts(&b, field);
                    q = b.data;
                    symbols_add(&t->symbols, q, SYM_FIELD, owner->qualified_name, NULL, 0, false);
                    free(field);
                    free(q);
                }
                p = semi + 1;
                continue;
            }
        }
        ++p;
    }
    (void)ns;
}

static void discover_range(Transpiler *t, NamespaceStack *ns, size_t begin, size_t end)
{
    size_t p = begin;
    while (p < end) {
        Token *x = at(t, p);
        Callable c;
        VarDecl d;

        if (token_is(x, "namespace") && p + 2 < end && at(t, p + 1)->kind == TOK_IDENTIFIER && at(t, p + 2)->kind == TOK_LBRACE) {
            size_t close = matching(t, p + 2, TOK_LBRACE, TOK_RBRACE, end);
            ns_push(ns, at(t, p + 1));
            discover_range(t, ns, p + 3, close);
            ns_pop(ns);
            p = close + 1;
            continue;
        }
        if ((token_is(x, "struct") || token_is(x, "enum")) && p + 2 < end && at(t, p + 1)->kind == TOK_IDENTIFIER && at(t, p + 2)->kind == TOK_LBRACE) {
            size_t close = matching(t, p + 2, TOK_LBRACE, TOK_RBRACE, end);
            char *name = token_text(at(t, p + 1)), *q = qualify(ns, ns->count, name);
            Symbol *s = symbols_add(&t->symbols, q, token_is(x, "enum") ? SYM_ENUM : SYM_TYPE, NULL, NULL, 0, false);
            if (token_is(x, "enum")) {
                discover_enum(t, ns, p, p + 2, close, s);
            } else {
                discover_struct(t, ns, p, p + 2, close, s);
            }
            free(name);
            free(q);
            p = close + 1;
            if (p < end && at(t, p)->kind == TOK_SEMICOLON) ++p;
            continue;
        }
        if (parse_callable(t, p, end, &c)) {
            /* Foo::method definitions are already registered by their class. */
            if (c.name == 0 || at(t, c.name - 1)->kind != TOK_SCOPE) {
                char *name = token_text(at(t, c.name)), *q = qualify(ns, ns->count, name);
                size_t ignored;
                int return_depth = 0;
                Symbol *return_type = parse_resolved_type(t, ns, p, c.name, &ignored, &return_depth);
                symbols_add(&t->symbols, q, SYM_FUNCTION, NULL, return_type ? return_type->qualified_name : NULL, return_depth, c.is_static);
                free(name);
                free(q);
            }
            p = c.after;
            continue;
        }
        if (parse_typedef_decl(t, ns, p, end, &d.name)) {
            char *name = token_text(at(t, d.name));
            char *q = qualify(ns, ns->count, name);
            symbols_add(&t->symbols, q, SYM_TYPE, NULL, NULL, 0, false);
            free(name);
            free(q);
            while (p < end && at(t, p)->kind != TOK_SEMICOLON) ++p;
            if (p < end) ++p;
            continue;
        }
        if (parse_var_decl(t, ns, p, end, &d)) {
            char *name = token_text(at(t, d.name)), *q = qualify(ns, ns->count, name);
            symbols_add(&t->symbols, q, SYM_VARIABLE, NULL, d.type->qualified_name, d.pointer_depth, false);
            free(name);
            free(q);
            while (p < end && at(t, p)->kind != TOK_SEMICOLON) ++p;
            if (p < end) ++p;
            continue;
        }
        ++p;
    }
}

/* ------------------------------------------------------------------------- */
/* Lowering                                                                   */

static Symbol *lookup_value(Transpiler *t, NamespaceStack *ns, const char *name, int *ptr)
{
    Local *l = local_lookup(&t->locals, name);
    Symbol *s;
    if (l) {
        if (ptr) *ptr = l->pointer_depth;
        return symbol_exact(&t->symbols, l->type_qualified, SYM_TYPE | SYM_ENUM);
    }
    s = resolve_name(&t->symbols, ns, name, SYM_VARIABLE);
    if (s && ptr) *ptr = s->pointer_depth;
    if (s) return symbol_exact(&t->symbols, s->type_qualified, SYM_TYPE | SYM_ENUM);
    return NULL;
}

static Symbol *method_for(Transpiler *t, const char *typeq, const char *name)
{
    Buffer b;
    Symbol *s;
    buffer_init(&b);
    buffer_puts(&b, typeq);
    buffer_puts(&b, "::");
    buffer_puts(&b, name);
    s = symbol_exact(&t->symbols, b.data, SYM_METHOD);
    buffer_free(&b);
    return s;
}

static Symbol *field_for(Transpiler *t, const char *typeq, const char *name)
{
    Buffer b;
    Symbol *s;
    buffer_init(&b);
    buffer_puts(&b, typeq);
    buffer_puts(&b, "::");
    buffer_puts(&b, name);
    s = symbol_exact(&t->symbols, b.data, SYM_FIELD);
    buffer_free(&b);
    return s;
}

static void emit_one(Transpiler *t, NamespaceStack *ns, size_t *pp, size_t end);
static void emit_fragment(Transpiler *t, NamespaceStack *ns, size_t begin, size_t end)
{
    size_t p = begin;
    while (p < end) emit_one(t, ns, &p, end);
}

static void emit_fragment_to_buffer(Buffer *out, Transpiler *t, NamespaceStack *ns, size_t begin, size_t end)
{
    Buffer saved = t->output;
    Buffer tmp;
    buffer_init(&tmp);
    t->output = tmp;
    emit_fragment(t, ns, begin, end);
    tmp = t->output;
    t->output = saved;
    if (tmp.len) {
        buffer_reserve(out, tmp.len);
        memcpy(out->data + out->len, tmp.data, tmp.len);
        out->len += tmp.len;
        out->data[out->len] = 0;
    }
    buffer_free(&tmp);
}

static void defer_stack_push(DeferStack *d)
{
    if (d->count == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 8;
        d->items = xrealloc(d->items, d->cap * sizeof(*d->items));
    }
    memset(&d->items[d->count], 0, sizeof(d->items[d->count]));
    ++d->count;
}

static DeferScope *defer_stack_current(DeferStack *d)
{
    return d->count ? &d->items[d->count - 1] : NULL;
}

static void defer_stack_pop(DeferStack *d)
{
    if (!d->count) return;
    buffer_free(&d->items[d->count - 1].output);
    --d->count;
}

/* C+ struct literals borrow Rust's field syntax: Point { x: 1, y: 2 }.
 * They lower to a standard C99 designated compound literal. */
static bool try_emit_struct_literal(Transpiler *t, NamespaceStack *ns, size_t *pp, size_t end)
{
    size_t p = *pp, used = 0, open, close, item;
    char *q;
    Symbol *type;

    if (at(t, p)->kind != TOK_IDENTIFIER) return false;
    q = read_qualified(t, p, end, &used);
    if (!q) return false;
    type = resolve_name(&t->symbols, ns, q, SYM_TYPE);
    free(q);
    open = p + used;
    if (!type || open >= end || at(t, open)->kind != TOK_LBRACE) return false;
    close = matching(t, open, TOK_LBRACE, TOK_RBRACE, end);
    emit_ws(&t->output, at(t, p));
    buffer_putc(&t->output, '(');
    buffer_puts(&t->output, type->mangled_name);
    buffer_puts(&t->output, "){");
    item = open + 1;
    while (item < close) {
        size_t colon, value_end, scan;
        char *field;
        if (at(t, item)->kind == TOK_COMMA) {
            emit_full(&t->output, at(t, item));
            ++item;
            continue;
        }
        if (at(t, item)->kind != TOK_IDENTIFIER) die_at(at(t, item), "struct literal expects a field name");
        colon = item + 1;
        if (colon >= close || at(t, colon)->kind != TOK_COLON) die_at(at(t, item), "struct literal fields use 'field: value'");
        field = token_text(at(t, item));
        if (!field_for(t, type->qualified_name, field)) {
            free(field);
            die_at(at(t, item), "unknown field in struct literal");
        }
        buffer_putc(&t->output, '.');
        buffer_puts(&t->output, field);
        buffer_puts(&t->output, " =");
        free(field);
        value_end = close;
        scan = colon + 1;
        {
            int paren = 0, bracket = 0, brace = 0;
            for (; scan < close; ++scan) {
                Token *z = at(t, scan);
                if (z->kind == TOK_LPAREN) ++paren;
                else if (z->kind == TOK_RPAREN) --paren;
                else if (z->kind == TOK_LBRACKET) ++bracket;
                else if (z->kind == TOK_RBRACKET) --bracket;
                else if (z->kind == TOK_LBRACE) ++brace;
                else if (z->kind == TOK_RBRACE) --brace;
                else if (z->kind == TOK_COMMA && !paren && !bracket && !brace) {
                    value_end = scan;
                    break;
                }
            }
        }
        emit_fragment(t, ns, colon + 1, value_end);
        if (value_end < close) {
            buffer_putc(&t->output, ',');
            item = value_end + 1;
        } else {
            item = close;
        }
    }
    emit_ws(&t->output, at(t, close));
    buffer_putc(&t->output, '}');
    *pp = close + 1;
    return true;
}

static void emit_fragment_without_first_ws(Transpiler *t, NamespaceStack *ns, size_t begin, size_t end)
{
    Token *first = at(t, begin);
    size_t ws = first->ws_length;
    first->ws_length = 0;
    emit_fragment(t, ns, begin, end);
    first->ws_length = ws;
}

static void emit_value_receiver_call(Transpiler *t, NamespaceStack *ns, size_t begin, size_t receiver_end, Symbol *type, Symbol *method, size_t method_lparen, size_t method_rparen, size_t *pp)
{
    char temp[64];
    bool pass_receiver = !method->is_static || method->receiver_pointer_depth > 0;
    snprintf(temp, sizeof(temp), "__cplus_receiver_%zu", t->next_temporary++);
    emit_ws(&t->output, at(t, begin));
    if (pass_receiver) {
        buffer_puts(&t->output, "({ ");
        buffer_puts(&t->output, type->mangled_name);
        buffer_putc(&t->output, ' ');
        buffer_puts(&t->output, temp);
        buffer_puts(&t->output, " = ");
        emit_fragment_without_first_ws(t, ns, begin, receiver_end);
        buffer_puts(&t->output, "; ");
        buffer_puts(&t->output, method->mangled_name);
        buffer_puts(&t->output, "(&");
        buffer_puts(&t->output, temp);
        if (method_lparen + 1 < method_rparen) buffer_putc(&t->output, ',');
        emit_fragment(t, ns, method_lparen + 1, method_rparen);
        buffer_puts(&t->output, "); })");
    } else {
        /* Instance syntax on a parameterless static method keeps evaluation
         * of the receiver expression rather than dropping its side effects. */
        buffer_puts(&t->output, "((void)(");
        emit_fragment_without_first_ws(t, ns, begin, receiver_end);
        buffer_puts(&t->output, "), ");
        buffer_puts(&t->output, method->mangled_name);
        buffer_putc(&t->output, '(');
        emit_fragment(t, ns, method_lparen + 1, method_rparen);
        buffer_puts(&t->output, "))");
    }
    *pp = method_rparen + 1;
}

static bool try_emit_expression_method_call(Transpiler *t, NamespaceStack *ns, size_t *pp, size_t end)
{
    size_t p = *pp, used = 0, receiver_end, op, method_name, method_lparen, method_rparen;
    char *q, *mn;
    Symbol *callee, *type, *method;
    bool arrow = false;

    if (at(t, p)->kind != TOK_IDENTIFIER) return false;
    q = read_qualified(t, p, end, &used);
    if (!q || p + used >= end || at(t, p + used)->kind != TOK_LPAREN) {
        free(q);
        return false;
    }
    callee = resolve_name(&t->symbols, ns, q, SYM_FUNCTION | SYM_METHOD);
    free(q);
    if (!callee || !callee->type_qualified) return false;
    receiver_end = matching(t, p + used, TOK_LPAREN, TOK_RPAREN, end) + 1;
    if (receiver_end >= end) return false;
    if (at(t, receiver_end)->kind == TOK_DOT) {
        op = receiver_end;
        method_name = receiver_end + 1;
        method_lparen = receiver_end + 2;
    } else if (token_char(at(t, receiver_end), '-') && receiver_end + 3 < end && at(t, receiver_end + 1)->kind == TOK_GT) {
        arrow = true;
        op = receiver_end;
        method_name = receiver_end + 2;
        method_lparen = receiver_end + 3;
    } else {
        return false;
    }
    (void)op;
    if (method_lparen >= end || at(t, method_name)->kind != TOK_IDENTIFIER || at(t, method_lparen)->kind != TOK_LPAREN) return false;
    type = symbol_exact(&t->symbols, callee->type_qualified, SYM_TYPE);
    if (!type) return false;
    mn = token_text(at(t, method_name));
    method = method_for(t, type->qualified_name, mn);
    free(mn);
    if (!method) return false;
    method_rparen = matching(t, method_lparen, TOK_LPAREN, TOK_RPAREN, end);
    if (arrow && callee->pointer_depth < 1) die_at(at(t, p), "'->' method call requires a pointer expression");
    if (callee->pointer_depth > 0 && (!method->is_static || method->receiver_pointer_depth > 0)) {
        emit_ws(&t->output, at(t, p));
        buffer_puts(&t->output, method->mangled_name);
        buffer_putc(&t->output, '(');
        emit_fragment_without_first_ws(t, ns, p, receiver_end);
        if (method_lparen + 1 < method_rparen) buffer_putc(&t->output, ',');
        emit_fragment(t, ns, method_lparen + 1, method_rparen);
        buffer_putc(&t->output, ')');
        *pp = method_rparen + 1;
        return true;
    }
    emit_value_receiver_call(t, ns, p, receiver_end, type, method, method_lparen, method_rparen, pp);
    return true;
}

static bool try_emit_parenthesized_method_call(Transpiler *t, NamespaceStack *ns, size_t *pp, size_t end)
{
    size_t p = *pp, close, method_name, method_lparen, method_rparen;
    char *name, *mn;
    Symbol *type, *method;
    int ptr = 0;

    if (at(t, p)->kind != TOK_LPAREN) return false;
    close = matching(t, p, TOK_LPAREN, TOK_RPAREN, end);
    if (close != p + 2 || close + 3 >= end || at(t, close + 1)->kind != TOK_DOT || at(t, close + 2)->kind != TOK_IDENTIFIER || at(t, close + 3)->kind != TOK_LPAREN) return false;
    name = token_text(at(t, p + 1));
    type = lookup_value(t, ns, name, &ptr);
    if (!type) {
        free(name);
        return false;
    }
    method_name = close + 2;
    method_lparen = close + 3;
    mn = token_text(at(t, method_name));
    method = method_for(t, type->qualified_name, mn);
    free(mn);
    if (!method) {
        free(name);
        return false;
    }
    method_rparen = matching(t, method_lparen, TOK_LPAREN, TOK_RPAREN, end);
    emit_ws(&t->output, at(t, p));
    buffer_puts(&t->output, method->mangled_name);
    buffer_putc(&t->output, '(');
    if (!method->is_static || method->receiver_pointer_depth > 0) {
        if (ptr == 0) buffer_putc(&t->output, '&');
        buffer_puts(&t->output, name);
        if (method_lparen + 1 < method_rparen) buffer_putc(&t->output, ',');
    }
    emit_fragment(t, ns, method_lparen + 1, method_rparen);
    buffer_putc(&t->output, ')');
    free(name);
    *pp = method_rparen + 1;
    return true;
}

static bool try_emit_struct_literal_method_call(Transpiler *t, NamespaceStack *ns, size_t *pp, size_t end)
{
    size_t p = *pp, used = 0, open, close, method_name, method_lparen, method_rparen;
    char *q, *mn;
    Symbol *type, *method;

    if (at(t, p)->kind != TOK_IDENTIFIER) return false;
    q = read_qualified(t, p, end, &used);
    if (!q) return false;
    type = resolve_name(&t->symbols, ns, q, SYM_TYPE);
    free(q);
    open = p + used;
    if (!type || open >= end || at(t, open)->kind != TOK_LBRACE) return false;
    close = matching(t, open, TOK_LBRACE, TOK_RBRACE, end);
    if (close + 3 >= end || at(t, close + 1)->kind != TOK_DOT || at(t, close + 2)->kind != TOK_IDENTIFIER || at(t, close + 3)->kind != TOK_LPAREN) return false;
    method_name = close + 2;
    method_lparen = close + 3;
    mn = token_text(at(t, method_name));
    method = method_for(t, type->qualified_name, mn);
    free(mn);
    if (!method) return false;
    method_rparen = matching(t, method_lparen, TOK_LPAREN, TOK_RPAREN, end);
    emit_value_receiver_call(t, ns, p, close + 1, type, method, method_lparen, method_rparen, pp);
    return true;
}

static bool builtin_type_token(const Token *x)
{
    return token_is(x, "void") || token_is(x, "char") || token_is(x, "short") || token_is(x, "int") || token_is(x, "long") || token_is(x, "float") || token_is(x, "double") || token_is(x, "bool") || token_is(x, "signed") || token_is(x, "unsigned") || token_is(x, "size_t");
}

static bool looks_like_c_style_cast(Transpiler *t, NamespaceStack *ns, size_t p, size_t end)
{
    size_t q, after;
    int depth;
    Symbol *type;
    if (at(t, p)->kind != TOK_LPAREN || p + 2 >= end || (p && token_is(at(t, p - 1), "sizeof"))) return false;
    q = p + 1;
    type = parse_resolved_type(t, ns, q, end, &after, &depth);
    if (type) return after < end && at(t, after)->kind == TOK_RPAREN;
    if (!builtin_type_token(at(t, q))) return false;
    while (q < end && (builtin_type_token(at(t, q)) || token_is(at(t, q), "const") || token_is(at(t, q), "volatile") || token_char(at(t, q), '*'))) ++q;
    return q < end && at(t, q)->kind == TOK_RPAREN;
}

static bool try_emit_defer_block(Transpiler *t, NamespaceStack *ns, size_t *pp, size_t end)
{
    size_t p = *pp;
    size_t open, close;
    DeferScope *scope;
    Buffer emitted;
    if (!token_is(at(t, p), "defer") || p + 1 >= end || at(t, p + 1)->kind != TOK_LBRACE) return false;
    open = p + 1;
    close = matching(t, open, TOK_LBRACE, TOK_RBRACE, end);
    scope = defer_stack_current(&t->defers);
    if (!scope) return false;
    buffer_init(&emitted);
    emit_fragment_to_buffer(&emitted, t, ns, open + 1, close);
    buffer_puts(&scope->output, emitted.data);
    buffer_free(&emitted);
    *pp = close + 1;
    return true;
}

static void emit_fragment_with_substitution(Transpiler *t, NamespaceStack *ns, size_t begin, size_t end)
{
    size_t p = begin;
    while (p < end) {
        Token *x = at(t, p);
        if (x->kind == TOK_IDENTIFIER && x->length == 1 && x->begin[0] == '_') {
            emit_ws(&t->output, x);
            buffer_puts(&t->output, "__switch_value");
        } else {
            emit_full(&t->output, x);
        }
        ++p;
    }
}

static bool try_emit_switch(Transpiler *t, NamespaceStack *ns, size_t *pp, size_t end)
{
    size_t p = *pp, lp, rp, body_open, body_close, i, case_begin = end, clause_begin = end;
    bool has_break = false;
    bool saw_case = false;
    if (!token_is(at(t, p), "switch") || p + 1 >= end || at(t, p + 1)->kind != TOK_LPAREN) return false;
    lp = p + 1;
    rp = matching(t, lp, TOK_LPAREN, TOK_RPAREN, end);
    if (rp + 1 >= end || at(t, rp + 1)->kind != TOK_LBRACE) return false;
    body_open = rp + 1;
    body_close = matching(t, body_open, TOK_LBRACE, TOK_RBRACE, end);
    for (i = body_open + 1; i < body_close; ++i) {
        Token *x = at(t, i);
        if (x->kind == TOK_IDENTIFIER && token_is(x, "break")) {
            has_break = true;
            break;
        }
    }
    if (has_break) return false;

    emit_ws(&t->output, at(t, p));
    buffer_puts(&t->output, "({ int __switch_value = ");
    emit_fragment_with_substitution(t, ns, p + 2, rp);
    buffer_puts(&t->output, "; ");
    i = body_open + 1;
    while (i < body_close) {
        Token *x = at(t, i);
        if (x->kind == TOK_IDENTIFIER && token_is(x, "case")) {
            size_t label_begin = i + 1, label_end = i + 1, colon = body_close, j;
            bool range = false;
            bool inclusive = false;
            size_t range_pos = body_close;
            if (label_begin >= body_close) break;
            while (label_end < body_close) {
                Token *z = at(t, label_end);
                if (z->kind == TOK_COLON) {
                    colon = label_end;
                    break;
                }
                if (z->kind == TOK_RANGE) {
                    range = true;
                    inclusive = false;
                    range_pos = label_end;
                } else if (z->kind == TOK_RANGE_INCLUSIVE) {
                    range = true;
                    inclusive = true;
                    range_pos = label_end;
                }
                ++label_end;
            }
            if (colon >= body_close) break;
            if (!saw_case) {
                buffer_puts(&t->output, "if (");
            } else {
                buffer_puts(&t->output, "else if (");
            }
            if (!range) {
                bool predicate = false;
                size_t k;

                // Detect pattern cases like: case foo(_):
                for (k = label_begin; k < colon; ++k) {
                    Token *z = at(t, k);

                    if (z->kind == TOK_IDENTIFIER && token_is(z, "_")) {
                        predicate = true;
                        break;
                    }
                }

                if (predicate) {
                    emit_fragment_with_substitution(t, ns, label_begin, colon);
                } else {
                    buffer_puts(&t->output, "(__switch_value == ");
                    emit_fragment_with_substitution(t, ns, label_begin, colon);
                    buffer_puts(&t->output, ")");
                }
            } else {
                buffer_puts(&t->output, "(__switch_value >= ");
                emit_fragment_with_substitution(t, ns, label_begin, range_pos);
                buffer_puts(&t->output, ") && (__switch_value ");
                if (inclusive) {
                    buffer_puts(&t->output, "<=");
                } else {
                    buffer_puts(&t->output, "<");
                }
                buffer_putc(&t->output, ' ');
                emit_fragment_with_substitution(t, ns, range_pos + 1, colon);
                buffer_puts(&t->output, ")");
            }
            buffer_puts(&t->output, ") { ");
            saw_case = true;
            j = colon + 1;
            while (j < body_close) {
                Token *z = at(t, j);
                if (z->kind == TOK_IDENTIFIER && token_is(z, "case")) break;
                if (z->kind == TOK_IDENTIFIER && token_is(z, "default")) break;
                emit_full(&t->output, z);
                ++j;
            }
            buffer_puts(&t->output, " } ");
            i = j;
            continue;
        }
        if (x->kind == TOK_IDENTIFIER && token_is(x, "default")) {
            size_t colon = i + 1, j;
            if (colon < body_close && at(t, colon)->kind == TOK_COLON) {
                buffer_puts(&t->output, "else { ");
                j = colon + 1;
                while (j < body_close) {
                    Token *z = at(t, j);
                    if (z->kind == TOK_IDENTIFIER && token_is(z, "case")) break;
                    if (z->kind == TOK_IDENTIFIER && token_is(z, "default")) break;
                    emit_full(&t->output, z);
                    ++j;
                }
                buffer_puts(&t->output, " } ");
                i = j;
                continue;
            }
        }
        ++i;
    }
    buffer_puts(&t->output, " });");
    *pp = body_close + 1;
    return true;
}

static bool try_emit_header_include(Transpiler *t, size_t *pp)
{
    size_t p = *pp, n, dot = 0;
    Token *x = at(t, p);
    char *s;
    if (x->kind != TOK_STRING || p == 0 || !token_is(at(t, p - 1), "include") || x->length < 5 || x->begin[0] != '"') return false;
    for (n = 1; n + 1 < x->length; ++n) {
        if (x->begin[n] == '.') dot = n;
    }
    if (!dot || n + 1 != x->length || !((x->length - dot == 4 && memcmp(x->begin + dot, ".hx\"", 4) == 0) || (x->length - dot == 4 && memcmp(x->begin + dot, ".h+\"", 4) == 0))) return false;
    s = xmalloc(dot + 4);
    memcpy(s, x->begin, dot);
    memcpy(s + dot, ".h\"", 3);
    s[dot + 3] = 0;
    emit_ws(&t->output, x);
    buffer_puts(&t->output, s);
    free(s);
    ++*pp;
    return true;
}

static bool try_emit_method_call(Transpiler *t, NamespaceStack *ns, size_t *pp, size_t end)
{
    size_t p = *pp, op, method, lp;
    Token *x = at(t, p);
    char *receiver, *mn;
    Symbol *type, *m;
    int ptr = 0;
    bool arrow = false, pass_receiver;

    if (x->kind != TOK_IDENTIFIER || p + 3 >= end) return false;
    if (at(t, p + 1)->kind == TOK_DOT) {
        op = p + 1;
        method = p + 2;
        lp = p + 3;
    } else if (token_char(at(t, p + 1), '-') && at(t, p + 2)->kind == TOK_GT) {
        arrow = true;
        op = p + 1;
        method = p + 3;
        lp = p + 4;
        if (lp >= end) return false;
    } else {
        return false;
    }
    (void)op;
    if (at(t, method)->kind != TOK_IDENTIFIER || at(t, lp)->kind != TOK_LPAREN) return false;
    receiver = token_text(x);
    type = lookup_value(t, ns, receiver, &ptr);
    if (!type) {
        free(receiver);
        return false;
    }
    mn = token_text(at(t, method));
    m = method_for(t, type->qualified_name, mn);
    free(mn);
    if (!m) {
        free(receiver);
        return false;
    }
    if (arrow && ptr < 1) {
        free(receiver);
        die_at(x, "'->' method call requires a pointer receiver");
    }
    pass_receiver = !m->is_static || m->receiver_pointer_depth > 0;
    if (pass_receiver && m->receiver_pointer_depth > 0 && (ptr ? ptr : 1) != m->receiver_pointer_depth) {
        warn_at(x, "receiver pointer depth differs from the method's declared receiver; generated C may warn");
    }
    emit_ws(&t->output, x);
    buffer_puts(&t->output, m->mangled_name);
    buffer_putc(&t->output, '(');
    if (pass_receiver) {
        if (ptr == 0) buffer_putc(&t->output, '&');
        buffer_puts(&t->output, receiver);
        if (at(t, lp + 1)->kind != TOK_RPAREN) buffer_putc(&t->output, ',');
    }
    free(receiver);
    *pp = lp + 1;
    return true;
}

static void emit_one(Transpiler *t, NamespaceStack *ns, size_t *pp, size_t end)
{
    size_t p = *pp, used = 0;
    Token *x = at(t, p);
    char *q;
    Symbol *s;
    bool qualified;

    if (try_emit_expression_method_call(t, ns, pp, end)) return;
    if (try_emit_parenthesized_method_call(t, ns, pp, end)) return;
    if (try_emit_struct_literal_method_call(t, ns, pp, end)) return;
    if (try_emit_method_call(t, ns, pp, end)) return;
    if (try_emit_struct_literal(t, ns, pp, end)) return;
    if (try_emit_defer_block(t, ns, pp, end)) return;
    if (try_emit_switch(t, ns, pp, end)) return;
    if (try_emit_header_include(t, pp)) return;
    if (looks_like_c_style_cast(t, ns, p, end)) {
        warn_at(x, "C-style cast; prefer static_cast<Type>(expression)");
    }
    if (token_is(x, "static_cast") && p + 1 < end && at(t, p + 1)->kind == TOK_LT) {
        size_t close = matching(t, p + 1, TOK_LT, TOK_GT, end), i;
        emit_ws(&t->output, x);
        buffer_putc(&t->output, '(');
        for (i = p + 2; i < close; ) {
            Token *z = at(t, i);
            if (z->kind == TOK_IDENTIFIER) {
                size_t u = 0;
                char *qq = read_qualified(t, i, close, &u);
                Symbol *ts = qq ? resolve_name(&t->symbols, ns, qq, SYM_TYPE | SYM_ENUM) : NULL;
                if (ts) {
                    emit_ws(&t->output, z);
                    buffer_puts(&t->output, ts->mangled_name);
                    free(qq);
                    i += u;
                    continue;
                }
                free(qq);
            }
            emit_full(&t->output, z);
            ++i;
        }
        buffer_putc(&t->output, ')');
        *pp = close + 1;
        return;
    }
    if (x->kind == TOK_IDENTIFIER && token_is(x, "nullptr")) {
        emit_ws(&t->output, x);
        buffer_puts(&t->output, "((void *)0)");
        ++*pp;
        return;
    }
    if (x->kind == TOK_IDENTIFIER) {
        q = read_qualified(t, p, end, &used);
        qualified = q && strstr(q, "::") != NULL;
        if (qualified) {
            s = resolve_name(&t->symbols, ns, q, SYM_TYPE | SYM_ENUM | SYM_ENUM_MEMBER | SYM_FUNCTION | SYM_METHOD | SYM_VARIABLE);
            if (!s) {
                free(q);
                die_at(x, "unknown qualified name");
            }
            emit_ws(&t->output, x);
            buffer_puts(&t->output, s->mangled_name);
            free(q);
            *pp += used;
            return;
        }
        free(q);
        {
            char *name = token_text(x);
            Local *local = local_lookup(&t->locals, name);
            Symbol *var = resolve_name(&t->symbols, ns, name, SYM_VARIABLE);
            if (local) {
                emit_full(&t->output, x);
                free(name);
                ++*pp;
                return;
            }
            if (var) {
                emit_ws(&t->output, x);
                buffer_puts(&t->output, var->mangled_name);
                free(name);
                ++*pp;
                return;
            }
            s = resolve_name(&t->symbols, ns, name, SYM_TYPE | SYM_ENUM);
            if (s) {
                emit_ws(&t->output, x);
                buffer_puts(&t->output, s->mangled_name);
                free(name);
                ++*pp;
                return;
            }
            if (p + 1 < end && at(t, p + 1)->kind == TOK_LPAREN && !is_c_keyword(x)) {
                s = resolve_name(&t->symbols, ns, name, SYM_FUNCTION);
                if (s) {
                    emit_ws(&t->output, x);
                    buffer_puts(&t->output, s->mangled_name);
                    free(name);
                    ++*pp;
                    return;
                }
            }
            free(name);
        }
    }
    emit_full(&t->output, x);
    ++*pp;
}

static void add_parameters(Transpiler *t, NamespaceStack *ns, size_t begin, size_t end, const Symbol *owner, Symbol *method, bool method_static, const Token *where)
{
    size_t p = begin, ordinal = 0;
    if (begin == end) {
        if (owner && !method_static) die_at(where, "non-static method requires its first parameter to be a pointer to its struct");
        return;
    }
    if (end == begin + 1 && token_is(at(t, begin), "void")) {
        if (owner && !method_static) die_at(where, "non-static method requires its first parameter to be a pointer to its struct");
        return;
    }
    while (p < end) {
        size_t q = p;
        VarDecl d;
        while (q < end && at(t, q)->kind != TOK_COMMA) ++q;
        if (parse_var_decl(t, ns, p, q, &d)) {
            char *name = token_text(at(t, d.name));
            if (owner && ordinal == 0) {
                if (!method_static && (strcmp(d.type->qualified_name, owner->qualified_name) != 0 || d.pointer_depth < 1)) {
                    free(name);
                    die_at(where, "non-static method requires its first parameter to be a pointer to its struct");
                }
                /* A static method may choose a same-struct pointer as its first
                 * ordinary parameter.  In instance syntax that parameter is a
                 * receiver; in qualified syntax it remains explicit. */
                if (method && strcmp(d.type->qualified_name, owner->qualified_name) == 0 && d.pointer_depth >= 1) {
                    method->receiver_pointer_depth = d.pointer_depth;
                }
            }
            local_add(&t->locals, name, d.type->qualified_name, d.pointer_depth, 1);
            free(name);
        } else if (owner && ordinal == 0 && !method_static) {
            die_at(where, "non-static method requires its first parameter to be a pointer to its struct");
        }
        ++ordinal;
        p = q + 1;
    }
}

static void emit_body(Transpiler *t, NamespaceStack *ns, size_t begin, size_t close)
{
    size_t p = begin;
    t->local_depth = 1;
    defer_stack_push(&t->defers);
    while (p < close) {
        VarDecl d;
        if (parse_var_decl(t, ns, p, close, &d)) {
            char *name = token_text(at(t, d.name));
            local_add(&t->locals, name, d.type->qualified_name, d.pointer_depth, t->local_depth);
            free(name);
        }
        if (at(t, p)->kind == TOK_LBRACE) {
            defer_stack_push(&t->defers);
            emit_full(&t->output, at(t, p));
            ++t->local_depth;
            ++p;
            continue;
        }
        if (at(t, p)->kind == TOK_RBRACE) {
            DeferScope *scope = defer_stack_current(&t->defers);
            if (scope && scope->output.len) {
                buffer_puts(&t->output, scope->output.data);
            }
            defer_stack_pop(&t->defers);
            locals_leave(&t->locals, t->local_depth);
            --t->local_depth;
            emit_full(&t->output, at(t, p));
            ++p;
            continue;
        }
        emit_one(t, ns, &p, close);
    }
    if (t->defers.count) {
        DeferScope *scope = defer_stack_current(&t->defers);
        if (scope && scope->output.len) {
            buffer_puts(&t->output, scope->output.data);
        }
    }
    defer_stack_pop(&t->defers);
}

static void transpile_function(Transpiler *t, NamespaceStack *ns, size_t start, const Callable *c)
{
    size_t p = start, mark = locals_mark(&t->locals);
    add_parameters(t, ns, c->lparen + 1, c->rparen, NULL, NULL, false, at(t, c->name));
    while (p < c->body) emit_one(t, ns, &p, c->body);
    emit_full(&t->output, at(t, c->body));
    emit_body(t, ns, c->body + 1, c->close);
    emit_full(&t->output, at(t, c->close));
    locals_restore(&t->locals, mark);
}

static void transpile_method(Transpiler *t, NamespaceStack *ns, size_t start, const Callable *c, Symbol *owner, Symbol *method)
{
    size_t p = start, mark = locals_mark(&t->locals);
    const char *old_struct = t->current_struct, *old_method = t->current_method;
    bool old_static = t->current_method_static;

    t->current_struct = owner->qualified_name;
    t->current_method = method->qualified_name;
    t->current_method_static = method->is_static;
    add_parameters(t, ns, c->lparen + 1, c->rparen, owner, method, method->is_static, at(t, c->name));

    while (p < c->body) {
        if (p == start && c->is_static && token_is(at(t, p), "static")) {
            ++p;
            continue;
        }
        if (p == c->name) {
            emit_ws(&t->output, at(t, p));
            buffer_puts(&t->output, method->mangled_name);
            ++p;
            continue;
        }
        emit_one(t, ns, &p, c->body);
    }
    emit_full(&t->output, at(t, c->body));
    emit_body(t, ns, c->body + 1, c->close);
    emit_full(&t->output, at(t, c->close));
    locals_restore(&t->locals, mark);
    t->current_struct = old_struct;
    t->current_method = old_method;
    t->current_method_static = old_static;
}

static void transpile_struct(Transpiler *t, NamespaceStack *ns, size_t start, size_t open, size_t close)
{
    size_t p, after = close + 1, nused;
    char *name = token_text(at(t, start + 1)), *q = qualify(ns, ns->count, name);
    Symbol *owner = symbol_exact(&t->symbols, q, SYM_TYPE);

    emit_ws(&t->output, at(t, start));
    buffer_puts(&t->output, "typedef struct ");
    buffer_puts(&t->output, owner->mangled_name);
    emit_full(&t->output, at(t, open));

    for (p = open + 1; p < close; ) {
        Callable c;
        if (parse_callable(t, p, close, &c)) {
            p = c.after;
            continue;
        }
        emit_one(t, ns, &p, close);
    }
    emit_ws(&t->output, at(t, close));
    buffer_puts(&t->output, "} ");
    buffer_puts(&t->output, owner->mangled_name);
    buffer_putc(&t->output, ';');
    if (after < t->tokens.count && at(t, after)->kind == TOK_SEMICOLON) ++after;

    for (p = open + 1; p < close; ) {
        Callable c;
        if (parse_callable(t, p, close, &c)) {
            char *mn = token_text(at(t, c.name));
            Buffer b;
            Symbol *method;
            buffer_init(&b);
            buffer_puts(&b, owner->qualified_name);
            buffer_puts(&b, "::");
            buffer_puts(&b, mn);
            method = symbol_exact(&t->symbols, b.data, SYM_METHOD);
            free(mn);
            buffer_free(&b);
            if (c.body < close) {
                buffer_putc(&t->output, '\n');
                transpile_method(t, ns, p, &c, owner, method);
            }
            p = c.after;
            continue;
        }
        ++p;
    }
    (void)nused;
    free(name);
    free(q);
}

static void transpile_enum(Transpiler *t, NamespaceStack *ns, size_t start, size_t open, size_t close)
{
    size_t p, after = close + 1;
    bool want = true;
    char *name = token_text(at(t, start + 1)), *q = qualify(ns, ns->count, name);
    Symbol *owner = symbol_exact(&t->symbols, q, SYM_ENUM);

    emit_ws(&t->output, at(t, start));
    buffer_puts(&t->output, "typedef enum ");
    buffer_puts(&t->output, owner->mangled_name);
    emit_full(&t->output, at(t, open));

    for (p = open + 1; p < close; ) {
        if (want && at(t, p)->kind == TOK_IDENTIFIER) {
            char *field = token_text(at(t, p));
            Buffer b;
            Symbol *member;
            buffer_init(&b);
            buffer_puts(&b, owner->qualified_name);
            buffer_puts(&b, "::");
            buffer_puts(&b, field);
            member = symbol_exact(&t->symbols, b.data, SYM_ENUM_MEMBER);
            emit_ws(&t->output, at(t, p));
            buffer_puts(&t->output, member->mangled_name);
            free(field);
            buffer_free(&b);
            ++p;
            want = false;
            continue;
        }
        if (at(t, p)->kind == TOK_COMMA) want = true;
        emit_one(t, ns, &p, close);
    }
    emit_ws(&t->output, at(t, close));
    buffer_puts(&t->output, "} ");
    buffer_puts(&t->output, owner->mangled_name);
    buffer_putc(&t->output, ';');
    if (after < t->tokens.count && at(t, after)->kind == TOK_SEMICOLON) ++after;
    free(name);
    free(q);
}

static void transpile_range(Transpiler *t, NamespaceStack *ns, size_t begin, size_t end)
{
    size_t p = begin;
    while (p < end) {
        Token *x = at(t, p);
        Callable c;

        if (token_is(x, "namespace") && p + 2 < end && at(t, p + 1)->kind == TOK_IDENTIFIER && at(t, p + 2)->kind == TOK_LBRACE) {
            size_t close = matching(t, p + 2, TOK_LBRACE, TOK_RBRACE, end);
            emit_ws(&t->output, x);
            ns_push(ns, at(t, p + 1));
            transpile_range(t, ns, p + 3, close);
            ns_pop(ns);
            emit_ws(&t->output, at(t, close));
            p = close + 1;
            continue;
        }
        if ((token_is(x, "struct") || token_is(x, "enum")) && p + 2 < end && at(t, p + 1)->kind == TOK_IDENTIFIER && at(t, p + 2)->kind == TOK_LBRACE) {
            size_t close = matching(t, p + 2, TOK_LBRACE, TOK_RBRACE, end);
            if (token_is(x, "struct")) {
                transpile_struct(t, ns, p, p + 2, close);
            } else {
                transpile_enum(t, ns, p, p + 2, close);
            }
            p = close + 1;
            if (p < end && at(t, p)->kind == TOK_SEMICOLON) ++p;
            continue;
        }
        if (parse_callable(t, p, end, &c) && c.body < end) {
            transpile_function(t, ns, p, &c);
            p = c.after;
            continue;
        }
        emit_one(t, ns, &p, end);
    }
}

static void transpile(Transpiler *t)
{
    NamespaceStack ns;
    ns_init(&ns);
    buffer_puts(&t->output, "/* Generated by C+ compiler. DO NOT EDIT. */\n#include <stdbool.h>\n#include <stddef.h>\n\n");
    discover_range(t, &ns, 0, t->tokens.count - 1);
    transpile_range(t, &ns, 0, t->tokens.count - 1);
    ns_free(&ns);
}

/* ------------------------------------------------------------------------- */
/* Indexing and backend                                                      */

static void emit_ide_index(const TokenList *list)
{
    size_t i, j;
    printf("[\n");
    for (i = 0; i < list->count && list->items[i].kind != TOK_EOF; ++i) {
        const Token *t = &list->items[i];
        const char *k = "OTHER";
        if (t->kind == TOK_IDENTIFIER) {
            k = is_c_keyword(t) ? "KEYWORD" : "IDENTIFIER";
        } else if (t->kind == TOK_NUMBER) {
            k = "NUMBER";
        } else if (t->kind == TOK_STRING) {
            k = "STRING";
        } else if (t->kind == TOK_CHAR) {
            k = "CHAR";
        } else if (t->kind >= TOK_LBRACE && t->kind <= TOK_GT) {
            k = "PUNCTUATION";
        }
        printf("  { \"line\": %zu, \"column\": %zu, \"length\": %zu, \"kind\": \"%s\", \"text\": \"", t->line, t->column, t->length, k);
        for (j = 0; j < t->length; ++j) {
            char c = t->begin[j];
            if (c == '"') printf("\\\"");
            else if (c == '\\') printf("\\\\");
            else if (c == '\n') printf("\\n");
            else putchar(c);
        }
        printf("\" }%s\n", i + 1 < list->count && list->items[i + 1].kind != TOK_EOF ? "," : "");
    }
    printf("]\n");
}

typedef struct {
    char **items;
    size_t count, cap;
} Arguments;

static void args_init(Arguments *a)
{
    memset(a, 0, sizeof(*a));
}

static void args_push(Arguments *a, const char *s)
{
    if (a->count == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 16;
        a->items = xrealloc(a->items, a->cap * sizeof(char *));
    }
    a->items[a->count++] = xstrdup(s);
}

static void args_free(Arguments *a)
{
    size_t i;
    for (i = 0; i < a->count; ++i) free(a->items[i]);
    free(a->items);
    memset(a, 0, sizeof(*a));
}

#ifdef _WIN32
static int run_process(Arguments *a)
{
    Buffer c;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    size_t i;
    buffer_init(&c);
    for (i = 0; i < a->count; ++i) {
        const char *s = a->items[i];
        if (i) buffer_putc(&c, ' ');
        buffer_putc(&c, '"');
        while (*s) {
            if (*s == '"' || *s == '\\') buffer_putc(&c, '\\');
            buffer_putc(&c, *s++);
        }
        buffer_putc(&c, '"');
    }
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, c.data, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        buffer_free(&c);
        fprintf(stderr, "c+: error: could not execute backend\n");
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    {
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        buffer_free(&c);
        return (int)code;
    }
}
#else
static int run_process(Arguments *a)
{
    char **v = xmalloc((a->count + 1) * sizeof(char *));
    size_t i;
    pid_t pid;
    int status;
    for (i = 0; i < a->count; ++i) v[i] = a->items[i];
    v[a->count] = NULL;
    pid = fork();
    if (pid < 0) {
        free(v);
        perror("c+: fork");
        return 1;
    }
    if (!pid) {
        execvp(v[0], v);
        perror("c+: exec");
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        free(v);
        perror("c+: waitpid");
        return 1;
    }
    free(v);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
#endif

// Helper function to extract a JSON string value given a key search context
static bool extract_json_value(const char *json, const char *key, char *dest, size_t dest_size) {
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);
    
    const char *p = strstr(json, search_pattern);
    if (!p) return false;
    
    // Move past the key and find the colon
    p = strchr(p, ':');
    if (!p) return false;
    
    // Skip whitespace and quotes
    while (*p && (*p == ':' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '"')) {
        p++;
    }
    
    // Copy until the closing quote or delimiter
    size_t i = 0;
    while (*p && *p != '"' && *p != ',' && *p != '}' && i < dest_size - 1) {
        dest[i++] = *p++;
    }
    dest[i] = '\0';
    return i > 0;
}

// Specific helper to target nested dependency versions (e.g., inside "dependencies": { "tcc": { "version": "..." } })
static bool extract_nested_dependency_version(const char *json, const char *dep_name, char *dest, size_t dest_size) {
    char dep_pattern[128];
    snprintf(dep_pattern, sizeof(dep_pattern), "\"%s\"", dep_name);
    
    const char *p = strstr(json, dep_pattern);
    if (!p) return false;
    
    // Search for "version" subsequent to the dependency block declaration
    return extract_json_value(p, "version", dest, dest_size);
}

static void usage(void)
{
    char cplus_version[32] = "unknown";
    char *manifest = read_file("manifest.json");
    if (manifest) {
        extract_json_value(manifest, "version", cplus_version, sizeof(cplus_version));
        free(manifest);
    }

    printf(
        "C+ compiler (v%s)\n\n"
        "Version:\n"
        "  %s\n"
        "Extensions:\n"
        "  .cp\n"
        "  .c+\n"
        "  .hp\n"
        "  .h+\n\n"
        "Usage:\n"
        "  c+    [options] file.('cp'|'c+')\n"
        "  cc+   [options] file.('cp'|'c+')\n"
        "  cplus [options] file.('cp'|'c+')\n\n"
        "  Options:\n"
        "  -v, --version Outputs the installed C+ version and exits\n"
        "  -h, --help    Outputs this message and exits\n"
        "  -o <file>     Output executable or file\n"
        "  -c            Compile to an object file\n"
        "  -C            Emit generated C only and exit\n"
        "  -D <macro>    Define preprocessor macro (forwarded to TCC)\n"
        "  -I <dir>      Add include directory (forwarded to TCC)\n"
        "  --keep-c      Keep generated .gencx.c file\n"
        "  --index       Emit token index for IDE highlighting services\n"
        "  --            Stop processing C+ options\n",
        cplus_version, cplus_version
    );
}

static void version(void)
{
    char cplus_version[32] = "unknown";
    char tcc_version[32] = "unknown";
    
    char *manifest = read_file("manifest.json");
    if (manifest) {
        extract_json_value(manifest, "version", cplus_version, sizeof(cplus_version));
        extract_nested_dependency_version(manifest, "tcc", tcc_version, sizeof(tcc_version));
        free(manifest);
    }

    printf(
        "C+ version:\n"
        "  %s\n"
        "TCC version:\n"
        "  %s\n",
        cplus_version,
        tcc_version
    );
}

static char *generated_name(const char *in)
{
    size_t n = strlen(in);
    char *s = xmalloc(n + 9);
    snprintf(s, n + 9, "%s.gencx.c", in);
    return s;
}

static char *replace_extension(const char *s, const char *ext)
{
    const char *dot = strrchr(s, '.');
    size_t n = dot ? (size_t)(dot - s) : strlen(s), e = strlen(ext);
    char *r = xmalloc(n + e + 1);
    memcpy(r, s, n);
    memcpy(r + n, ext, e + 1);
    return r;
}

int main(int argc, char **argv)
{
    const char *input = NULL, *output = NULL;
    bool emit_c = false, compile_only = false, keep = false, index = false, endopt = false, notify = false;
    Arguments backend, command;
    char *source, *generated;
    Lexer lex;
    TokenList toks;
    Transpiler t;
    int i, status;

    if (argc < 2) {
        usage();
        return 1;
    }
    args_init(&backend);

    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!endopt && strcmp(a, "--") == 0) {
            endopt = true;
            continue;
        }
        if (!endopt && (!strcmp(a, "-h") || !strcmp(a, "--help")) /* checks for -h or --help */) {
            usage();
            return 0;
        }
        if (!endopt && (!strcmp(a, "-v") || !strcmp(a, "--version")) /* checks for -v or --version */) {
            version();
            return 0;
        }
        if (!endopt && strcmp(a, "-o") == 0) {
            if (++i >= argc) die("-o requires an argument");
            output = argv[i];
            continue;
        }
        if (!endopt && strcmp(a, "-w") == 0) {
            ALLOW_WARNINGS = false;
            continue;
        }
        if (!endopt && strcmp(a, "-C") == 0) {
            emit_c = true;
            continue;
        }
        if (!endopt && strcmp(a, "-c") == 0) {
            compile_only = true;
            args_push(&backend, a);
            continue;
        }
        if (!endopt && (strcmp(a, "-D") == 0 || strcmp(a, "-I") == 0)) {
            if (++i >= argc) die("-D/-I requires an argument");
            args_push(&backend, a);
            args_push(&backend, argv[i]);
            continue;
        }
        if (!endopt && strcmp(a, "--keep-c") == 0) {
            keep = true;
            continue;
        }
        if (!endopt && strcmp(a, "--index") == 0) {
            index = true;
            continue;
        }
        if (!endopt && strcmp(a, "--notify") == 0) {
            notify = true;
            continue;
        }
        if (!endopt && a[0] != '-') {
            if (input) die("multiple input files are not supported yet");
            input = a;
            continue;
        }
        args_push(&backend, a);
    }

    if (!input) die("no input .c+ file specified");
    if (!output) {
        if (compile_only) {
            output = replace_extension(input, ".o");
        } else {
#ifdef _WIN32
            output = "out.exe";
#else
            output = "out";
#endif
        }
    }

    source = read_file(input);
    if (!source) {
        fprintf(stderr, "c+: error: could not read '%s': %s\n", input, strerror(errno));
        args_free(&backend);
        return 1;
    }

    {
        char *preprocessed = preprocess_source(input, source, 0);
        free(source);
        source = preprocessed;
    }

    lexer_init(&lex, source);
    tokens_init(&toks);
    for (;;) {
        Token x = lexer_next(&lex);
        tokens_push(&toks, x);
        if (x.kind == TOK_EOF) break;
    }

    if (index) {
        emit_ide_index(&toks);
        tokens_free(&toks);
        free(source);
        args_free(&backend);
        return 0;
    }

    memset(&t, 0, sizeof(t));
    t.source = source;
    t.tokens = toks;
    symbols_init(&t.symbols);
    locals_init(&t.locals);
    buffer_init(&t.output);
    transpile(&t);

    generated = generated_name(input);
    {
        FILE *f = fopen(generated, "wb");
        if (!f) die("could not create generated C file");
        fwrite(t.output.data, 1, t.output.len, f);
        fclose(f);
    }

    if (emit_c) {
        printf("C+ -> C: %s\n", generated);
        free(generated);
        buffer_free(&t.output);
        locals_free(&t.locals);
        symbols_free(&t.symbols);
        tokens_free(&toks);
        free(source);
        args_free(&backend);
        return 0;
    }

    args_init(&command);
    args_push(&command, "tcc");
    args_push(&command, "-w");
    for (i = 0; i < (int)backend.count; ++i) {
        args_push(&command, backend.items[i]);
    }
    args_push(&command, "-o");
    args_push(&command, output);
    args_push(&command, generated);
    //printf("[C+] Backend: tcc -> %s\n", output);

    status = run_process(&command);
    if (status == 0 && !keep) remove(generated);
    if (status != 0) {
        fprintf(stderr, "c+: error: backend compilation failed (exit code %d)\n", status);
        fprintf(stderr, "c+: generated C was kept at: %s\n", generated);
    }

    if (!status /* aka status == 0 */ && notify) fprintf(stdout, "[C+] compilation of '%s' successfully finished!\n", input);
    args_free(&command);
    args_free(&backend);
    free(generated);
    buffer_free(&t.output);
    locals_free(&t.locals);
    symbols_free(&t.symbols);
    tokens_free(&toks);
    free(source);
    return status;
}