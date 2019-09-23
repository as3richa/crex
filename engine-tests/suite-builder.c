#undef NDEBUG

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "crex.h"
#include "suite-builder.h"
#include "suite.h"

struct suite_builder {
  int fd;

  size_t size;
  size_t capacity;

  char *mapping;

  struct {
    size_t size;
    size_t capacity;
    suite_pattern_t *patterns;
  } patterns;

  struct {
    size_t size;
    size_t capacity;
    suite_testcase_t *testcases;
  } testcases;

  size_t prev_pattern_index;
};

static void *append(suite_builder_t *suite, size_t size);
static void align_to_size_t(suite_builder_t *suite);

static void safe_memcpy(void *dest, const void *source, size_t size);

suite_builder_t *create_test_suite(const char *path) {
  suite_builder_t *suite = malloc(sizeof(suite_builder_t));
  assert(suite != NULL);

  // User/group read-write permissions
  suite->fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0x1b0u);
  assert(suite->fd != -1);

  suite->size = 0;
  suite->capacity = 0;

  append(suite, sizeof(suite_header_t));

  suite->patterns.size = 0;
  suite->patterns.capacity = 0;
  suite->patterns.patterns = NULL;

  suite->testcases.size = 0;
  suite->testcases.capacity = 0;
  suite->testcases.testcases = NULL;

  suite->prev_pattern_index = SIZE_MAX;

  return suite;
}

suite_builder_t *create_test_suite_argv(int argc, char **argv) {
  assert(argc == 2);
  return create_test_suite(argv[1]);
}

void finalize_test_suite(suite_builder_t *suite) {
  align_to_size_t(suite);

  suite_header_t *header = (void *)suite->mapping;
  header->patterns_offset = suite->size;
  header->n_patterns = suite->patterns.size;
  header->n_testcases = suite->testcases.size;

  const size_t required_size = required_suite_size(header);

  if (suite->capacity < required_size) {
    append(suite, required_size - suite->capacity);
  }

  int status = ftruncate(suite->fd, required_size);
  assert(status != -1);

  status = close(suite->fd);
  assert(status != -1);

  suite_t unpacked;
  unpack_suite(&unpacked, suite->mapping, required_size);

  safe_memcpy(
      unpacked.patterns, suite->patterns.patterns, sizeof(suite_pattern_t) * unpacked.n_patterns);

  safe_memcpy(unpacked.testcases,
              suite->testcases.testcases,
              sizeof(suite_testcase_t) * unpacked.n_testcases);

  status = msync(suite->mapping, suite->capacity, MS_SYNC);
  assert(status != -1);

  munmap(suite->mapping, suite->capacity);

  free(suite->patterns.patterns);
  free(suite->testcases.testcases);

  free(suite);
}

void emit_pattern(suite_builder_t *suite,
                  const char *pattern,
                  size_t size,
                  size_t n_capturing_groups) {
  assert(suite->patterns.size <= suite->patterns.capacity);

  if (suite->patterns.size == suite->patterns.capacity) {
    suite->patterns.capacity = 2 * suite->patterns.capacity + 1;

    suite->patterns.patterns =
        realloc(suite->patterns.patterns, sizeof(suite_pattern_t) * suite->patterns.capacity);

    assert(suite->patterns.patterns != NULL);
  }

  const size_t index = suite->patterns.size++;
  suite_pattern_t *suite_pattern = &suite->patterns.patterns[index];
  suite_pattern->size = size;
  suite_pattern->n_capturing_groups = n_capturing_groups;
  suite_pattern->offset = suite->size;

  safe_memcpy(append(suite, size), pattern, size);

  suite->prev_pattern_index = index;
}

void emit_pattern_str(suite_builder_t *suite, const char *pattern, size_t n_capturing_groups) {
  emit_pattern(suite, pattern, strlen(pattern), n_capturing_groups);
}

void emit_pattern_sb(suite_builder_t *suite,
                     const str_builder_t *pattern,
                     size_t n_capturing_groups) {
  emit_pattern(suite, sb2str(pattern), sb_size(pattern), n_capturing_groups);
}

