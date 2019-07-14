#include <stdio.h>

#include "crex.h"

void crex_debug_lex(const char *, FILE *);
void crex_debug_parse(const char *, FILE *);
void crex_debug_compile(const char *, FILE *);

const char *pattern = "(alpha)+|(b(e+)ta)+";

const char *cases[] = {"alphaalphaalpha", "betabeeeeeeeeeeeeeeeeeeeeeta"};

const size_t n_cases = sizeof(cases) / sizeof(const char *);

int main(void) {
  printf("/%s/\n\n", pattern);
  /*crex_debug_lex(pattern, stdout);
  putchar('\n');
  crex_debug_parse(pattern, stdout);
  putchar('\n');
  crex_debug_compile(pattern, stdout);
  putchar('\n');*/

  crex_regex_t regex;
  crex_status_t status;

  status = crex_compile_str(&regex, pattern);

  if (status != CREX_OK) {
    return 1;
  }

  crex_context_t context;

  crex_create_context(&context);

  for (size_t i = 0; i < n_cases; i++) {
    int is_match;
    status = crex_is_match_str(&is_match, &context, &regex, cases[i]);

    if (status != CREX_OK) {
      crex_free_context(&context);
      crex_free_regex(&regex);
      return 1;
    }

    printf("\"%s\" => %d\n", cases[i], is_match);

    if (is_match) {
      crex_slice_t matches[4];
      status = crex_match_groups_str(matches, &context, &regex, cases[i]);

      if (status != CREX_OK) {
        crex_free_context(&context);
        crex_free_regex(&regex);
        return 1;
      }

      for (size_t i = 0; i < 4; i++) {
        printf("$%zu = \"", i);

        for (size_t j = 0; j < matches[i].size; j++) {
          putchar(matches[i].start[j]);
        }

        puts("\"");
      }
    }

    puts("\n");
  }

  crex_free_context(&context);
  crex_free_regex(&regex);

  return 0;
}
