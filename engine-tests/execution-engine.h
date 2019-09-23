#ifndef EXECUTION_ENGINE_H
#define EXECUTION_ENGINE_H

#include "crex.h"
#include "harness.h"

typedef struct {
  const char *name;

  allocator_type_t allocator_type;

  void *(*create)(void *allocator);
  void (*destroy)(void *self, void *allocator);

  void *(*compile_regex)(
      void *self, const char *pattern, size_t size, size_t n_capturing_groups, void *allocator);

  void (*destroy_regex)(void *self, void *regex, void *allocator);

  int (*run)(void *self,
             crex_match_t *matches,
             void *regex,
             const char *str,
             size_t size,
             void *allocator);
} execution_engine_t;

#define CREX_ALLOC(allocator, size)                                                                \
  ((crex_allocator_t *)allocator)->alloc(((crex_allocator_t *)allocator)->data, (size))

#define CREX_FREE(allocator, pointer)                                                              \
  ((crex_allocator_t *)allocator)->free(((crex_allocator_t *)allocator)->data, (pointer))

#define PCRE_ALLOC(allocator, size)                                                                \
  ((pcre_allocator_t *)allocator)->alloc((size), ((pcre_allocator_t *)allocator)->data)

#define PCRE_FREE(allocator, pointer)                                                              \
  ((pcre_allocator_t *)allocator)->free((pointer), ((pcre_allocator_t *)allocator)->data)

#endif
