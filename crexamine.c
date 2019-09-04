#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crex.h"

#define MAX_GROUPS 32

crex_status_t crex_print_tokenization(const char *pattern, const size_t size, FILE *file);
crex_status_t crex_print_parsetree(const char *pattern, const size_t size, FILE *file);
void crex_print_bytecode(const crex_regex_t *regex, FILE *file);

typedef struct {
  size_t size;
  size_t capacity;
  char *buffer;
} string_t;

int read_line(string_t *str);
void print(const string_t *str);

int main(void) {
  string_t pattern = {0, 0, NULL};

  fputs("pattern> ", stdout);

  if (!read_line(&pattern)) {
    free(pattern.buffer);
    return 1;
  }

  putchar('\n');

  print(&pattern);

  puts("\n");

  crex_status_t status;

  status = crex_print_tokenization(pattern.buffer, pattern.size, stdout);

  if (status != CREX_OK) {
    free(pattern.buffer);
    return 1;
  }

  putchar('\n');

  status = crex_print_parsetree(pattern.buffer, pattern.size, stdout);

  if (status != CREX_OK) {
    free(pattern.buffer);
    return 1;
  }

  putchar('\n');

  if (status != CREX_OK) {
    free(pattern.buffer);
    return 1;
  }

  crex_regex_t *regex = crex_compile(&status, pattern.buffer, pattern.size);

  if (regex == NULL) {
    free(pattern.buffer);
    return 1;
  }

  crex_print_bytecode(regex, stdout);

  const size_t n_capturing_groups = crex_regex_n_capturing_groups(regex);

  crex_context_t *context = crex_create_context(&status);

  if (context == NULL) {
    crex_destroy_regex(regex);
    free(pattern.buffer);
    return 1;
  }

  string_t str = {0, 0, NULL};

  while (!feof(stdin)) {
    putchar('\n');
    fputs("string> ", stdout);

    if (!read_line(&str)) {
      crex_destroy_context(context);
      crex_destroy_regex(regex);
      free(pattern.buffer);
      return 1;
    }

    crex_match_t groups[MAX_GROUPS];

    status = crex_match_groups(groups, context, regex, str.buffer, str.size);

    if (status != CREX_OK) {
      crex_destroy_context(context);
      crex_destroy_regex(regex);
      free(pattern.buffer);
      return 1;
    }

    putchar('\n');

    for (size_t i = 0; i < n_capturing_groups; i++) {
      printf("$%zu: ", i);

      if (groups[i].begin == NULL) {
        assert(groups[i].end == NULL);

        puts("<unmatched>");
        continue;
      }

      putchar('"');

      for (const char *it = groups[i].begin; it != groups[i].end; it++) {
        if (*it == '"') {
          fputs("\\\"", stdout);
        } else if (isprint(*it) || *it == ' ') {
          putchar(*it);
        } else {
          printf("\\x%02x", (unsigned int)*it);
        }
      }

      putchar('"');

      printf(" (@ %zu)\n", (size_t)(groups[i].begin - str.buffer));
    }
  }

  crex_destroy_context(context);
  crex_destroy_regex(regex);
  free(pattern.buffer);

  return 0;
}

int read_line(string_t *str) {
  str->size = 0;

  for (;;) {
    assert(str->size <= str->capacity);

    const int c = getchar();

    if (c == EOF || c == '\n') {
      break;
    }

    if (str->size == str->capacity) {
      const size_t capacity = 2 * str->capacity + 1;
      char *buffer = realloc(str->buffer, capacity);

      if (buffer == NULL) {
        return 0;
      }

      str->capacity = capacity;
      str->buffer = buffer;
    }

    str->buffer[str->size++] = c;
  }

  return 1;
}

void print(const string_t *str) {
  for (size_t i = 0; i < str->size; i++) {
    putchar(str->buffer[i]);
  }
}