static void
variadic_emit_testcase(suite_builder_t *suite, const char *str, size_t size, va_list args) {
  assert(suite->testcases.size <= suite->testcases.capacity);
  assert(suite->prev_pattern_index != SIZE_MAX);

  if (suite->testcases.size == suite->testcases.capacity) {
    suite->testcases.capacity = 2 * suite->testcases.capacity + 1;

    suite->testcases.testcases =
        realloc(suite->testcases.testcases, sizeof(suite_testcase_t) * suite->testcases.capacity);

    assert(suite->testcases.testcases != NULL);
  }

  suite_testcase_t *testcase = &suite->testcases.testcases[suite->testcases.size++];
  const suite_pattern_t *pattern = &suite->patterns.patterns[suite->prev_pattern_index];

  testcase->pattern_index = suite->prev_pattern_index;
  testcase->size = size;

  testcase->str_offset = suite->size;
  safe_memcpy(append(suite, size), str, size);

  align_to_size_t(suite);
  testcase->matches_offset = suite->size;
  size_t *match = append(suite, sizeof(size_t) * 2 * pattern->n_capturing_groups);

  // Each variadic argument is a match_expectation_t signifying the expected subgroup match or lack
  // thereof. As a shorthand, if the first variadic argument is UNMATCHED, assume that no subgroups
  // match

  int first_vararg = 1;

  for (size_t i = 0; i < pattern->n_capturing_groups; i++, match += 2, first_vararg = 0) {
    const match_expectation_t match_ex = va_arg(args, match_expectation_t);

    if (match_ex.substr == NULL && match_ex.begin == SIZE_MAX) {
      // UNMATCHED

      assert(match_ex.end == SIZE_MAX);

      if (first_vararg) {
        for (size_t j = 0; j < 2 * pattern->n_capturing_groups; j++) {
          match[j] = SIZE_MAX;
        }

        break;
      }

      match[0] = SIZE_MAX;
      match[1] = SIZE_MAX;

      continue;
    }

    if (match_ex.substr == NULL) {
      // SPAN(begin, end)

      assert(match_ex.begin <= match_ex.end && match_ex.end != SIZE_MAX);

      match[0] = match_ex.begin;
      match[1] = match_ex.end;

      continue;
    }

    // SUBSTR(substr) or SUBST_AT(substr, begin)

    const size_t substr_size = strlen(match_ex.substr);

    size_t begin = match_ex.begin;

    if (begin == SIZE_MAX) {
      // SUBSTR

      for (begin = 0; begin + substr_size <= size; begin++) {
        if (memcmp(str + begin, match_ex.substr, substr_size) == 0) {
          break;
        }
      }
    }

    assert(begin != SIZE_MAX && begin + substr_size <= size);
    assert(memcmp(str + begin, match_ex.substr, substr_size) == 0);

    match[0] = begin;
    match[1] = begin + substr_size;
  }
}

void emit_testcase(suite_builder_t *suite, const char *str, size_t size, ...) {
  va_list args;
  va_start(args, size);
  variadic_emit_testcase(suite, str, size, args);
  va_end(args);
}

void emit_testcase_str(suite_builder_t *suite, const char *str, ...) {
  va_list args;
  va_start(args, str);
  variadic_emit_testcase(suite, str, strlen(str), args);
  va_end(args);
}

void emit_testcase_sb(suite_builder_t *suite, const str_builder_t *str, ...) {
  va_list args;
  va_start(args, str);
  variadic_emit_testcase(suite, sb2str(str), sb_size(str), args);
  va_end(args);
}

static void *append(suite_builder_t *suite, size_t size) {
  if (suite->size + size <= suite->capacity) {
    void *result = suite->mapping + suite->size;
    suite->size += size;

    return result;
  }

  int status = msync(suite->mapping, suite->capacity, MS_SYNC);
  assert(status != -1);

  munmap(suite->mapping, suite->capacity);

  suite->capacity = 2 * suite->capacity + size;
  assert(suite->size + size <= suite->capacity);

  status = ftruncate(suite->fd, suite->capacity);
  assert(status != -1);

  void *mapping = mmap(NULL, suite->capacity, PROT_READ | PROT_WRITE, MAP_SHARED, suite->fd, 0);
  assert(mapping != MAP_FAILED);

  suite->mapping = mapping;

  void *result = suite->mapping + suite->size;
  suite->size += size;

  return result;
}

static void align_to_size_t(suite_builder_t *suite) {
  if (suite->size % sizeof(size_t) == 0) {
    return;
  }

  append(suite, sizeof(size_t) - suite->size % sizeof(size_t));
}

static void safe_memcpy(void *dest, const void *source, size_t size) {
  assert(size == 0 || (dest != NULL && source != NULL));

  if (size == 0) {
    return;
  }

  memcpy(dest, source, size);
}
