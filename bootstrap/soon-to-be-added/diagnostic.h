#pragma once
#include "common.h"
#include "lexer.h"

typedef enum {
    DIAG_NOTE,
    DIAG_HELP,
    DIAG_WARNING,
    DIAG_ERROR,
    DIAG_ICE
} DiagnosticLevel;

typedef enum {
    CAT_LEXICAL,
    CAT_PREPROCESSOR,
    CAT_SYNTAX,
    CAT_SEMANTIC,
    CAT_INTERNAL
} DiagnosticCategory;

typedef struct {
    const char *filename;
    const char *source;
    int error_count;
    int warning_count;
    bool use_colors;
    const char *context_stack[16];
    size_t context_depth;
} DiagnosticEngine;

/* Global engine for lexical/preprocessing passes before transpiler context exists */
extern DiagnosticEngine global_diag;

void diag_init(DiagnosticEngine *env, const char *filename, const char *source);
void diag_push_context(DiagnosticEngine *env, const char *ctx);
void diag_pop_context(DiagnosticEngine *env);
bool diag_has_errors(const DiagnosticEngine *env);

void diag_emit(DiagnosticEngine *env, DiagnosticLevel level, const char *code, const Token *tok, const char *msg, const char *label, const char *help);
void diag_ice(DiagnosticEngine *env, const char *file, int line, const Token *tok, const char *msg);

const char *token_kind_name(TokenKind k);

#define ICE(env, tok, msg) diag_ice(env, __FILE__, __LINE__, tok, msg)