#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "framework.h"

static const char *argv0;

NO_RETURN void die(const char *format, ...) {
  fprintf(stderr, "\x1b[31m%s: ", argv0);

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

typedef enum { TS_CMD_PATTERN, TS_CMD_TESTCASE } testsuite_cmd_t;

struct test_suite_builder {
  const char *path;
  FILE *file;

  size_t last_n_capturing_groups;
};

#define CHECK_ERROR(condition, suite)                                                              \
  (condition || ((delete_test_suite(suite), 1) && (ASSERT(condition), 1)))

static void delete_test_suite(test_suite_builder_t *suite) {
  remove(suite->path);
}

test_suite_builder_t *create_test_suite(const char *path) {
  test_suite_builder_t *suite = malloc(sizeof(test_suite_builder_t));
  ASSERT(suite != NULL);

  suite->file = fopen(path, "wb");
  ASSERT(suite->file != NULL);

  suite->path = path;
  suite->last_n_capturing_groups = SIZE_MAX;

  return suite;
}

void close_test_suite(test_suite_builder_t *suite) {
  CHECK_ERROR(fclose(suite->file) == 0, suite);
  free(suite);
}

void emit_pattern(test_suite_builder_t *suite,
                  const char *pattern,
                  size_t size,
                  size_t n_capturing_groups) {
  fputc(TS_CMD_PATTERN, suite->file);
  fwrite_u32(size, suite->file);
  fwrite(pattern, 1, size, suite->file);
  fwrite_u32(n_capturing_groups, suite->file);

  ASSERT(!ferror(suite->file));

  suite->last_n_capturing_groups = n_capturing_groups;
}

void emit_pattern_str(test_suite_builder_t *suite, const char *pattern, size_t n_capturing_groups) {
  emit_pattern(suite, pattern, strlen(pattern), n_capturing_groups);
}

static void
variadic_emit_testcase(test_suite_builder_t *suite, const char *str, size_t size, va_list args) {
  ASSERT(suite->last_n_capturing_groups != SIZE_MAX);

  fputc(TS_CMD_TESTCASE, suite->file);
  fwrite_u32(size, suite->file);
  fwrite(str, 1, size, suite->file);

  // Map match pointers to indices, with NULL => U32_MAX, because obviously the string won't be in
  // the same address when the test file is read

  int no_match = 0;

  for (size_t i = 0; i < suite->last_n_capturing_groups; i++) {
    const testcase_match_t match = no_match ? UNMATCHED : va_arg(args, testcase_match_t);

    if (match.substr == NULL) {
      ASSERT((match.begin == SIZE_MAX) == (match.end == SIZE_MAX));

      if (match.begin == SIZE_MAX) {
        fwrite_u32(UINT32_MAX, suite->file);
        fwrite_u32(UINT32_MAX, suite->file);

        if (i == 0) {
          no_match = 1;
        }
      } else {
        fwrite_u32(match.begin, suite->file);
        fwrite_u32(match.end, suite->file);
      }

      continue;
    }

    const char *substr = match.substr;
    const size_t substr_size = strlen(substr);

    size_t begin = match.begin;

    if (begin == SIZE_MAX) {
      for (begin = 0; begin + substr_size <= size; begin++) {
        if (memcmp(str + begin, substr, substr_size) == 0) {
          break;
        }
      }
    }

    ASSERT(begin + substr_size <= size);

    const size_t end = begin + substr_size;
    ASSERT(end <= size);

    ASSERT(memcmp(str + begin, substr, substr_size) == 0);

    fwrite_u32(begin, suite->file);
    fwrite_u32(end, suite->file);
  }

  ASSERT(!ferror(suite->file));
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

void *default_create(char **args, size_t n_args) {
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

  // fopen all the suite files ahead of time to fail early in the case of a bad path
  FILE **files = malloc(sizeof(FILE *) * n_paths);

  for (size_t i = 0; i < n_paths; i++) {
    files[i] = fopen(paths[i], "rb");

    if (files[i] == NULL) {
      die("%s: fopen failed", paths[i]);
    }
  }

  struct {
    size_t capacity;
    size_t size;
    char *str;
  } buffer = {0, 0, NULL};

  match_t *expectation = NULL;
  match_t *matches = NULL;
  size_t max_groups = 0;

  size_t n_tests = 0;
  size_t n_passed = 0;

  void *tx_data = tx->create(args, n_args);

  for (size_t i = 0; i < n_paths; i++) {
    void *regex = NULL;
    size_t n_capturing_groups;

    int compiled = 0;

    for (;;) {
      const int cmd = fgetc(files[i]);

      if (cmd == EOF) {
        break;
      }

      const int ok = fread_u32(&buffer.size, files[i]);
      ASSERT(ok);

      if (buffer.capacity < buffer.size) {
        buffer.capacity = 2 * buffer.size;
        buffer.str = malloc(buffer.capacity);
        ASSERT(buffer.str != NULL);
      }

      const size_t read = fread(buffer.str, 1, buffer.size, files[i]);
      ASSERT(read == buffer.size);

      if (cmd == TS_CMD_PATTERN) {
        const int ok = fread_u32(&n_capturing_groups, files[i]);
        ASSERT(ok);

        if (regex != NULL) {
          tx->destroy_regex(tx_data, regex);
        }

        regex = tx->compile_regex(tx_data, buffer.str, buffer.size, n_capturing_groups);
        compiled = 1;

        if (!tx->is_benchmark && max_groups < n_capturing_groups) {
          max_groups = 2 * n_capturing_groups;

          free(expectation);
          expectation = malloc(sizeof(match_t) * 2 * max_groups);
          ASSERT(expectation != NULL);

          free(matches);
          matches = malloc(sizeof(match_t) * 2 * max_groups);
        }

        continue;
      }

      ASSERT(cmd == TS_CMD_TESTCASE);
      ASSERT(compiled);

      n_tests++;

      // Reconstruct the expected result of crex_match_groups or equivalent.
      // UINT32_MAX => NULL, k => str + k
      for (size_t j = 0; j < n_capturing_groups; j++) {
        size_t begin, end;

        int ok = fread_u32(&begin, files[i]);
        ASSERT(ok);

        ok = fread_u32(&end, files[i]);
        ASSERT(ok);

        ASSERT((begin == UINT32_MAX) == (end == UINT32_MAX));

        if (begin == UINT32_MAX) {
          expectation[j].begin = NULL;
          expectation[j].end = NULL;
        } else {
          expectation[j].begin = buffer.str + begin;
          expectation[j].end = buffer.str + end;
        }
      }

      tx->run_test(matches, tx_data, regex, buffer.str, buffer.size);

      if (!tx->is_benchmark) {
        n_passed += memcmp(matches, expectation, sizeof(match_t) * n_capturing_groups) == 0;
      }
    }

    ASSERT(!ferror(files[i]));

    if (regex != NULL) {
      tx->destroy_regex(tx_data, regex);
    }

    fclose(files[i]);
  }

  free(buffer.str);
  free(expectation);
  free(files);

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
