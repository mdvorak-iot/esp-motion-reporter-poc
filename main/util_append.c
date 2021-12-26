#include "util_append.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

char *util_append(char *dst, const char *end, const char *fmt, ...)
{
    if (!dst) return NULL;

    assert(end);
    assert(end >= dst);
    assert(fmt);

    va_list args;
    va_start(args, fmt);
    size_t n = (size_t)(end - dst);
    int count = vsnprintf(dst, n, fmt, args);
    va_end(args);

    if (count < 0)
    {
        return NULL;
    }
    if (count >= n)
    {
        return NULL;
    }

    return dst + count;
}
