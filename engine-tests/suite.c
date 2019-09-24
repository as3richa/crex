#undef NDEBUG

#include <assert.h>
#include <stdint.h>
#include <sys/mman.h>

#include "suite.h"

size_t required_suite_size(const suite_header_t *header) {
  size_t size = header->patterns_offset;
  size += sizeof(suite_pattern_t) * header->n_patterns;
  size += sizeof(suite_testcase_t) * header->n_testcases;
  return size;
}

void unpack_suite(suite_t *suite, void *mapping, size_t size) {
  assert(size >= sizeof(suite_header_t));

  const suite_header_t *header = mapping;
  assert(size == required_suite_size(header));

  suite->mapping = mapping;

  suite->n_patterns = header->n_patterns;
  suite->patterns = (suite_pattern_t *)(suite->mapping + header->patterns_offset);

  const size_t patterns_size = sizeof(suite_pattern_t) * header->n_patterns;

  suite->n_testcases = header->n_testcases;
  suite->testcases = (suite_testcase_t *)(suite->mapping + header->patterns_offset + patterns_size);
}

void destroy_unpacked_suite(suite_t *suite) {
  munmap(suite->mapping, required_suite_size((suite_header_t *)suite->mapping));
}

const char *
suite_get_pattern(size_t *size, size_t *n_capturing_groups, const suite_t *suite, size_t index) {
  assert(index < suite->n_patterns);
  const suite_pattern_t *pattern = &suite->patterns[index];

  *size = pattern->size;
  *n_capturing_groups = pattern->n_capturing_groups;

  return suite->mapping + pattern->offset;
}

const char *
suite_get_testcase_str(size_t *pattern_index, size_t *size, const suite_t *suite, size_t index) {
  assert(index < suite->n_testcases);
  const suite_testcase_t *testcase = &suite->testcases[index];

  *pattern_index = testcase->pattern_index;
  *size = testcase->size;

  const char *str = suite->mapping + testcase->str_offset;

  return str;
}

void suite_get_testcase_matches_crex(crex_match_t *matches, const suite_t *suite, size_t index) {
  assert(index < suite->n_testcases);
  const suite_testcase_t *testcase = &suite->testcases[index];
  const char *str = (char *)suite->mapping + testcase->str_offset;

  const size_t n_capturing_groups = suite->patterns[testcase->pattern_index].n_capturing_groups;

  size_t *match = (size_t *)(suite->mapping + testcase->matches_offset);

  for (size_t i = 0; i < n_capturing_groups; i++, match += 2, matches++) {
    assert((match[0] == SIZE_MAX) == (match[1] == SIZE_MAX));

    if (match[0] == SIZE_MAX) {
      matches->begin = NULL;
      matches->end = NULL;
      continue;
    }

    matches->begin = str + match[0];
    matches->end = str + match[1];
  }
}

size_t *suite_get_testcase_matches_pcre(const suite_t *suite, size_t index) {
  assert(index < suite->n_testcases);
  const suite_testcase_t *testcase = &suite->testcases[index];
  return (size_t *)(suite->mapping + testcase->matches_offset);
}
