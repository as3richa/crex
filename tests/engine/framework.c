#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "framework.h"

#if (defined(__GNUC__) || defined(__clang__))
#define NO_RETURN __attribute__((noreturn))
#else
#define NO_RETURN
#endif

NO_RETURN static void die(const char *format, ...) {
  fputs("\x1b[31m", stderr);

  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  fputs("\x1b[0m\n", stderr);

  abort();
}

static int fwrite_u32(size_t value, FILE *file) {
  unsigned char buffer[4];
  buffer[0] = value & 0xffu;
  buffer[1] = (value >> 8u) & 0xffu;
  buffer[2] = (value >> 16u) & 0xffu;
  buffer[3] = value >> 24u;

  return fwrite(buffer, 1, 4, file) == 4;
}

static int fread_u32(size_t *value, FILE *file) {
  unsigned char buffer[4];

  if (fread(buffer, 1, 4, file) != 4) {
    return 0;
  }

  *value = 0;

  for (size_t i = 0; i < 4; i++) {
    *value |= ((size_t)buffer[i]) << (8 * i);
  }

  return 1;
}

// Functions for creating test suite files

enum { TS_COMMAND_PATTERN, TS_COMMAND_TESTCASE };

struct test_suite_builder {
  const char *path;
  FILE *file;

  size_t last_n_capturing_groups;
};

#define TS_BUILDER_ERROR(suite, name) die("%s: %s: %s failed", __func__, (suite)->path, name)

static void delete_test_suite(test_suite_builder_t *suite) {
  remove(suite->path);
}

test_suite_builder_t *create_test_suite(const char *path) {
  test_suite_builder_t *suite = malloc(sizeof(test_suite_builder_t));

  if (suite == NULL) {
    TS_BUILDER_ERROR(suite, "malloc");
  }

  suite->file = fopen(path, "wb");

  if (suite->file == NULL) {
    TS_BUILDER_ERROR(suite, "fopen");
  }

  suite->path = path;
  suite->last_n_capturing_groups = SIZE_MAX;

  return suite;
}

void close_test_suite(test_suite_builder_t *suite) {
  if (fclose(suite->file) == 0) {
    return;
  }

  TS_BUILDER_ERROR(suite, "fclose");
}

void emit_pattern(test_suite_builder_t *suite,
                  const char *pattern,
                  size_t size,
                  size_t n_capturing_groups) {
  if (fputc(TS_COMMAND_PATTERN, suite->file) == EOF) {
    TS_BUILDER_ERROR(suite, "fputc");
  }

  if (!fwrite_u32(size, suite->file)) {
    TS_BUILDER_ERROR(suite, "fwrite_u32");
  }

  if (fwrite(pattern, 1, size, suite->file) != size) {
    TS_BUILDER_ERROR(suite, "fwrite");
  }

  suite->last_n_capturing_groups = n_capturing_groups;
}

void emit_pattern_str(test_suite_builder_t *suite, const char *pattern, size_t n_capturing_groups) {
  emit_pattern(suite, pattern, strlen(pattern), n_capturing_groups);
}

void v_emit_testcase(test_suite_builder_t *suite, const char *str, size_t size, va_list args) {
  assert(suite->last_n_capturing_groups != SIZE_MAX);

  if (fputc(TS_COMMAND_TESTCASE, suite->file) == EOF) {
    TS_BUILDER_ERROR(suite, "fputc");
  }

  if (!fwrite_u32(size, suite->file)) {
    TS_BUILDER_ERROR(suite, "fwrite_u32");
  }

  if (fwrite(str, 1, size, suite->file) != size) {
    TS_BUILDER_ERROR(suite, "fwrite");
  }

  // Map match pointers to indices, with NULL => U32_MAX, because obviously the string won't be in
  // the same address when the test file is read

  int no_match = 0;

  for (size_t i = 0; i < suite->last_n_capturing_groups; i++) {
    testcase_match_t match = no_match ? UNMATCHED : va_arg(args, testcase_match_t);

    if (match.substr == NULL) {
      if (i == 0) {
        no_match = 1;
      }

      if (!fwrite_u32(UINT32_MAX, suite->file) || !fwrite_u32(UINT32_MAX, suite->file)) {
        TS_BUILDER_ERROR(suite, "fwrite_u32");
      }

      continue;
    }

    const char *substr = match.substr;
    const size_t substr_size = strlen(substr);

    if (match.position == SIZE_MAX) {
      for (size_t position = 0; position + substr_size <= size; position++) {
        if (memcmp(str + position, substr, substr_size) == 0) {
          match.position = position;
          break;
        }
      }
    }

    assert(match.position != SIZE_MAX);

    const size_t begin = match.position;
    const size_t end = begin + substr_size;

    assert(end <= size);
    assert(memcmp(str + begin, substr, substr_size) == 0);

    if (!fwrite_u32(begin, suite->file) || !fwrite_u32(end, suite->file)) {
      TS_BUILDER_ERROR(suite, "fwrite_u32");
    }
  }
}

void emit_testcase(test_suite_builder_t *suite, const char *str, size_t size, ...) {
  va_list args;
  va_start(args, size);
  v_emit_testcase(suite, str, size, args);
  va_end(args);
}

void emit_testcase_str(test_suite_builder_t *suite, const char *str, ...) {
  va_list args;
  va_start(args, str);
  v_emit_testcase(suite, str, strlen(str), args);
  va_end(args);
}

#undef TS_BUILDER_ERROR
