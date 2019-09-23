#include <string.h>

#include "../suite-builder.h"

int main(int argc, char **argv) {
  suite_builder_t *suite = create_test_suite_argv(argc, argv);

  char as[100];
  memset(as, 'a', sizeof(as));

  emit_pattern_str(suite, "a?", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, (i == 0) ? SUBSTR("") : SUBSTR("a"));
  }

  emit_pattern_str(suite, "a??", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, SUBSTR(""));
  }

  emit_pattern_str(suite, "a*", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, SPAN(0, i));
  }

  emit_pattern_str(suite, "a*?", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, SUBSTR(""));
  }

  emit_pattern_str(suite, "a+", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, (i == 0) ? UNMATCHED : SPAN(0, i));
  }

  emit_pattern_str(suite, "a+?", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, (i == 0) ? UNMATCHED : SUBSTR("a"));
  }

  emit_pattern_str(suite, "a{42}", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, (i < 42) ? UNMATCHED : SPAN(0, 42));
  }

  emit_pattern_str(suite, "a{42}?", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, (i < 42) ? UNMATCHED : SPAN(0, 42));
  }

  emit_pattern_str(suite, "a{37,64}", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, (i < 37) ? UNMATCHED : SPAN(0, (i <= 64) ? i : 64));
  }

  emit_pattern_str(suite, "a{37,64}?", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, (i < 37) ? UNMATCHED : SPAN(0, 37));
  }

  emit_pattern_str(suite, "a{42,}", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, (i < 42) ? UNMATCHED : SPAN(0, i));
  }

  emit_pattern_str(suite, "a{42,}?", 1);

  for (size_t i = 0; i <= sizeof(as); i++) {
    emit_testcase(suite, as, i, (i < 42) ? UNMATCHED : SPAN(0, 42));
  }

  finalize_test_suite(suite);

  return 0;
}
