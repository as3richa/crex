#include "types.h"
const size_t n_patterns = 2;
const pattern_defn_t pattern_defns[] = {
  {"\\Ahello world!\\z", 16},
  {"\\A(hello|goodbye) world!\\z", 26}
};
const size_t n_cases = 6;
const testcase_t testcases[] = {
  {0, "hello world!", 12, 0, {0, 0}},
  {0, "goodbye world!", 14, 0, {0, 0}},
  {1, "hello world!", 12, 1, {{0, 12}, {0, 5}}},
  {1, "goodbye world!", 14, 1, {{0, 14}, {0, 7}}},
  {1, "oh, hello world!", 16, 0, {0, 0}},
  {1, "goodbye world! see ya!", 22, 0, {0, 0}}
};
