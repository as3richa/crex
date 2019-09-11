#include <string.h>

#include "crex.h"
#include "framework.h"

#define MAX_CAPTURING_GROUPS 32

static int
run_test(void *context, const void *regex, const char *str, size_t size, match_t *expectation) {
  const size_t n_capturing_groups = crex_regex_n_capturing_groups(regex);

  match_t matches[MAX_CAPTURING_GROUPS];
  ASSERT(n_capturing_groups <= MAX_CAPTURING_GROUPS);

  ASSERT(crex_match_groups(matches, context, regex, str, size) == CREX_OK);

  return memcmp(matches, expectation, sizeof(match_t) * n_capturing_groups) == 0;
}

int main(int argc, char **argv) {
  test_executor_t tx = {NULL, NULL, NULL, run_test, NULL};
  return run_tests(argc, argv, &tx);
}
