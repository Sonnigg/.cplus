#pragma once
#include "common.h"
#include "lexer.h"

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

void ns_init(NamespaceStack *ns);
void ns_push(NamespaceStack *ns, const Token *tok);
void ns_pop(NamespaceStack *ns);
void ns_free(NamespaceStack *ns);
char *ns_prefix(const NamespaceStack *ns, size_t depth);
char *qualify(const NamespaceStack *ns, size_t depth, const char *name);

char *mangle_qualified_name(const char *qualified);

void symbols_init(SymbolRegistry *r);
void symbols_free(SymbolRegistry *r);
Symbol *symbol_exact(SymbolRegistry *r, const char *q, unsigned mask);
Symbol *symbols_add(SymbolRegistry *r, const char *q, unsigned kind, const char *owner, const char *type, int depth, bool is_static);
Symbol *resolve_name(SymbolRegistry *r, const NamespaceStack *ns, const char *spelling, unsigned mask);