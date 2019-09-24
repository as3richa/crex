#undef NDEBUG
#define PCRE2_CODE_UNIT_WIDTH 8

#include <assert.h>
#include <pcre2.h>
#include <stdint.h>

#include "../execution-engine.h"

typedef struct {
  pcre2_compile_context *compile;
  pcre2_match_context *match;
} contexts_t;

static void *create(void *void_allocator) {
  const pcre_allocator_t *allocator = void_allocator;

  contexts_t *contexts = malloc(sizeof(contexts_t));
  assert(contexts != NULL);

  pcre2_general_context *general =
      pcre2_general_context_create(allocator->alloc, allocator->free, allocator->data);

  assert(general != NULL);

  contexts->compile = pcre2_compile_context_create(general);
  assert(contexts->compile != NULL);

  contexts->match = pcre2_match_context_create(general);
  assert(contexts->match != NULL);

  pcre2_general_context_free(general);

  return contexts;
}

static void destroy(void *void_contexts, void *allocator) {
  (void)allocator;

  contexts_t *contexts = void_contexts;

  pcre2_compile_context_free(contexts->compile);
  pcre2_match_context_free(contexts->match);

  free(contexts);
}

static void *compile_regex(void *contexts,
                           const char *pattern,
                           size_t size,
                           size_t exp_n_capturing_groups,
                           void *allocator) {
  (void)allocator;

  pcre2_compile_context *context = ((contexts_t *)contexts)->compile;

  int error;
  PCRE2_SIZE erroroffset;

  pcre2_code *regex =
      pcre2_compile((const unsigned char *)pattern, size, 0, &error, &erroroffset, context);

  assert(regex != NULL);

  const int status = pcre2_jit_compile(regex, PCRE2_JIT_COMPLETE);
  assert(status >= 0);

  uint32_t n_capturing_groups;
  pcre2_pattern_info(regex, PCRE2_INFO_CAPTURECOUNT, &n_capturing_groups);
  n_capturing_groups++;

  assert(n_capturing_groups == exp_n_capturing_groups);

  return regex;
}

static void destroy_regex(void *contexts, void *regex, void *allocator) {
  (void)contexts;
  (void)allocator;

  pcre2_code_free(regex);
}

static int
run(void *contexts, void *matches, void *regex, const char *str, size_t size, void *allocator) {
  (void)allocator;

  pcre2_match_context *context = ((contexts_t *)contexts)->match;

  const int status =
      pcre2_jit_match(regex, (const unsigned char *)str, size, 0, 0, matches, context);

  assert(status >= 0 || status == PCRE2_ERROR_NOMATCH);

  return 1;
}

const execution_engine_t ex_pcre_jit = {
    "pcre-jit", 1, 1, create, destroy, compile_regex, destroy_regex, run};
