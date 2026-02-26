#ifndef WALU_STRING_H
#define WALU_STRING_H

#include <stddef.h>

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int value, size_t n);

#endif
