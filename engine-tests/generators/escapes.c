#include <stdio.h>

#include "../suite-builder.h"

int main(int argc, char **argv) {
  suite_builder_t *suite = create_test_suite_argv(argc, argv);

  const char *regex_punctuation = ".*+?[]{}^$()|\\";

  for (size_t i = 0; regex_punctuation[i]; i++) {
    char pattern[] = {'\\', regex_punctuation[i]};

    emit_pattern(suite, pattern, 2, 1);

    for (int c = 0; c < 256; c++) {
      char str[] = {c};
      emit_testcase(suite, str, 1, (c == regex_punctuation[i]) ? SPAN(0, 1) : UNMATCHED);
    }
  }

  for (int c = 0; c < 256; c++) {
    for (size_t k = 0; k <= 1; k++) {
      char pattern[5];
      sprintf(pattern, (k == 0) ? "\\x%x%x" : "\\x%X%X", c / 16, c % 16);

      emit_pattern_str(suite, pattern, 1);

      for (int d = 0; d < 256; d++) {
        char str[] = {d};
        emit_testcase(suite, str, 1, (c == d) ? SPAN(0, 1) : UNMATCHED);
      }

      if (c / 16 <= 9 || c % 16 <= 9) {
        break;
      }
    }
  }

  finalize_test_suite(suite);

  return 0;
}
