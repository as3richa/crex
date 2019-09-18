#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "framework.h"

static const char *argv0 = NULL;

NO_RETURN void die(const char *format, ...) {
  fputs("\x1b[31m", stderr);

  if (argv0 != NULL) {
    fprintf(stderr, "%s: ", argv0);
  }

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  fputs("\x1b[0m\n", stderr);

  abort();
}

// Functions for creating test suite files

typedef enum { TS_CMD_PATTERN, TS_CMD_STR } ts_cmd_type_t;

typedef struct {
  size_t size;
  size_t n_capturing_groups;

  char pattern[1];
} ts_pattern_t;

typedef struct {
  size_t size;
  char str[1];
} ts_str_t;

typedef struct {
  ts_cmd_type_t type;

  union {
    ts_pattern_t pattern;
    ts_str_t str;
  } u;
} ts_cmd_t;

#define ALIGN(offset) (((offset) + sizeof(size_t) - 1) / sizeof(size_t) * sizeof(size_t))

#define TS_PATTERN_SIZE(size)                                                                      \
  (offsetof(ts_cmd_t, u) + offsetof(ts_pattern_t, pattern) + ALIGN(size))

#define TS_STR_SIZE(size) (offsetof(ts_cmd_t, u) + offsetof(ts_str_t, str) + ALIGN(size))

struct test_suite_builder {
  const char *path;
  int fd;

  size_t size;

  size_t page_size;
  size_t capacity;

  void *mapping;

  size_t n_capturing_groups;
};

#define CHECK_ERROR(condition, suite)                                                              \
  (condition || ((delete_test_suite(suite), 1) && (ASSERT(condition), 1)))

static void delete_test_suite(test_suite_builder_t *suite) {
  unlink(suite->path);
}

void *append_to_suite(test_suite_builder_t *suite, size_t size) {
  if (suite->size + size <= suite->capacity) {
    void *result = suite->mapping + suite->size;
    suite->size += size;

    return result;
  }

  int status = msync(suite->mapping, suite->capacity, MS_SYNC);
  CHECK_ERROR(status != -1, suite);

  munmap(suite->mapping, suite->capacity);

  suite->capacity = 2 * suite->capacity + suite->page_size;
  ASSERT(suite->size + size <= suite->capacity);

  status = ftruncate(suite->fd, suite->capacity);
  CHECK_ERROR(status != -1, suite);

  void *mapping = mmap(NULL, suite->capacity, PROT_WRITE, MAP_SHARED, suite->fd, 0);
  CHECK_ERROR(mapping != MAP_FAILED, suite);

  suite->mapping = mapping;

  void *result = suite->mapping + suite->size;
  suite->size += size;

  return result;
}

test_suite_builder_t *create_test_suite(const char *path) {
  test_suite_builder_t *suite = malloc(sizeof(test_suite_builder_t));
  ASSERT(suite != NULL);

  // User/group read-write permissions
  suite->fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0x1b0u);
  ASSERT(suite->fd != -1);

  suite->size = 0;

  suite->page_size = sysconf(_SC_PAGESIZE);
  CHECK_ERROR(suite->page_size > 0, suite);

  suite->capacity = 0;

  suite->n_capturing_groups = SIZE_MAX;

  return suite;
}

void finalize_test_suite(test_suite_builder_t *suite) {
  ftruncate(suite->fd, suite->size);
  close(suite->fd);
  munmap(suite->mapping, suite->capacity);
  free(suite);
}

void emit_pattern(test_suite_builder_t *suite,
                  const char *pattern,
                  size_t size,
                  size_t n_capturing_groups) {
  ts_cmd_t *cmd = append_to_suite(suite, TS_PATTERN_SIZE(size));
  ts_pattern_t *ts_pattern = &cmd->u.pattern;

  cmd->type = TS_CMD_PATTERN;
  ts_pattern->size = size;
  ts_pattern->n_capturing_groups = n_capturing_groups;
  memcpy(ts_pattern->pattern, pattern, size);

  suite->n_capturing_groups = n_capturing_groups;
}

void emit_pattern_str(test_suite_builder_t *suite, const char *pattern, size_t n_capturing_groups) {
  emit_pattern(suite, pattern, strlen(pattern), n_capturing_groups);
}

static void
variadic_emit_testcase(test_suite_builder_t *suite, const char *str, size_t size, va_list args) {
  ASSERT(suite->n_capturing_groups != SIZE_MAX);

  ts_cmd_t *cmd = append_to_suite(suite, TS_STR_SIZE(size));
  ts_str_t *ts_str = &cmd->u.str;

  cmd->type = TS_CMD_STR;
  ts_str->size = size;
  memcpy(ts_str->str, str, size);

  match_t *matches = append_to_suite(suite, sizeof(match_t) * suite->n_capturing_groups);

  // Each variadic argument is a testcase_match_t signifying the expected subgroup match or lack
  // thereof. As a shorthand, if the first variadic argument is UNMATCHED, assume that no subgroups
  // match

  // Pretend that our matches correspond to a string at address 0x1. The test harness corrects this
  // later
  char *base = (char *)NULL + 1;

  int no_match = 0;

  for (size_t i = 0; i < suite->n_capturing_groups; i++) {
    const testcase_match_t match = no_match ? UNMATCHED : va_arg(args, testcase_match_t);

    if (match.substr == NULL) {
      // UNMATCHED or SPAN

      ASSERT((match.begin == SIZE_MAX) == (match.end == SIZE_MAX));

      if (match.begin == SIZE_MAX) {
        // UNMATCHED

        matches[i].begin = NULL;
        matches[i].end = NULL;

        if (i == 0) {
          no_match = 1;
        }
      } else {
        // SPAN
        matches[i].begin = base + match.begin;
        matches[i].end = base + match.end;
      }

      continue;
    }

    // SUBSTR or SUBSTR_AT

    const char *substr = match.substr;
    const size_t substr_size = strlen(substr);

    size_t begin = match.begin;

    if (begin == SIZE_MAX) {
      // SUBSTR

      for (begin = 0; begin + substr_size <= size; begin++) {
        if (memcmp(str + begin, substr, substr_size) == 0) {
          break;
        }
      }
    }

    ASSERT(begin + substr_size <= size && memcmp(str + begin, substr, substr_size) == 0);

    matches[i].begin = base + begin;
    matches[i].end = base + begin + substr_size;
  }
}

