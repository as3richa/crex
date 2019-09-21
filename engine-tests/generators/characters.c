#include "../framework/suite-builder.h"

int main(int argc, char **argv) {
  suite_builder_t *suite = create_test_suite_argv(argc, argv);

  const char *special_chars = ".*+?[^$()|\\";

  for (int c = 1; c < 256; c++) {
    int special = 0;

    for (size_t i = 0; special_chars[i] != 0; i++) {
      if (c == (unsigned char)special_chars[i]) {
        special = 1;
        break;
      }
    }

    if (special) {
      continue;
    }

    char pattern[] = {c};
    emit_pattern(suite, pattern, 1, 1);

    for (int d = 0; d < 256; d++) {
      char str[] = {d};
      emit_testcase(suite, str, 1, (c == d) ? SPAN(0, 1) : UNMATCHED);
    }
  }

  finalize_test_suite(suite);

  return 0;
}
