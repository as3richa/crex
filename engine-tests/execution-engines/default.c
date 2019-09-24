#undef NDEBUG

#include <assert.h>

#include "../execution-engine.h"

static void *create(void *allocator) {
  crex_context_t *context = crex_create_context_with_allocator(NULL, allocator);
  assert(context != NULL);

  return context;
}

static void destroy(void *context, void *allocator) {
  (void)allocator;

  crex_destroy_context(context);
}

static void *compile_regex(
    void *context, const char *pattern, size_t size, size_t n_capturing_groups, void *allocator) {
  (void)context;

  crex_regex_t *regex = crex_compile_with_allocator(NULL, pattern, size, allocator);
  assert(regex != NULL);

  assert(crex_regex_n_capturing_groups(regex) == n_capturing_groups);

  return regex;
}

static void destroy_regex(void *context, void *regex, void *allocator) {
  (void)allocator;
  (void)context;

  crex_destroy_regex(regex);
}

static int
run(void *context, void *matches, void *regex, const char *str, size_t size, void *allocator) {
  (void)allocator;

  const crex_status_t status = crex_match_groups(matches, context, regex, str, size);
  assert(status == CREX_OK);

  return 1;
}

const execution_engine_t ex_default = {
    "default", 0, 1, create, destroy, compile_regex, destroy_regex, run};
