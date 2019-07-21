#include <stddef.h>

#define MAX_GROUPS 100

typedef struct {
  const char *str;
  size_t size;
} pattern_defn_t;

typedef struct {
  size_t pattern_index;

  const char *str;
  size_t size;

  int is_match;

  struct {
    size_t begin;
    size_t end;
  } groups[MAX_GROUPS];
} testcase_t;
