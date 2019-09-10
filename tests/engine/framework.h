#include "crex.h"

// For brevity
typedef crex_match_t match_t;

typedef struct test_suite_builder test_suite_builder_t;

// Functions for creating test suite files

test_suite_builder_t *create_test_suite(const char *path);
void close_test_suite(test_suite_builder_t *suite);

void emit_pattern(test_suite_builder_t *suite,
                  const char *pattern,
                  size_t size,
                  size_t n_capturing_groups);

void emit_pattern_str(test_suite_builder_t *suite, const char *pattern, size_t n_capturing_groups);

typedef struct {
  const char *substr;
  size_t position;
} testcase_match_t;

#define UNMATCHED ((testcase_match_t){NULL, 0})
#define SUBSTR(substr) ((testcase_match_t){substr, SIZE_MAX})
#define SUBSTR_AT(substr, position) ((testcase_match_t){substr, position})

void emit_testcase(test_suite_builder_t *suite, const char *str, size_t size, ...);

void emit_testcase_str(test_suite_builder_t *suite, const char *str, ...);
