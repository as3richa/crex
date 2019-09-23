#ifndef SUITE_H
#define SUITE_H

#include <stddef.h>

#include "crex.h"

// A suite is encoded as:
// - a suite_header_t
// - a blob of binary data, spanning up to patterns_offset, containing pattern, string, and match
// data
// - header.n_patterns suite_pattern_t's
// - header.n_testcases suite_test_t's

// Testcase match data is encoded as a sequence of size_t pairs, each corresponding to a subgroup,
// with {SIZE_MAX, SIZE_MAX} representing to an unmatched subgroup

typedef struct {
  size_t patterns_offset;
  size_t n_patterns;
  size_t n_testcases;
} suite_header_t;

typedef struct {
  size_t size;
  size_t n_capturing_groups;
  size_t offset;
} suite_pattern_t;

typedef struct {
  size_t pattern_index;
  size_t size;
  size_t str_offset;
  size_t matches_offset;
} suite_testcase_t;

typedef struct {
  char *mapping;

  size_t n_patterns;
  suite_pattern_t *patterns;

  size_t n_testcases;
  suite_testcase_t *testcases;
} suite_t;

size_t required_suite_size(const suite_header_t *header);

void unpack_suite(suite_t *suite, void *mapping, size_t size);
void destroy_unpacked_suite(suite_t *suite);

const char *
suite_get_pattern(size_t *size, size_t *n_capturing_groups, const suite_t *suite, size_t index);

const char *
suite_get_testcase_str(size_t *pattern_index, size_t *size, const suite_t *suite, size_t index);

void suite_get_testcase_matches(crex_match_t *matches, const suite_t *suite, size_t index);

#endif
