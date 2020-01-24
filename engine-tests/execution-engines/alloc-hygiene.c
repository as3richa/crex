// #undef NDEBUG
#define NDEBUG

#include <assert.h>
#include <stdlib.h>

#include "../execution-engine.h"

typedef struct {
  size_t allocs;
  size_t frees;
  size_t quota;
} alloc_stats_t;

static void *stats_alloc(void *data, size_t size) {
  alloc_stats_t *stats = data;

  if (size == 0 || stats->allocs >= stats->quota) {
    return NULL;
  }

  stats->allocs++;

  return malloc(size);
}

static void stats_free(void *stats, void *pointer) {
  if (pointer == NULL) {
    return;
  }

  ((alloc_stats_t *)stats)->frees++;
  free(pointer);
}

static void *compile_regex(
    void *self, const char *pattern, size_t size, size_t n_capturing_groups, void *allocator) {
  (void)self;
  (void)allocator;

  alloc_stats_t stats;
  const crex_allocator_t stats_allocator = {&stats, stats_alloc, stats_free};

  crex_regex_t *regex;

  for (stats.quota = 0;; stats.quota++) {
    stats.allocs = 0;
    stats.frees = 0;

    crex_status_t status;
    regex = crex_compile_with_allocator(&status, pattern, size, &stats_allocator);

    assert(regex != NULL || status == CREX_E_NOMEM);

    if (regex != NULL) {
      break;
    }

    assert(stats.allocs == stats.frees);
  }

  crex_destroy_regex(regex);

  assert(stats.allocs == stats.frees);

  regex = crex_compile(NULL, pattern, size);
  assert(regex != NULL);

  assert(crex_regex_n_capturing_groups(regex) == n_capturing_groups);

  return regex;
}

static void destroy_regex(void *self, void *regex, void *allocator) {
  (void)self;
  (void)allocator;

  crex_destroy_regex(regex);
}

static int
run(void *self, void *matches, void *regex, const char *str, size_t size, void *allocator) {
  (void)self;
  (void)allocator;

  alloc_stats_t stats;
  const crex_allocator_t stats_allocator = {&stats, stats_alloc, stats_free};

  for (stats.quota = 0;; stats.quota++) {
    stats.allocs = 0;
    stats.frees = 0;

    crex_status_t status;
    crex_context_t *context = crex_create_context_with_allocator(&status, &stats_allocator);

    assert(context != NULL || status == CREX_E_NOMEM);

    if (context == NULL) {
      if (stats.allocs != stats.frees) {
        return 0;
      }

      continue;
    }

    status = crex_match_groups(matches, context, regex, str, size);
    assert(status == CREX_OK || status == CREX_E_NOMEM);

    crex_destroy_context(context);

    if (stats.allocs != stats.frees) {
      return 0;
    }

    if (status == CREX_E_NOMEM) {
      continue;
    }

    return 1;
  }
}

const execution_engine_t ex_alloc_hygiene = {
    "alloc-hygiene", 0, 0, NULL, NULL, compile_regex, destroy_regex, run};
