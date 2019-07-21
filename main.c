#include <stdio.h>

#include "crex.h"

void crex_debug_lex(const char *, FILE *);
void crex_debug_parse(const char *, FILE *);
void crex_debug_compile(const char *, FILE *);

const char *pattern = "a{13,37}?";

const char *cases[] = {"", "a", "aaa", "aaaaa", "aaaaaa"};

const size_t n_cases = sizeof(cases) / sizeof(const char *);

int main(void) {
  printf("/%s/\n\n", pattern);

  crex_debug_lex(pattern, stdout);
  putchar('\n');

  crex_debug_parse(pattern, stdout);
  putchar('\n');

  crex_debug_compile(pattern, stdout);
  putchar('\n');

  crex_status_t status;

  crex_regex_t *regex = crex_compile_str(&status, pattern);

  if (status != CREX_OK) {
    return 1;
  }

  const size_t n_groups = crex_regex_n_groups(regex);

  crex_context_t *context = crex_create_context(&status);

  if (status != CREX_OK) {
    crex_destroy_regex(regex);
    return 1;
  }

  for (size_t i = 0; i < n_cases; i++) {
    int is_match;
    status = crex_is_match_str(&is_match, context, regex, cases[i]);

    if (status != CREX_OK) {
      crex_destroy_context(context);
      crex_destroy_regex(regex);
      return 1;
    }

    printf("\"%s\" => %d\n", cases[i], is_match);

    if (is_match) {
      crex_slice_t matches[1000];
      status = crex_match_groups_str(matches, context, regex, cases[i]);

      if (status != CREX_OK) {
        crex_destroy_context(context);
        crex_destroy_regex(regex);
        return 1;
      }

      for (size_t i = 0; i < n_groups; i++) {
        printf("$%zu = \"", i);

        for (const char *iter = matches[i].begin; iter != matches[i].end; iter++) {
          putchar(*iter);
        }

        puts("\"");
      }
    }

    puts("\n");
  }

  crex_destroy_regex(regex);
  crex_destroy_context(context);

  return 0;
}
