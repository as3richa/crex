#include <stdint.h>
#include <string.h>

#include "builder.h"

#define MATCH(str, position)

int main(void) {
  test_suite_builder_t *suite = create_test_suite("ipv4.bin");

  emit_pattern_str(
      suite,
      "\\A(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{"
      "0,2})\\z",
      5);

  emit_testcase_str(suite, "asdf", UNMATCHED);

  emit_testcase_str(suite,
                    "127.0.0.1",
                    SUBSTR("127.0.0.1"),
                    SUBSTR("127"),
                    SUBSTR("0"),
                    SUBSTR_AT("0", 6),
                    SUBSTR_AT("1", 8));

  {
    emit_pattern_str(suite, "a{13,37}", 1);

    char str_of_a[100];
    memset(str_of_a, 'a', sizeof(str_of_a));

    for (size_t i = 0; i <= sizeof(str_of_a); i++) {
      emit_testcase(suite, str_of_a, i, ((13 <= i) ? SPAN(0, (i <= 37) ? i : 37) : UNMATCHED));
    }
  }

  finalize_test_suite(suite);

  return 0;
}
