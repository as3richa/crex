#include <stdio.h>
#include <stdlib.h>

#include "crex.h"

void crex_dump_native_code(const crex_regex_t *regex, FILE *file);

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <regex>\n", argv[0]);
    return 1;
  }

  crex_status_t status;
  crex_regex_t *regex = crex_compile_str(&status, argv[1]);

  if (regex == NULL) {
    return 1;
  }

  crex_dump_native_code(regex, stdout);

  crex_destroy_regex(regex);

  return 0;
}
