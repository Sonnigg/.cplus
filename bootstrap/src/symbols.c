#include "symbols.h"

void ns_init(NamespaceStack *ns)
{
    memset(ns, 0, sizeof(*ns));
}

void ns_push(NamespaceStack *ns, const Token *tok)
{
    if (ns->count == ns->cap) {
        ns->cap = ns->cap ? ns->cap * 2 : 8;
        ns->items = xrealloc(ns->items, ns->cap * sizeof(char *));
    }
    ns->items[ns->count++] = token_text(tok);
}

void ns_pop(NamespaceStack *ns)
{
    if (ns->count) free(ns->items[--ns->count]);
}

void ns_free(NamespaceStack *ns)
{
    while (ns->count) ns_pop(ns);
    free(ns->items);
    memset(ns, 0, sizeof(*ns));
}

char *ns_prefix(const NamespaceStack *ns, size_t depth)
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

char *qualify(const NamespaceStack *ns, size_t depth, const char *name)
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

char *mangle_qualified_name(const char *qualified)
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

void symbols_init(SymbolRegistry *r)
{
    memset(r, 0, sizeof(*r));
}

void symbols_free(SymbolRegistry *r)
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

Symbol *symbol_exact(SymbolRegistry *r, const char *q, unsigned mask)
{
    size_t i;
    for (i = 0; i < r->count; ++i) {
        if ((r->items[i].kind & mask) && strcmp(r->items[i].qualified_name, q) == 0) {
            return &r->items[i];
        }
    }
    return NULL;
}

Symbol *symbols_add(SymbolRegistry *r, const char *q, unsigned kind, const char *owner, const char *type, int depth, bool is_static)
{
    Symbol *old = symbol_exact(r, q, kind);
    size_t i;
    const char *last;
    Symbol *s;

    if (old) return old;
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

Symbol *resolve_name(SymbolRegistry *r, const NamespaceStack *ns, const char *spelling, unsigned mask)
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