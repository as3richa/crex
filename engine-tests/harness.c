#undef NDEBUG

#include <assert.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "execution-engine.h"
#include "harness.h"
#include "str-builder.h"
#include "suite.h"

static void *default_alloc_crex(void *data, size_t size) {
  (void)data;
  return malloc(size);
}

static void default_free_crex(void *data, void *pointer) {
  (void)data;
  free(pointer);
}

static void *measured_alloc_crex(void *data, size_t size) {
  bm_alloc_stats_t *allocator = data;
  allocator->n_allocations++;
  allocator->total_size += size;
  return malloc(size);
}

static void measured_free_crex(void *data, void *pointer) {
  (void)data;
  free(pointer);
}

static void *default_alloc_pcre(size_t size, void *data) {
  (void)data;
  return malloc(size);
}

static void default_free_pcre(void *pointer, void *data) {
  (void)data;
  free(pointer);
}

static void *measured_alloc_pcre(size_t size, void *data) {
  bm_alloc_stats_t *allocator = data;
  allocator->n_allocations++;
  allocator->total_size += size;
  return malloc(size);
}

static void measured_free_pcre(void *pointer, void *data) {
  (void)data;
  free(pointer);
}

void reset_allocator(allocator_t *allocator, ex_convention_t convention, int measure) {
  if (convention == CONVENTION_CREX) {
    allocator->u.crex.context = measure ? &allocator->stats : NULL;
    allocator->u.crex.alloc = measure ? measured_alloc_crex : default_alloc_crex;
    allocator->u.crex.free = measure ? measured_free_crex : default_free_crex;
    return;
  }

  assert(convention == CONVENTION_PCRE);

  allocator->u.pcre.data = measure ? &allocator->stats : NULL;
  allocator->u.pcre.alloc = measure ? measured_alloc_pcre : default_alloc_pcre;
  allocator->u.pcre.free = measure ? measured_free_pcre : default_free_pcre;
}

suite_t *load_suites(char **paths, size_t n_paths) {
  suite_t *suites = malloc(sizeof(suite_t) * n_paths);
  assert(suites != NULL);

  for (size_t i = 0; i < n_paths; i++) {
    const int fd = open(paths[i], O_RDONLY);
    assert(fd != -1);

    struct stat statbuf;
    const int status = fstat(fd, &statbuf);
    assert(status != -1);

    void *mapping = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(mapping != MAP_FAILED);

    close(fd);

    unpack_suite(&suites[i], mapping, statbuf.st_size);
  }

  return suites;
}

void destroy_suites(suite_t *suites, size_t n_suites) {
  for (size_t i = 0; i < n_suites; i++) {
    destroy_unpacked_suite(&suites[i]);
  }

  free(suites);
}

const execution_engine_t **load_engines(size_t *n_engines,
                                        const execution_engine_t *all_engines[],
                                        char **names,
                                        size_t n_names,
                                        const execution_engine_t *default_engine) {
  if (n_names == 0) {
    const execution_engine_t **engines = malloc(sizeof(execution_engine_t *));
    assert(engines != NULL);

    engines[0] = default_engine;

    *n_engines = 1;

    return engines;
  }

  if (n_names == 1 && strcmp(names[0], "all") == 0) {
    const size_t size = sizeof(execution_engine_t *) * (*n_engines);

    const execution_engine_t **engines = malloc(size);
    assert(engines != NULL);

    memcpy(engines, all_engines, size);

    return engines;
  }

  const execution_engine_t **engines = malloc(sizeof(execution_engine_t *) * n_names);
  assert(engines);

  for (size_t i = 0; i < n_names; i++) {
    engines[i] = NULL;

    for (size_t j = 0; j < *n_engines; j++) {
      if (strcmp(names[i], all_engines[j]->name) == 0) {
        engines[i] = all_engines[j];
        break;
      }
    }

    if (engines[i] == NULL) {
      free(engines);
      return NULL;
    }
  }

  *n_engines = n_names;

  return engines;
}

void destroy_engines(const execution_engine_t **engines) {
  free((void *)engines);
}

void start_timer(bench_timer_t *timer) {
  const int status = clock_gettime(CLOCK_MONOTONIC, timer);
  assert(status != -1);
}

double stop_timer(bench_timer_t *timer) {
  bench_timer_t now;

  const int status = clock_gettime(CLOCK_MONOTONIC, &now);
  assert(status != -1);

  return now.tv_sec - timer->tv_sec + (now.tv_nsec - timer->tv_nsec) / 1e9;
}

size_t parse_size(char *str) {
  size_t value = 0;

  while (*str != 0) {
    size_t digit = *str - '0';
    assert(digit <= 9 && value <= (SIZE_MAX - digit) / 10);
    value = 10 * value + digit;
  }

  return value;
}
