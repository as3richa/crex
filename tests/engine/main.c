#include <stdint.h>

#include "framework.h"

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

  return 0;
}