void emit_testcase(test_suite_builder_t *suite, const char *str, size_t size, ...) {
  va_list args;
  va_start(args, size);
  variadic_emit_testcase(suite, str, size, args);
  va_end(args);
}

void emit_testcase_str(test_suite_builder_t *suite, const char *str, ...) {
  va_list args;
  va_start(args, str);
  variadic_emit_testcase(suite, str, strlen(str), args);
  va_end(args);
}

#undef CHECK_ERROR

void *default_create(void) {
  (void)args;
  (void)n_args;

  context_t *context = crex_create_context(NULL);
  ASSERT(context != NULL);

  return context;
}

void default_destroy(void *context) {
  crex_destroy_context(context);
}

void *
default_compile_regex(void *data, const char *pattern, size_t size, size_t n_capturing_groups) {
  (void)data;

  status_t status;

  regex_t *regex = crex_compile(&status, pattern, size);
  ASSERT(regex != NULL);

  ASSERT(crex_regex_n_capturing_groups(regex) == n_capturing_groups);

  return regex;
}

void default_destroy_regex(void *data, void *regex) {
  (void)data;
  crex_destroy_regex(regex);
}

int run(int argc, char **argv, test_executor_t *tx) {
  argv0 = argv[0];

  char **paths = argv + 1;
  size_t n_paths = 0;

  while (n_paths + 1 < (size_t)argc) {
    if (strcmp(paths[n_paths], "--") == 0) {
      break;
    }

    n_paths++;
  }

  char **args = paths + n_paths + 1;
  const size_t n_args = argc - (1 + n_paths + 1);

  // Eagerly mmap all the suites to fail fast in the event of a bad path

  struct {
    size_t size;
    void *mapping;
  } * suites;

  suites = malloc(sizeof(*suites) * n_paths);
  ASSERT(suites != NULL);

  for (size_t i = 0; i < n_paths; i++) {
    const int fd = open(paths[i], O_RDONLY);
    ASSERT(fd != -1);

    struct stat statbuf;
    const int status = fstat(fd, &statbuf);
    ASSERT(status == 0);

    suites[i].size = statbuf.st_size;

    suites[i].mapping = mmap(NULL, suites[i].size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    ASSERT(suites[i].mapping != MAP_FAILED);

    close(fd);
  }

  match_t *matches = NULL;
  size_t max_groups = 0;

  size_t n_tests = 0;
  size_t n_passed = 0;

  void *tx_data = tx->create(args, n_args);

  for (size_t i = 0; i < n_paths; i++) {
    void *regex = NULL;
    size_t n_capturing_groups = SIZE_MAX;

    size_t k;

    for (k = 0; k < suites[i].size;) {
      ts_cmd_t *cmd = suites[i].mapping + k;

      if (cmd->type == TS_CMD_PATTERN) {
        ts_pattern_t *pattern = &cmd->u.pattern;
        k += TS_PATTERN_SIZE(pattern->size);

        if (regex != NULL) {
          tx->destroy_regex(tx_data, regex);
        }

        regex = tx->compile_regex(
            tx_data, pattern->pattern, pattern->size, pattern->n_capturing_groups);

        n_capturing_groups = pattern->n_capturing_groups;

        continue;
      }

      ASSERT(cmd->type == TS_CMD_STR);
      ASSERT(n_capturing_groups != SIZE_MAX);

      n_tests++;

      ts_str_t *str = &cmd->u.str;
      k += TS_STR_SIZE(str->size);

      if (max_groups < n_capturing_groups) {
        max_groups = 2 * n_capturing_groups;
        free(matches);
        matches = malloc(sizeof(match_t) * max_groups);
        ASSERT(matches != NULL);
      }

      tx->run_test(matches, tx_data, regex, str->str, str->size);

      match_t *expectation = suites[i].mapping + k;
      k += sizeof(match_t) * n_capturing_groups;

      if (!tx->is_benchmark) {
        for (size_t j = 0; j < n_capturing_groups; j++) {
          if (expectation[j].begin == NULL) {
            ASSERT(expectation[j].end == NULL);
            continue;
          }

          expectation[j].begin = str->str + (expectation[j].begin - ((char *)NULL + 1));
          expectation[j].end = str->str + (expectation[j].end - ((char *)NULL + 1));
        }

        n_passed += memcmp(matches, expectation, sizeof(match_t) * n_capturing_groups) == 0;
      }
    }

    ASSERT(k == suites[i].size);

    tx->destroy_regex(tx_data, regex);
  }

  tx->destroy(tx_data);

  if (tx->is_benchmark) {
    // FIXME
    return 0;
  }

  if (n_passed == n_tests && n_tests > 0) {
    fprintf(stderr, "\x1b[32m%zu/%zu test(s) passed\x1b[0m\n", n_passed, n_tests);
    return 1;
  }

  fprintf(stderr, "\x1b[33m%zu/%zu test(s) passed\x1b[0m\n", n_passed, n_tests);
  return 0;
}
