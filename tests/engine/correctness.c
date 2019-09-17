#include <stdlib.h>
#include <string.h>

#include "crex.h"
#include "framework.h"

#define MAX_CAPTURING_GROUPS 32

static void
run_test(match_t *matches, void *context, const void *regex, const char *str, size_t size) {
  const status_t status = crex_match_groups(matches, context, regex, str, size);
  ASSERT(status == CREX_OK);
}

int main(int argc, char **argv) {
  test_executor_t tx;

  tx.is_benchmark = 0;
  tx.compilation_only = 0;

  tx.create = default_create;
  tx.destroy = default_destroy;
  tx.compile_regex = default_compile_regex;
  tx.destroy_regex = default_destroy_regex;
  tx.run_test = run_test;

  return run(argc, argv, &tx) ? 0 : EXIT_FAILURE;
}
