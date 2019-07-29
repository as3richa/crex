#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crex.h"

#define MAX_ALLOCATIONS 8192

typedef struct {
  size_t allocs;
  size_t frees;
  size_t quota;

  void *pointers[MAX_ALLOCATIONS];
} metered_allocator_t;

void *metered_alloc(void *context, size_t size) {
  metered_allocator_t *allocator = context;

  if (allocator->allocs == allocator->quota) {
    return NULL;
  }

  allocator->allocs++;

  size_t index;

  for (index = 0; index < allocator->quota; index++) {
    if (allocator->pointers[index] == NULL) {
      break;
    }
  }

  allocator->pointers[index] = malloc(size);

  if (allocator->pointers[index] == NULL) {
    fputs("malloc unexpectedly returned NULL (too little memory?)\n", stderr);
    exit(1);
  }

  return allocator->pointers[index];
}

void metered_free(void *context, void *pointer) {
  if (pointer == NULL) {
    return;
  }

  metered_allocator_t *allocator = context;

  allocator->frees++;

  free(pointer);

  for (size_t index = 0; index < allocator->quota; index++) {
    if (allocator->pointers[index] == pointer) {
      allocator->pointers[index] = NULL;
      return;
    }
  }

  fputs("free called with a pointer that was never alloced\n", stderr);
  exit(1);
}

#define N_PATTERNS 1

static const char *patterns[N_PATTERNS] = {
    "\\A(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\z"};

int main(void) {
  metered_allocator_t allocator;
  const crex_allocator_t crex_allocator = {&allocator, metered_alloc, metered_free};

  for (size_t i = 0; i < MAX_ALLOCATIONS; i++) {
    allocator.pointers[i] = NULL;
  }

  for (size_t i = 0; i < N_PATTERNS; i++) {
    const char *pattern = patterns[i];
    const size_t size = strlen(pattern);

    allocator.allocs = 0;
    allocator.frees = 0;
    allocator.quota = MAX_ALLOCATIONS;

    crex_status_t status;
    crex_regex_t *regex = crex_compile_with_allocator(&status, pattern, size, &crex_allocator);

    if (regex == NULL) {
      if (status == CREX_E_NOMEM) {
        fprintf(stderr, "/%s/: MAX_ALLOCATIONS too low\n", pattern);
      } else {
        fprintf(stderr,
                "/%s/: unexpected status from crex_compile_with_allocator: %d\n",
                pattern,
                status);
      }

      return 1;
    }

    crex_destroy_regex(regex);

    if (allocator.allocs != allocator.frees) {
      fprintf(stderr,
              "/%s/: successful compilation with quota %zu, %zu alloc(s) but only %zu free(s)\n",
              pattern,
              allocator.quota,
              allocator.allocs,
              allocator.frees);

      return 1;
    }

    const size_t allocs_required = allocator.allocs;

    printf("/%s/: compiled with %zu alloc(s)\n", pattern, allocs_required);

    for (allocator.quota = 0; allocator.quota < allocs_required; allocator.quota++) {
      allocator.allocs = 0;
      allocator.frees = 0;

      regex = crex_compile_with_allocator(&status, pattern, size, &crex_allocator);

      if (regex != NULL) {
        fprintf(stderr,
                "/%s/: unexpectedly successful compilation with quota %zu\n",
                pattern,
                allocator.quota);
        return 1;
      }

      if (status != CREX_E_NOMEM) {
        fprintf(stderr,
                "/%s/: unexpected status from crex_compile_with_allocator: %d\n",
                pattern,
                status);
        return 1;
      }

      if (allocator.allocs != allocator.frees) {
        fprintf(
            stderr,
            "/%s/: unsuccessful compilation with quota %zu, %zu alloc(s) but only %zu free(s)\n",
            pattern,
            allocator.quota,
            allocator.allocs,
            allocator.frees);

        return 1;
      }
    }
  }

  // FIXME: test things other than compilation

  return 0;
}
