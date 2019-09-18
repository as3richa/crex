#include <assert.h>

#include "test-harness.h"

static void *create_crex_context(const void *allocator) {
  context_t *context = crex_create_context_with_allocator(NULL, allocator);
  assert(context != NULL);

  return context;
}

static void destroy_crex_context(void *context) {
  crex_destroy_context(context);
}

static void *compile_crex_regex(void *context,
                                const char *pattern,
                                size_t size,
                                size_t n_capturing_groups,
                                const void *allocator) {
  (void)context;

  regex_t *regex = crex_compile_with_allocator(NULL, pattern, size, allocator);

  assert(regex != NULL);
  assert(crex_regex_n_capturing_groups(regex) == n_capturing_groups);

  return regex;
}

static void destroy_crex_regex(void *context, void *regex) {
  (void)context;

  crex_destroy_regex(regex);
}

static void
run_crex_test(match_t *result, void *context, const void *regex, const char *str, size_t size) {
  const status_t status = crex_match_groups(result, context, regex, str, size);
  assert(status == CREX_OK);
}

const test_harness_t crex_correctness_default = {BM_NONE,
                                                 0,
                                                 create_crex_context,
                                                 destroy_crex_context,
                                                 compile_crex_regex,
                                                 destroy_crex_regex,
                                                 run_crex_test};

const test_harness_t crex_bench_default = {BM_TIME_MEMORY_CREX,
                                           0,
                                           create_crex_context,
                                           destroy_crex_context,
                                           compile_crex_regex,
                                           destroy_crex_regex,
                                           run_crex_test};

const test_harness_t crex_bench_compile_default = {BM_TIME_MEMORY_CREX,
                                                   1,
                                                   create_crex_context,
                                                   destroy_crex_context,
                                                   compile_crex_regex,
                                                   destroy_crex_regex,
                                                   NULL};
