#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crex.h"

// This program tests allocation hygiene and error handling.
//
// For any particular sequence of operations (compilations, searches, etc.), the library will make
// some number of memory allocations; moreover, this number is deterministic if we start from a
// fresh context. Using the allocator API, we can construct an allocator that succeeds for the first
// k allocations, and fails all allocations thereafter. If we run a sequence of operations using
// this allocator with k set to (e.g.) zero, the very first allocation done by the library will
// fail, some error handling branch will execute, and the library call will return CREX_E_NOMEM. If
// we set k to one, the first allocation will succeed, but the second allocation will fail, causing
// some other error handling branch to execute.
//
// In principle, if we repeatedly run the same sequence of operations against this allocator,
// looping over k starting from zero until the entire sequence of operations succeeds, we can
// exercise every allocation-related error handling branch that the sequence might ever encounter in
// production. This allows us to assert that each library function correctly returns CREX_E_NOMEM on
// allocation failure. Moreover, by adding some simple instrumentation to our custom allocator, we
// can assert that each error handling branch correctly frees any intermediate heap-allocated
// objects. Finally, we can add some additional bookkeeping to the custom allocator in order to
// detect invalid and double frees.
//
// These tests are extremely valuable and would be impossible to build without a custom allocator.

const char *argv0;

#define DIE(message)                                                                               \
  do {                                                                                             \
    fprintf(stderr, "\x1b[31m%s: %s:%d: %s\x1b[0m\n", argv0, __FILE__, __LINE__, message);         \
    abort();                                                                                       \
  } while (0)

#define MAX_CAPTURING_GROUPS 128

#define MAX_ALLOCATIONS 1024

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
    DIE("malloc unexpectedly returned NULL");
  }

  return allocator->pointers[index];
}

void metered_free(void *context, void *pointer) {
  if (pointer == NULL) {
    return;
  }

  metered_allocator_t *allocator = context;

  allocator->frees++;

  for (size_t index = 0; index < allocator->quota; index++) {
    if (allocator->pointers[index] == pointer) {
      allocator->pointers[index] = NULL;
      free(pointer);
      return;
    }
  }

  DIE("free called with an un-alloced pointer");
}

#define N_PATTERNS 5

static const char *patterns[N_PATTERNS] = {
    "a*",
    "a{13,37}",
    "(?:alpha)??",
    "([1-9][0-9]*)(?:\\.([0-9]+))?(?:[eE](-?[1-9][0-9]*))?",
    "\\A(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\z"};

#define N_STRINGS 10

static const char *strings[N_STRINGS] = {
    "",
    "aaa",
    "aaaaaaaaaaaaaaaaaaaaaaaaaa",
    "alpha",
    "1.3e37",
    "0.00000000000001",
    "192.168.1.254",
    "the quick brown fox jumps over the lazy dogs",
    "f7f376a1fcd0d0e11a10ed1b6577c99784d3a6bbe669b1d13fae43eb64634f6e",
    "*\x13\xc1\x97\x1e\n|\xde@\xd6\xe4\xa4u\xda|\xc5kA-\x1a\xb5\x0e\x0c\xdd\xd8u\xf0x)G\xedW'A"};

int main(int argc, char **argv) {
  (void)argc;
  argv0 = argv[0];

  metered_allocator_t allocator;
  const crex_allocator_t crex_allocator = {&allocator, metered_alloc, metered_free};

  for (size_t i = 0; i < MAX_ALLOCATIONS; i++) {
    allocator.pointers[i] = NULL;
  }

  for (size_t i = 0; i < N_PATTERNS; i++) {
    const char *pattern = patterns[i];
    const size_t size = strlen(pattern);

    for (allocator.quota = 0; allocator.quota <= MAX_ALLOCATIONS; allocator.quota++) {
      allocator.allocs = 0;
      allocator.frees = 0;

      crex_status_t status;
      crex_regex_t *regex = crex_compile_with_allocator(&status, pattern, size, &crex_allocator);

      if (regex == NULL) {
        if (status == CREX_E_NOMEM) {
          if (allocator.allocs != allocator.frees) {
            DIE("mismatched allocs/frees");
          }

          continue;
        }

        DIE("unexpected status");
      }

      crex_context_t *context = crex_create_context_with_allocator(&status, &crex_allocator);

      if (context == NULL) {
        if (status == CREX_E_NOMEM) {
          crex_destroy_regex(regex);

          if (allocator.allocs != allocator.frees) {
            DIE("mismatched allocs/frees");
          }

          continue;
        }

        DIE("unexpected status");
      }

      for (size_t j = 0; j < N_STRINGS; j++) {
        const char *str = strings[j];

        int is_match;
        status = crex_is_match_str(&is_match, context, regex, str);

        if (status == CREX_E_NOMEM) {
          break;
        }

        if (status != CREX_OK) {
          DIE("unexpected status");
        }

        crex_match_t match;
        status = crex_find_str(&match, context, regex, str);

        if (status == CREX_E_NOMEM) {
          break;
        }

        if (status != CREX_OK) {
          DIE("unexpected status");
        }

        crex_match_t matches[MAX_CAPTURING_GROUPS];
        status = crex_match_groups_str(matches, context, regex, str);

        if (status == CREX_E_NOMEM) {
          break;
        }

        if (status != CREX_OK) {
          DIE("unexpected status");
        }
      }

      crex_destroy_context(context);
      crex_destroy_regex(regex);

      if (allocator.allocs != allocator.frees) {
        DIE("mismatched allocs/frees");
      }

      if (status == CREX_OK) {
        break;
      }
    }

    if (allocator.quota > MAX_ALLOCATIONS) {
      DIE("MAX_ALLOCATIONS too low");
    }
  }

  printf("\x1b[32m%s: %s:%d: passed\x1b[0m\n", argv0, __FILE__, __LINE__);

  return 0;
}
