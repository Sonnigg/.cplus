#include "diagnostic.h"
#include <stdarg.h>

DiagnosticEngine global_diag;

void diag_init(DiagnosticEngine *env, const char *filename, const char *source) {
    memset(env, 0, sizeof(*env));
    env->filename = filename;
    env->source = source;
    env->use_colors = true; /* Can be toggled via CLI checks (e.g., isatty) */
}

void diag_push_context(DiagnosticEngine *env, const char *ctx) {
    if (env->context_depth < 16) {
        env->context_stack[env->context_depth++] = ctx;
    }
}

void diag_pop_context(DiagnosticEngine *env) {
    if (env->context_depth > 0) env->context_depth--;
}

bool diag_has_errors(const DiagnosticEngine *env) {
    return env->error_count > 0;
}

const char *token_kind_name(TokenKind k) {
    switch (k) {
        case TOK_EOF: return "EOF";
        case TOK_IDENTIFIER: return "identifier";
        case TOK_NUMBER: return "number";
        case TOK_STRING: return "string literal";
        case TOK_CHAR: return "character literal";
        case TOK_LBRACE: return "'{'";
        case TOK_RBRACE: return "'}'";
        case TOK_LPAREN: return "'('";
        case TOK_RPAREN: return "')'";
        case TOK_LBRACKET: return "'['";
        case TOK_RBRACKET: return "']'";
        case TOK_SEMICOLON: return "';'";
        case TOK_COMMA: return "','";
        case TOK_DOT: return "'.'";
        case TOK_COLON: return "':'";
        case TOK_SCOPE: return "'::'";
        case TOK_LT: return "'<'";
        case TOK_GT: return "'>'";
        case TOK_ARROW: return "'->'";
        default: return "token";
    }
}

void diag_emit(DiagnosticEngine *env, DiagnosticLevel level, const char *code, const Token *tok, const char *msg, const char *label, const char *help) {
    if (level == DIAG_ERROR) env->error_count++;
    if (level == DIAG_WARNING) env->warning_count++;

    const char *c_err   = env->use_colors ? "\x1B[31;1m" : "";
    const char *c_warn  = env->use_colors ? "\x1B[33;1m" : "";
    const char *c_note  = env->use_colors ? "\x1B[36;1m" : "";
    const char *c_blue  = env->use_colors ? "\x1B[34;1m" : "";
    const char *c_bold  = env->use_colors ? "\x1B[1m" : "";
    const char *c_reset = env->use_colors ? "\x1B[0m" : "";

    const char *lvl_str = "";
    const char *c_lvl = "";
    switch (level) {
        case DIAG_ERROR:   lvl_str = "error";   c_lvl = c_err;  break;
        case DIAG_WARNING: lvl_str = "warning"; c_lvl = c_warn; break;
        case DIAG_NOTE:    lvl_str = "note";    c_lvl = c_note; break;
        case DIAG_HELP:    lvl_str = "help";    c_lvl = c_note; break;
        case DIAG_ICE:     lvl_str = "internal compiler error"; c_lvl = c_err; break;
    }

    /* Print header: error[E0123]: expected ';' */
    if (code) {
        fprintf(stderr, "%s%s[%s]%s: %s%s%s\n", c_lvl, lvl_str, code, c_reset, c_bold, msg, c_reset);
    } else {
        fprintf(stderr, "%s%s%s: %s%s%s\n", c_lvl, lvl_str, c_reset, c_bold, msg, c_reset);
    }

    if (tok && env->source && tok->kind != TOK_EOF) {
        fprintf(stderr, " %s-->%s %s:%zu:%zu\n", c_blue, c_reset, env->filename ? env->filename : "<source>", tok->line, tok->column);

        /* Isolate Snippet */
        const char *line_start = env->source;
        size_t current_line = 1;
        while (current_line < tok->line && *line_start) {
            if (*line_start == '\n') current_line++;
            line_start++;
        }

        /* Multiline Span Calculation */
        size_t end_offset = (size_t)(tok->begin - env->source) + tok->length;
        size_t end_line = tok->line;
        for (const char *p = tok->begin; p < env->source + end_offset; p++) {
            if (*p == '\n') end_line++;
        }

        fprintf(stderr, "%s%4zu |%s ", c_blue, tok->line, c_reset);
        
        /* Render First Line (handle tabs gracefully) */
        const char *line_end = line_start;
        while (*line_end && *line_end != '\n') line_end++;
        
        for (const char *p = line_start; p < line_end; p++) {
            if (*p == '\t') fprintf(stderr, "    ");
            else fputc(*p, stderr);
        }
        fprintf(stderr, "\n");

        /* Multiline indicator or single-line underline */
        fprintf(stderr, "%s     |%s ", c_blue, c_reset);
        if (end_line > tok->line) {
            fprintf(stderr, "%s^~~~ %s%s\n", c_lvl, label ? label : "span continues here", c_reset);
        } else {
            size_t spaces = 0;
            for (const char *p = line_start; p < tok->begin; p++) {
                if (*p == '\t') spaces += 4;
                else spaces += 1;
            }
            for (size_t i = 0; i < spaces; i++) fputc(' ', stderr);

            fprintf(stderr, "%s^", c_lvl);
            size_t underline = tok->length > 0 ? tok->length - 1 : 0;
            for (size_t i = 0; i < underline; i++) fputc('~', stderr);

            if (label) fprintf(stderr, " %s", label);
            fprintf(stderr, "%s\n", c_reset);
        }
    }

    if (help) {
        fprintf(stderr, "  %s=%s %shelp%s: %s\n", c_blue, c_reset, c_note, c_reset, help);
    }

    if (env->context_depth > 0) {
        for (int i = env->context_depth - 1; i >= 0; i--) {
            fprintf(stderr, "  %s=%s %snote%s: %s\n", c_blue, c_reset, c_note, c_reset, env->context_stack[i]);
        }
    }
    fprintf(stderr, "\n");
}

void diag_ice(DiagnosticEngine *env, const char *file, int line, const Token *tok, const char *msg) {
    fprintf(stderr, "\n\x1B[31;1minternal compiler error\x1B[0m\n");
    fprintf(stderr, "This is a compiler bug.\n");
    fprintf(stderr, "Please report this issue.\n\n");
    fprintf(stderr, "Location:\n    %s:%d\n\n", file, line);
    if (tok) {
        fprintf(stderr, "Token:\n    %s\n\n", token_kind_name(tok->kind));
    }
    fprintf(stderr, "Message:\n    %s\n\n", msg);
    exit(1);
}