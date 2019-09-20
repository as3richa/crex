#undef NDEBUG

// FIXME: need this for clock_gettime; this doesn't work on OSX
#define _GNU_SOURCE

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "test-harness.h"

static size_t parse_size(char *str) {
  size_t value = 0;

  while (*str != 0) {
    size_t digit = *str - '0';
    assert(digit <= 9 && value <= (SIZE_MAX - digit) / 10);
    value = 10 * value + digit;
  }

  return value;
}

static size_t literallify(char *result, const char *str, size_t size) {
  char *begin = result;

  *(result++) = '"';

  for (size_t i = 0; i < size; i++) {
    switch (str[i]) {
    case '\\': {
      *(result++) = '\\';
      *(result++) = '\\';
      break;
    }

    case '\n': {
      *(result++) = '\\';
      *(result++) = '\n';
      break;
    }

    default: {
      if (isprint(str[i])) {
        *(result++) = str[i];
      } else {
        result +=
            sprintf(result, "\\\\x%x%x", (unsigned char)str[i] / 16, (unsigned char)str[i] % 16);
      }
    }
    }
  }

  *(result++) = '"';
  *(result++) = 0;

  return result - begin;
}

static double delta(struct timespec *finish, struct timespec *start) {
  return finish->tv_sec - start->tv_sec + (finish->tv_nsec - start->tv_nsec) / 1e9;
}

typedef struct {
  size_t n_allocations;
  size_t total_size;
} metered_allocator_data_t;

static void *metered_alloc_crex(void *data, size_t size) {
  metered_allocator_data_t *allocator = data;
  allocator->n_allocations++;
  allocator->total_size += size;
  return malloc(size);
}

static void metered_free_crex(void *data, void *pointer) {
  (void)data;
  free(pointer);
}

static void *metered_alloc_pcre(size_t size, void *data) {
  metered_allocator_data_t *allocator = data;
  allocator->n_allocations++;
  allocator->total_size += size;
  return malloc(size);
}

static void metered_free_pcre(void *pointer, void *data) {
  (void)data;
  free(pointer);
}

