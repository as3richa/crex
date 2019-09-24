#define PCRE2_CODE_UNIT_WIDTH 8

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pcre2.h>

#include "../execution-engine.h"
#include "../harness.h"
#include "../suite.h"

extern const execution_engine_t ex_default;
extern const execution_engine_t ex_alloc_hygiene;
extern const execution_engine_t ex_pcre_default;
extern const execution_engine_t ex_pcre_jit;

#define N_ENGINES 4

static const execution_engine_t *all_engines[N_ENGINES] = {
    &ex_default, &ex_alloc_hygiene, &ex_pcre_default, &ex_pcre_jit};

static size_t execute_suite_with_engine(const execution_engine_t *engine,
                                        void *self,
                                        suite_t *suite,
                                        allocator_t *allocator);

static void usage(char **argv);

int main(int argc, char **argv) {
  size_t n_engine_names;
  char **engine_names = argv + 1;

  for (n_engine_names = 0; n_engine_names < (size_t)argc - 1; n_engine_names++) {
    if (strcmp(engine_names[n_engine_names], "--") == 0) {
      break;
    }
  }

  if (n_engine_names + 1 >= (size_t)argc - 1) {
    // No suites given, or missing -- seperator
    usage(argv);
    return EXIT_FAILURE;
  }

  char **suite_paths = argv + 1 + n_engine_names + 1;
  const size_t n_suites = (argv + argc) - suite_paths;

  size_t n_engines = N_ENGINES;
  const execution_engine_t **engines =
      load_engines(&n_engines, all_engines, engine_names, n_engine_names, &ex_default);

  bench_timer_t total_time_timer;
  start_timer(&total_time_timer);

  suite_t *suites = load_suites(suite_paths, n_suites);
  assert(suites);

  int ok = 1;

  for (size_t i = 0; i < n_engines; i++) {
    const execution_engine_t *engine = engines[i];

    bench_timer_t engine_time_timer;
    start_timer(&engine_time_timer);

    allocator_t allocator;

    if (engine->needs_allocator) {
      reset_allocator(&allocator, engine->convention, 0);
    }

    void *self = (engine->create == NULL) ? NULL : engine->create(&allocator);

    size_t suites_passed = 0;
    size_t testcases_passed = 0;
    size_t testcases_run = 0;

    for (size_t j = 0; j < n_suites; j++) {
      const size_t suite_testcases_passed =
          execute_suite_with_engine(engine, self, &suites[j], &allocator);

      const size_t suite_testcases_run = suites[j].n_testcases;

      fprintf(stderr,
              "%s%s: suite %s: %zu/%zu test(s) passed\n",
              (suite_testcases_passed == suite_testcases_run) ? "\x1b[32m" : "\x1b[33m",
              engine->name,
              suite_paths[j],
              suite_testcases_passed,
              suite_testcases_run);

      testcases_passed += suite_testcases_passed;
      testcases_run += suite_testcases_run;
      suites_passed += suite_testcases_passed == suite_testcases_run;
    }

    if (self != NULL) {
      assert(engine->destroy != NULL);
      engine->destroy(self, &allocator);
    }

    const double engine_time = stop_timer(&engine_time_timer);

    fprintf(stderr,
            "%s%s: %zu/%zu test(s), %zu/%zu suite(s) passed in %0.4fs\x1b[0m\n",
            (suites_passed == n_suites) ? "\x1b[32m" : "\x1b[33m",
            engine->name,
            testcases_passed,
            testcases_run,
            suites_passed,
            n_suites,
            engine_time);

    ok &= suites_passed == n_suites;
  }

  const double total_time = stop_timer(&total_time_timer);
  fprintf(stderr, "done in %0.4fs\n", total_time);

  destroy_engines(engines);
  destroy_suites(suites, n_suites);

  return ok ? 0 : EXIT_FAILURE;
}

static void usage(char **argv) {
  const char *fmt = "usage:\n"
                    "  %s <engine> [...] -- <suite> [...]\n"
                    "  %s -- <suite> [...]\n"
                    "  %s all -- <suite> [...]\n"
                    "available engines:\n";

  fprintf(stderr, fmt, argv[0], argv[0], argv[0]);

  for (size_t i = 0; i < N_ENGINES; i++) {
    fprintf(stderr, "  %s\n", all_engines[i]->name);
  }
}

static size_t execute_suite_with_engine(const execution_engine_t *engine,
                                        void *self,
                                        suite_t *suite,
                                        allocator_t *allocator) {
  size_t testcases_passed = 0;

  // If engine->convention == CONVENTION_CREX, matches and expected_matches are arrays of
  // crex_match_t; otherwise, matches is a pcre_match_data and expected_matches is an array of
  // size_t

  void *matches = NULL;
  void *expected_matches = NULL;
  size_t max_groups = 0;

  void *regex = NULL;

  size_t prev_pattern_index = SIZE_MAX;
  size_t n_capturing_groups;

  for (size_t i = 0; i < suite->n_testcases; i++) {
    size_t pattern_index;
    size_t size;

    const char *str = suite_get_testcase_str(&pattern_index, &size, suite, i);

    if (pattern_index != prev_pattern_index) {
      if (regex != NULL) {
        engine->destroy_regex(self, regex, allocator);
      }

      size_t pattern_size;

      const char *pattern =
          suite_get_pattern(&pattern_size, &n_capturing_groups, suite, pattern_index);

      regex = engine->compile_regex(self, pattern, pattern_size, n_capturing_groups, allocator);

      if (n_capturing_groups > max_groups) {
        if (engine->convention == CONVENTION_PCRE) {
          // If the assertion holds, as it does as of this writing, the testcase
          // match format on-disk is actually identical to PCRE's
          // representation. We need not rehydrate the match data, nor allocate
          // space for it
          assert(sizeof(PCRE2_SIZE) == sizeof(size_t) && PCRE2_UNSET == SIZE_MAX);

          pcre2_match_data_free(matches);
          matches = pcre2_match_data_create(n_capturing_groups, NULL);
        } else {
          // Allocate enough for matches and expected_matches in a single allocation
          matches = realloc(matches, sizeof(crex_match_t) * 2 * n_capturing_groups);
          expected_matches = (crex_match_t *)matches + n_capturing_groups;
        }
      }

      prev_pattern_index = pattern_index;
    }

    if (regex == NULL) {
      continue;
    }

    if (engine->convention == CONVENTION_CREX) {
      suite_get_testcase_matches_crex(expected_matches, suite, i);
    } else {
      PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(matches);

      for (size_t i = 0; i < 2 * n_capturing_groups; i++) {
        ovector[i] = PCRE2_UNSET;
      }

      expected_matches = suite_get_testcase_matches_pcre(suite, i);
    }

    int ok = engine->run(self, matches, regex, str, size, allocator);

    if (engine->convention == CONVENTION_CREX) {
      ok &= memcmp(expected_matches, matches, sizeof(crex_match_t) * n_capturing_groups) == 0;
    } else {
      PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(matches);
      ok &= memcmp(expected_matches, ovector, sizeof(size_t) * 2 * n_capturing_groups) == 0;
    }

    testcases_passed += ok;
  }

  if (regex != NULL) {
    engine->destroy_regex(self, regex, allocator);
  }

  if (engine->convention == CONVENTION_CREX) {
    free(matches);
  } else {
    pcre2_match_data_free(matches);
  }

  return testcases_passed;
}
