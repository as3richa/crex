#include <stdio.h>

#include "crex.h"

const char *pattern = "(alpha)+|(b(e+)ta)+";

const char *cases[] = {"alpha",
                       "alphaalpha",
                       "alphaalphaalpha",
                       "beta",
                       "beeta",
                       "betabeeta",
                       "betabeetabeeeta",
                       "bet",
                       "bta",
                       "alphabeta",
                       "kappa"};

const size_t n_cases = sizeof(cases) / sizeof(const char *);

int main(void) {
  crex_regex_t regex;
  crex_status_t status;

  printf("/%s/\n", pattern);

  status = crex_compile_str(&regex, pattern);

  if (status != CREX_OK) {
    return 1;
  }

  for (size_t i = 0; i < n_cases; i++) {
    crex_match_result_t match_result;
    crex_match_str(&match_result, &regex, cases[i]);

    printf("\"%s\" => %d\n", cases[i], match_result.matched);
  }

  crex_free_regex(&regex);

  return 0;
}
