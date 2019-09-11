#include "crex.h"

#if (defined(__GNUC__) || defined(__clang__))
#define NO_RETURN __attribute__((noreturn))
#else
#define NO_RETURN
#endif

NO_RETURN void die(const char *format, ...);

#define ASSERT(condition)                                                                          \
  (void)(condition || (die("%s:%u: assertion failed: %s", __FILE__, __LINE__, #condition), 1))

// For brevity
typedef crex_status_t status_t;
typedef crex_context_t context_t;
typedef crex_regex_t regex_t;
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

typedef enum { TM_UNMATCHED, TM_SUBSTRING, TM_SUBSTRING_AT, TM_SPAN } testcase_match_type_t;

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

// Test executor

typedef struct {
  void *(*create)(char **, size_t);
  void *(*compile_regex)(void *, const char *, size_t, size_t);
  void (*destroy_regex)(void *, void *);
  int (*run_test)(void *, const void *, const char *, size_t, match_t *expectation);
  void (*destroy)(void *);
} test_executor_t;

int run_tests(int argc, char **argv, test_executor_t *tx);
