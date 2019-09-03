#include <stdio.h>

#include "crex.h"

int main(void) {
  crex_status_t status;

  crex_context_t *context = crex_create_context(&status);

  if (context == NULL) {
    return 1;
  }

  const char *pattern = "a";
  crex_regex_t *regex = crex_compile_str(&status, pattern);

  if (regex == NULL) {
    crex_destroy_context(context);
    return 1;
  }

  const char *str = "a";

  int is_match;

  if (crex_is_match_str(&is_match, context, regex, str) != CREX_OK) {
    crex_destroy_context(context);
    crex_destroy_regex(regex);
    return 1;
  }

  printf("%d\n", is_match);

  crex_destroy_context(context);
  crex_destroy_regex(regex);

  return 0;
}