int run(int argc, char **argv, const test_harness_t *harness) {
  char **paths = argv + 1;
  size_t n_suites = 0;

  struct {
    int verbose;
    size_t compile_iterations;
    size_t testcase_iterations;
  } options;

  options.verbose = 0;

  options.compile_iterations =
      (harness->benchmark_type != BM_NONE && harness->compile_only) ? 1000 : 1;

  options.testcase_iterations =
      (harness->benchmark_type == BM_NONE) ? !harness->compile_only : 1000;

  for (int i = 0; i < argc - 1; i++) {
    if (strcmp(paths[i], "-v") == 0) {
      options.verbose = 1;
      continue;
    }

    if (strcmp(paths[i], "--compile-iterations") == 0 || strcmp(paths[i], "-c") == 0) {
      assert(i < argc - 2);
      options.compile_iterations = parse_size(paths[++i]);
      continue;
    }

    if (strcmp(paths[i], "--testcase-iterations") == 0 || strcmp(paths[i], "-t") == 0) {
      assert(i < argc - 2);
      options.testcase_iterations = parse_size(paths[++i]);
      continue;
    }

    paths[n_suites++] = paths[i];
  }

  // Eagerly mmap all the suites, in order to fail fast in the event of a bad path

  struct {
    size_t size;
    char *mapping;
  } * suites;

  suites = malloc(sizeof(*suites) * n_suites);
  assert(suites != NULL);

  for (size_t i = 0; i < n_suites; i++) {
    const int fd = open(paths[i], O_RDONLY);
    assert(fd != -1);

    struct stat statbuf;
    const int status = fstat(fd, &statbuf);
    assert(status == 0);

    suites[i].size = statbuf.st_size;

    suites[i].mapping = mmap(NULL, suites[i].size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    assert(suites[i].mapping != MAP_FAILED);

    close(fd);
  }

  union {
    allocator_t crex;
    pcre_allocator_t pcre;
  } allocator_struct;

  metered_allocator_data_t allocator_data;

  void *allocator;

  switch (harness->benchmark_type) {
  case BM_TIME_MEMORY_CREX: {
    allocator_struct.crex.context = &allocator_data;
    allocator_struct.crex.alloc = metered_alloc_crex;
    allocator_struct.crex.free = metered_free_crex;
    allocator = &allocator_struct;
    break;
  }

  case BM_TIME_MEMORY_PCRE: {
    allocator_struct.pcre.data = &allocator_data;
    allocator_struct.pcre.alloc = metered_alloc_pcre;
    allocator_struct.pcre.free = metered_free_pcre;
    break;
  }

  default:
    allocator = NULL;
  }

  struct {
    ts_pattern_t *pattern;
    ts_str_t *str;
    match_t *expectation;
    match_t *matches;
  } failure = {NULL};

  void *harness_data = harness->create(allocator);

  match_t *matches = NULL;
  size_t max_groups = 0;

  size_t n_tests = 0;
  size_t n_passed = 0;

  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);

  for (size_t i = 0; i < n_suites; i++) {
    ts_pattern_t *pattern = NULL;

    void *regex = NULL;
    size_t n_capturing_groups = SIZE_MAX;

    for (char *cursor = suites[i].mapping; cursor < suites[i].mapping + suites[i].size;) {
      ts_cmd_t *cmd = (ts_cmd_t *)cursor;

      if (cmd->type == TS_CMD_PATTERN) {
        pattern = &cmd->u.pattern;
        cursor += TS_PATTERN_SIZE(pattern->size);

        for (size_t j = 0; j < options.compile_iterations; j++) {
          harness->destroy_regex(harness_data, regex);

          regex = harness->compile_regex(harness_data,
                                         pattern->pattern,
                                         pattern->size,
                                         pattern->n_capturing_groups,
                                         allocator);
        }

        // Cache this for better locality
        n_capturing_groups = pattern->n_capturing_groups;

        continue;
      }

      assert(cmd->type == TS_CMD_STR);
      assert(pattern != NULL && n_capturing_groups != SIZE_MAX);

      if (options.testcase_iterations == 0) {
        continue;
      }

      ts_str_t *str = &cmd->u.str;
      cursor += TS_STR_SIZE(str->size);

      if (max_groups < n_capturing_groups) {
        max_groups = 2 * n_capturing_groups;
        free(matches);
        matches = malloc(sizeof(match_t) * max_groups);
        assert(matches != NULL);
      }

      match_t *expectation = (match_t *)cursor;
      cursor += sizeof(match_t) * n_capturing_groups;

      if (harness->benchmark_type == BM_NONE) {
        for (size_t j = 0; j < n_capturing_groups; j++) {
          if (expectation[j].begin == NULL) {
            assert(expectation[j].end == NULL);
            continue;
          }

          expectation[j].begin = str->str + (expectation[j].begin - ((char *)NULL + 1));
          expectation[j].end = str->str + (expectation[j].end - ((char *)NULL + 1));
        }
      }

      for (size_t j = 0; j < options.testcase_iterations; j++) {
        harness->run_test(matches, harness_data, regex, str->str, str->size);

        n_tests++;

        if (harness->benchmark_type == BM_NONE) {
          const int ok = memcmp(matches, expectation, sizeof(match_t) * n_capturing_groups) == 0;
          n_passed += ok;

          if (!ok && failure.pattern == NULL && pattern->size <= 50 && str->size <= 50) {
            failure.pattern = pattern;
            failure.str = str;
            failure.expectation = expectation;

            failure.matches = malloc(sizeof(match_t) * n_capturing_groups);
            assert(failure.matches != NULL);

            memcpy(failure.matches, matches, sizeof(match_t) * n_capturing_groups);
          }
        }
      }
    }

    harness->destroy_regex(harness_data, regex);
  }

  struct timespec finish;
  clock_gettime(CLOCK_MONOTONIC, &finish);

  const double total_time = delta(&finish, &start);

  int ok = 1;

  if (harness->benchmark_type != BM_NONE) {
    fprintf(stderr, "\x1b[32mran %zu test(s) in %0.4fs\x1b[0m\n", n_tests, total_time);
  } else if (n_passed == n_tests && n_tests > 0) {
    fprintf(
        stderr, "\x1b[32m%zu/%zu test(s) passed in %0.4fs\x1b[0m\n", n_passed, n_tests, total_time);
  } else {
    ok = 0;

    fprintf(
        stderr, "\x1b[33m%zu/%zu test(s) passed in %0.4fs\x1b[0m\n", n_passed, n_tests, total_time);

    if (failure.pattern != NULL) {
      char buffer[1000];

      literallify(buffer, failure.pattern->pattern, failure.pattern->size);

      fprintf(stderr, "\nfailing case:\n  pattern: %s\n", buffer);

      literallify(buffer, failure.str->str, failure.str->size);
      fprintf(stderr, "  str: %s\n", buffer);

      for (size_t k = 0; k <= 1; k++) {
        fprintf(stderr, "  %s:\n", (k == 0) ? "matches" : "expectation");

        // FIXME: don't shadow, it's evil
        match_t *matches = (k == 0) ? failure.matches : failure.expectation;

        for (size_t i = 0; i < failure.pattern->n_capturing_groups; i++) {
          const int ok = failure.matches[i].begin == failure.expectation[i].begin &&
                         failure.matches[i].end == failure.expectation[i].end;

          const char *color_code = ok ? "\x1b[32m" : "\x1b[31m";

          fprintf(stderr, "    %s%zu => ", color_code, i);

          if (matches[i].begin != NULL) {
            literallify(buffer, matches[i].begin, matches[i].end - matches[i].begin);

            fprintf(stderr,
                    "%s (@ %zu)\x1b[0m\n",
                    buffer,
                    (size_t)(matches[i].begin - failure.str->str));
          } else {
            fputs("<unmatched>\x1b[0m\n", stderr);
          }
        }
      }

      free(failure.matches);
    }
  }

  free(matches);

  return ok;
}
