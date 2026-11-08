#pragma once
#include <stddef.h> // for size_t
#include <sys/mman.h>

void *simple_malloc(size_t);
void simple_free(void *);
void *simple_realloc(void *, size_t);
