#undef NDEBUG

// For ftruncate. FIXME
#define _GNU_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "builder.h"
#include "common.h"
#include "crex.h"

struct test_suite_builder {
  const char *path;
  int fd;

  size_t size;

  size_t page_size;
  size_t capacity;

  char *mapping;

  size_t n_capturing_groups;
};

#define CHECK_ERROR(condition, suite)                                                              \
  (condition || ((delete_test_suite(suite), 1) && (assert(condition), 1)))

static void delete_test_suite(test_suite_builder_t *suite);

test_suite_builder_t *create_test_suite(const char *path) {
  test_suite_builder_t *suite = malloc(sizeof(test_suite_builder_t));
  assert(suite != NULL);

  // User/group read-write permissions
  suite->fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0x1b0u);
  assert(suite->fd != -1);

  suite->size = 0;

  suite->page_size = sysconf(_SC_PAGESIZE);
  CHECK_ERROR(suite->page_size > 0, suite);

  suite->capacity = 0;

  suite->n_capturing_groups = SIZE_MAX;

  return suite;
}

test_suite_builder_t *create_test_suite_argv(int argc, char **argv) {
  assert(argc == 2);
  return create_test_suite(argv[1]);
}

static void delete_test_suite(test_suite_builder_t *suite) {
  unlink(suite->path);
}

void finalize_test_suite(test_suite_builder_t *suite) {
  ftruncate(suite->fd, suite->size);
  close(suite->fd);

  munmap(suite->mapping, suite->capacity);
  munmap(suite->mapping, suite->capacity);

  free(suite);
}

static void *append_to_suite(test_suite_builder_t *suite, size_t size);

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
  assert(suite->n_capturing_groups != SIZE_MAX);

  ts_cmd_t *cmd = append_to_suite(suite, TS_STR_SIZE(size));
  ts_str_t *ts_str = &cmd->u.str;

  cmd->type = TS_CMD_STR;
  ts_str->size = size;
  memcpy(ts_str->str, str, size);

  crex_match_t *matches = append_to_suite(suite, sizeof(crex_match_t) * suite->n_capturing_groups);

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

      assert((match.begin == SIZE_MAX) == (match.end == SIZE_MAX));

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

    assert(begin + substr_size <= size && memcmp(str + begin, substr, substr_size) == 0);

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

static void *append_to_suite(test_suite_builder_t *suite, size_t size) {
  if (suite->size + size <= suite->capacity) {
    char *result = suite->mapping + suite->size;
    suite->size += size;

    return result;
  }

  int status = msync(suite->mapping, suite->capacity, MS_SYNC);
  CHECK_ERROR(status != -1, suite);

  munmap(suite->mapping, suite->capacity);

  suite->capacity = 2 * suite->capacity + suite->page_size;
  assert(suite->size + size <= suite->capacity);

  status = ftruncate(suite->fd, suite->capacity);
  CHECK_ERROR(status != -1, suite);

  void *mapping = mmap(NULL, suite->capacity, PROT_WRITE, MAP_SHARED, suite->fd, 0);
  CHECK_ERROR(mapping != MAP_FAILED, suite);

  suite->mapping = mapping;

  void *result = suite->mapping + suite->size;
  suite->size += size;

  return result;
}

#undef CHECK_ERROR
