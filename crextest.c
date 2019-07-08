#include <stdio.h>
#include <string.h>

#include "crex.h"

int main(void) {
  const char *regex_str = "a+|b*|c?\\x41\\xAA";
  const size_t regex_length = strlen(regex_str);

  fprintf(stderr, "%s\n\n", regex_str);

  crex_debug_lex(regex_str, regex_length);

  fputc('\n', stderr);

  crex_debug_parse(regex_str, regex_length);

  return 0;
}
