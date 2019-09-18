#ifndef HARNESSES_H
#define HARNESSES_H

#include <stdlib.h>

#include "test-harness.h"

extern const test_harness_t crex_correctness_default;
extern const test_harness_t crex_bench_default;
extern const test_harness_t crex_bench_compile_default;

#define HARNESS_MAIN(harness)                                                                      \
  int main(int argc, char **argv) {                                                                \
    return run(argc, argv, &(harness)) ? 0 : EXIT_FAILURE;                                         \
  }

#endif
