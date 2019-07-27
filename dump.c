#include <stdio.h>
#include <stdlib.h>

#include "crex.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <regex>\n", argv[0]);
    return 1;
  }

  crex_status_t status;
  crex_regex_t *regex = crex_compile_str(&status, argv[0]);

  if (regex == NULL) {
    return 1;
  }

  size_t size;
  unsigned char *buffer = crex_dump_regex(&status, &size, regex);

  if (buffer == NULL) {
    crex_destroy_regex(regex);
    return 1;
  }

  printf("size_t size = %zu\n", size);

  printf("unsigned char* buffer = {");

  for (size_t i = 0; i < size; i++) {
    printf("0x%02x%s", buffer[i], (i == size - 1) ? "};\n" : ", ");
  }

  free(buffer);
  crex_destroy_regex(regex);

  return 0;
}
