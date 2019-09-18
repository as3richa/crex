#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

typedef enum { TS_CMD_PATTERN, TS_CMD_STR } ts_cmd_type_t;

typedef struct {
  size_t size;
  size_t n_capturing_groups;

  char pattern[1];
} ts_pattern_t;

typedef struct {
  size_t size;
  char str[1];
} ts_str_t;

typedef struct {
  ts_cmd_type_t type;

  union {
    ts_pattern_t pattern;
    ts_str_t str;
  } u;
} ts_cmd_t;

#define ALIGN(offset) (((offset) + sizeof(size_t) - 1) / sizeof(size_t) * sizeof(size_t))

#define TS_PATTERN_SIZE(size)                                                                      \
  (offsetof(ts_cmd_t, u) + offsetof(ts_pattern_t, pattern) + ALIGN(size))

#define TS_STR_SIZE(size) (offsetof(ts_cmd_t, u) + offsetof(ts_str_t, str) + ALIGN(size))

#endif
