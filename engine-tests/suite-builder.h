#ifndef BUILDER_H
#define BUILDER_H

#include <stddef.h>
#include <stdint.h>

#include "str-builder.h"

typedef struct suite_builder suite_builder_t;

suite_builder_t *create_test_suite(const char *path);
suite_builder_t *create_test_suite_argv(int argc, char **argv);

void finalize_test_suite(suite_builder_t *suite);

void emit_pattern(suite_builder_t *suite,
                  const char *pattern,
                  size_t size,
                  size_t n_capturing_groups);

void emit_pattern_str(suite_builder_t *suite, const char *pattern, size_t n_capturing_groups);

void emit_pattern_sb(suite_builder_t *suite,
                     const str_builder_t *pattern,
                     size_t n_capturing_groups);

typedef struct {
  size_t begin;
  size_t end;
  const char *substr;
} match_expectation_t;

#define UNMATCHED ((match_expectation_t){SIZE_MAX, SIZE_MAX, NULL})
#define SUBSTR(substr) ((match_expectation_t){SIZE_MAX, SIZE_MAX, substr})
#define SUBSTR_AT(substr, begin) ((match_expectation_t){begin, SIZE_MAX, substr})
#define SPAN(begin, end) ((match_expectation_t){begin, end, NULL})

void emit_testcase(suite_builder_t *suite, const char *str, size_t size, ...);

void emit_testcase_str(suite_builder_t *suite, const char *str, ...);

void emit_testcase_sb(suite_builder_t *suite, const str_builder_t *str, ...);

#endif
