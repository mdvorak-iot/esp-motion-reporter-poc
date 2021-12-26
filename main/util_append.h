#pragma once

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((format(printf, 3, 4))) char *util_append(char *dst, const char *end, const char *fmt, ...);

#ifdef __cplusplus
}
#endif
