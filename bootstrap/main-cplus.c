#include "common.h"
#include "lexer.h"
#include "transpiler.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#endif

bool ALLOW_WARNINGS = true;

#ifdef _WIN32
static double timer_now(void)
{
    static LARGE_INTEGER freq;
    static bool initialized = false;
    if (!initialized) {
        QueryPerformanceFrequency(&freq);
        initialized = true;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
}
#else
static double timer_now(void)
{
    return (double)clock() / (double)CLOCKS_PER_SEC;
}
#endif

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

static bool extract_json_value(const char *json, const char *key, char *dest, size_t dest_size) {
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);
    const char *p = strstr(json, search_pattern);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    while (*p && (*p == ':' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '"')) {
        p++;
    }
    size_t i = 0;
    while (*p && *p != '"' && *p != ',' && *p != '}' && i < dest_size - 1) {
        dest[i++] = *p++;
    }
    dest[i] = '\0';
    return i > 0;
}

static bool extract_nested_dependency_version(const char *json, const char *dep_name, char *dest, size_t dest_size) {
    char dep_pattern[128];
    snprintf(dep_pattern, sizeof(dep_pattern), "\"%s\"", dep_name);
    const char *p = strstr(json, dep_pattern);
    if (!p) return false;
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
        "Version:\n  %s\n"
        "Extensions:\n  .cp\n  .c+\n  .hp\n  .h+\n\n"
        "Usage:\n"
        "  c+    [options] file.('cp'|'c+') [...files]\n"
        "  cc+   [options] file.('cp'|'c+') [...files]\n"
        "  cplus [options] file.('cp'|'c+') [...files]\n\n"
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
    printf("C+ version:\n  %s\nTCC version:\n  %s\n", cplus_version, tcc_version);
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

static void print_stats(size_t source_lines, size_t source_bytes, size_t token_count, size_t generated_lines, size_t generated_bytes, double preprocess_time, double lex_time, double transpile_time, double backend_time, double total_time)
{
    double frontend_time = preprocess_time + lex_time + transpile_time;
    if (total_time <= 0.0) total_time = 0.000001;

    printf("\n==================================================\n");
    printf("                  C+ STATISTICS                   \n");
    printf("==================================================\n");
    printf("[ Input Metrics ]\n  Source Lines : %10zu\n  Source Bytes : %10zu bytes\n  Tokens       : %10zu\n", source_lines, source_bytes, token_count);
    printf("\n[ Output Metrics ]\n  Gen. Lines   : %10zu\n  Gen. Bytes   : %10zu bytes\n", generated_lines, generated_bytes);
    printf("\n[ Timing Breakdown ]\n");
    printf("  Preprocessor : %8.3f ms  (%5.1f%%)\n", preprocess_time * 1000, (preprocess_time / total_time) * 100.0);
    printf("  Lexer        : %8.3f ms  (%5.1f%%)\n", lex_time * 1000, (lex_time / total_time) * 100.0);
    printf("  Transpiler   : %8.3f ms  (%5.1f%%)\n", transpile_time * 1000, (transpile_time / total_time) * 100.0);
    printf("  ------------------------------------------------\n");
    printf("  Frontend     : %8.3f ms  (%5.1f%%)\n", frontend_time * 1000, (frontend_time / total_time) * 100.0);
    if (backend_time > 0) printf("  Backend(TCC) : %8.3f ms  (%5.1f%%)\n", backend_time * 1000, (backend_time / total_time) * 100.0);
    printf("  ------------------------------------------------\n  Total Time   : %8.3f ms\n", total_time * 1000);
    printf("\n[ Throughput & Ratios ]\n");
    printf("  Line Speed   : %10.0f lines/sec\n", source_lines / total_time);
    printf("  Token Speed  : %10.0f tokens/sec\n", token_count / total_time);
    printf("  Byte Speed   : %10.0f bytes/sec\n", source_bytes / total_time);
    printf("  Tokens/Line  : %10.2f\n", source_lines > 0 ? (double)token_count / (double)source_lines : 0.0);
    printf("  C Expansion  : %10.2fx\n", source_bytes > 0 ? (double)generated_bytes / (double)source_bytes : 0.0);
    printf("==================================================\n");
}

static void emit_ide_index(const TokenList *list)
{
    size_t i, j;
    printf("[\n");
    for (i = 0; i < list->count && list->items[i].kind != TOK_EOF; ++i) {
        const Token *t = &list->items[i];
        const char *k = "OTHER";
        if (t->kind == TOK_IDENTIFIER) k = is_c_keyword(t) ? "KEYWORD" : "IDENTIFIER";
        else if (t->kind == TOK_NUMBER) k = "NUMBER";
        else if (t->kind == TOK_STRING) k = "STRING";
        else if (t->kind == TOK_CHAR) k = "CHAR";
        else if (t->kind >= TOK_LBRACE && t->kind <= TOK_GT) k = "PUNCTUATION";
        
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

int main(int argc, char **argv)
{
    const char *output = NULL;
    char *allocated_output = NULL;
    bool emit_c = false, compile_only = false, keep = false, index = false, endopt = false, notify = false, stats = false;
    
    Arguments backend, command, inputs, generated_files;
    int i, status = 0;

    double total_start = timer_now();
    double preprocess_time = 0.0, lex_time = 0.0, transpile_time = 0.0, backend_time = 0.0;
    
    // We start line metrics at 0, incremented during file reading
    size_t source_lines = 0, source_bytes = 0, token_count = 0, generated_lines = 0, generated_bytes = 0;

    if (argc < 2) { usage(); return 1; }
    
    args_init(&backend);
    args_init(&inputs);
    args_init(&generated_files);

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!endopt && strcmp(a, "--") == 0) { endopt = true; continue; }
        if (!endopt && (!strcmp(a, "-h") || !strcmp(a, "--help"))) { usage(); return 0; }
        if (!endopt && (!strcmp(a, "-v") || !strcmp(a, "--version"))) { version(); return 0; }
        if (!endopt && !strcmp(a, "--stats")) { stats = true; continue; }
        if (!endopt && !strcmp(a, "-o")) { if (++i >= argc) die("-o requires an argument"); output = argv[i]; continue; }
        if (!endopt && !strcmp(a, "-w")) { ALLOW_WARNINGS = false; continue; }
        if (!endopt && !strcmp(a, "-C")) { emit_c = true; continue; }
        if (!endopt && !strcmp(a, "-c")) { compile_only = true; args_push(&backend, a); continue; }
        if (!endopt && (!strcmp(a, "-D") || !strcmp(a, "-I"))) { if (++i >= argc) die("-D/-I requires an argument"); args_push(&backend, a); args_push(&backend, argv[i]); continue; }
        if (!endopt && !strcmp(a, "--keep-c")) { keep = true; continue; }
        if (!endopt && !strcmp(a, "--index")) { index = true; continue; }
        if (!endopt && !strcmp(a, "--notify")) { notify = true; continue; }
        if (!endopt && a[0] != '-') { args_push(&inputs, a); continue; }
        args_push(&backend, a);
    }

    if (inputs.count == 0) die("no input .c+ file specified");

    // Output default deduction
    if (!output) {
        if (!compile_only) {
            output = "out"
#ifdef _WIN32
            ".exe"
#endif
            ;
        } else if (inputs.count == 1) {
            allocated_output = replace_extension(inputs.items[0], ".o");
            output = allocated_output;
        }
    }

    /* --- COMPILE EVERY INPUT FILE TO .c --- */
    for (size_t fidx = 0; fidx < inputs.count; fidx++) {
        const char *input_file = inputs.items[fidx];
        
        char *source = read_file(input_file);
        if (!source) { 
            fprintf(stderr, "c+: error: could not read '%s': %s\n", input_file, strerror(errno)); 
            status = 1; 
            goto cleanup; 
        }
        
        size_t s_bytes = strlen(source);
        size_t s_lines = 1;
        for (char *p = source; *p; p++) if (*p == '\n') s_lines++;
        
        source_bytes += s_bytes;
        source_lines += s_lines;

        double preprocess_start = timer_now();
        char *preprocessed = preprocess_source(input_file, source, 0);
        free(source);
        source = preprocessed;
        preprocess_time += timer_now() - preprocess_start;

        double lex_start = timer_now();
        Lexer lex;
        TokenList toks;
        lexer_init(&lex, source);
        tokens_init(&toks);
        for (;;) {
            Token x = lexer_next(&lex);
            token_count++;
            tokens_push(&toks, x);
            if (x.kind == TOK_EOF) break;
        }
        lex_time += timer_now() - lex_start;

        if (index) { 
            emit_ide_index(&toks); 
            tokens_free(&toks); 
            free(source); 
            continue; // Move to next file if indexing
        }

        double transpile_start = timer_now();
        Transpiler t;
        memset(&t, 0, sizeof(t));
        t.source = source;
        t.tokens = toks;
        symbols_init(&t.symbols);
        locals_init(&t.locals);
        buffer_init(&t.output);
        transpile(&t);
        transpile_time += timer_now() - transpile_start;

        size_t g_bytes = t.output.len;
        size_t g_lines = 1;
        for (size_t x = 0; x < g_bytes; x++) if (t.output.data[x] == '\n') g_lines++;
        generated_bytes += g_bytes;
        generated_lines += g_lines;

        char *generated_tmp = generated_name(input_file);
        args_push(&generated_files, generated_tmp); // Copies it into the array
        
        FILE *f = fopen(generated_tmp, "wb");
        if (!f) die("could not create generated C file");
        fwrite(t.output.data, 1, t.output.len, f);
        fclose(f);
        free(generated_tmp);

        if (emit_c) {
            printf("C+ -> C: %s\n", generated_files.items[generated_files.count - 1]);
        }

        buffer_free(&t.output);
        locals_free(&t.locals);
        symbols_free(&t.symbols);
        tokens_free(&toks);
        free(source);
    }

    if (index || status != 0) {
        goto cleanup;
    }

    if (emit_c) {
        if (stats) print_stats(source_lines, source_bytes, token_count, generated_lines, generated_bytes, preprocess_time, lex_time, transpile_time, 0, timer_now() - total_start);
        goto cleanup;
    }

    /* --- BACKEND BATCH EXECUTION --- */
    double backend_start = timer_now();
    args_init(&command);
    args_push(&command, "tcc");
    args_push(&command, "-w");
    
    // Add forwarded backend args (like -I, -D, -c)
    for (i = 0; i < (int)backend.count; i++) args_push(&command, backend.items[i]);
    
    // Target output executable/object if supplied or deduced
    if (output) {
        args_push(&command, "-o");
        args_push(&command, output);
    }
    
    // Add all generated C files
    for (i = 0; i < (int)generated_files.count; i++) {
        args_push(&command, generated_files.items[i]);
    }
    
    status = run_process(&command);
    backend_time = timer_now() - backend_start;

    if (stats) print_stats(source_lines, source_bytes, token_count, generated_lines, generated_bytes, preprocess_time, lex_time, transpile_time, backend_time, timer_now() - total_start);
    
    /* --- POST COMPILATION CLEANUP --- */
    for (i = 0; i < (int)generated_files.count; i++) {
        if (status == 0 && !keep) remove(generated_files.items[i]);
    }
    
    if (status != 0) { 
        fprintf(stderr, "c+: error: backend compilation failed (exit code %d)\n", status); 
        fprintf(stderr, "c+: generated C files were kept.\n"); 
    }
    
    if (!status && notify) {
        if (inputs.count == 1) {
            printf("[C+] compilation of '%s' successfully finished!\n", inputs.items[0]);
        } else {
            printf("[C+] compilation of %zu files successfully finished!\n", inputs.count);
        }
    }

    args_free(&command);

cleanup:
    if (allocated_output) free(allocated_output);
    args_free(&generated_files);
    args_free(&inputs);
    args_free(&backend);
    
    return status;
}