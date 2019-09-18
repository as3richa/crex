#ifndef BUILDER_H
#define BUILDER_H

typedef struct test_suite_builder test_suite_builder_t;

test_suite_builder_t *create_test_suite(const char *path);
void finalize_test_suite(test_suite_builder_t *suite);

void emit_pattern(test_suite_builder_t *suite,
                  const char *pattern,
                  size_t size,
                  size_t n_capturing_groups);

void emit_pattern_str(test_suite_builder_t *suite, const char *pattern, size_t n_capturing_groups);

typedef struct {
  size_t begin;
  size_t end;
  const char *substr;
} testcase_match_t;

#define UNMATCHED ((testcase_match_t){SIZE_MAX, SIZE_MAX, NULL})
#define SUBSTR(substr) ((testcase_match_t){SIZE_MAX, SIZE_MAX, substr})
#define SUBSTR_AT(substr, begin) ((testcase_match_t){begin, SIZE_MAX, substr})
#define SPAN(begin, end) ((testcase_match_t){begin, end, NULL})

void emit_testcase(test_suite_builder_t *suite, const char *str, size_t size, ...);

void emit_testcase_str(test_suite_builder_t *suite, const char *str, ...);

#endif
