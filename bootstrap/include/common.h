#pragma once

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>

extern bool ALLOW_WARNINGS;

void die(const char *s);
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

typedef struct {
    char *data;
    size_t len, cap;
} Buffer;

void buffer_init(Buffer *b);
void buffer_reserve(Buffer *b, size_t n);
void buffer_putc(Buffer *b, char c);
void buffer_puts(Buffer *b, const char *s);
void buffer_free(Buffer *b);

char *read_file(const char *path);
char *path_join(const char *a, const char *b);
char *path_join3(const char *a, const char *b, const char *c);
bool file_exists(const char *path);
char *get_cwd(void);
char *dir_name(const char *path);
char *resolve_include_path(const char *from_file, const char *spec, bool angle);