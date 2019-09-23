#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../execution-engine.h"
#include "../harness.h"
#include "../suite.h"

extern const execution_engine_t ex_default;
extern const execution_engine_t ex_alloc_hygiene;

#define N_ENGINES 2

static const execution_engine_t *all_engines[N_ENGINES] = {&ex_default, &ex_alloc_hygiene};

static char *default_engine_name = "default";

static size_t execute_suite_with_engine(const execution_engine_t *engine,
                                        void *self,
                                        suite_t *suite,
                                        allocator_t *allocator);

static void usage(char **argv);

int main(int argc, char **argv) {
  size_t n_engines;
  char **engine_names = argv + 1;

  for (n_engines = 0; n_engines < (size_t)argc - 1; n_engines++) {
    if (strcmp(engine_names[n_engines], "--") == 0) {
      break;
    }
  }

  if (n_engines == (size_t)argc - 1) {
    usage(argv);
    return EXIT_FAILURE;
  }

  char **suite_paths = argv + 1 + n_engines + 1;
  const size_t n_suites = (argv + argc) - suite_paths;

  if (n_suites == 0) {
    usage(argv);
    return EXIT_FAILURE;
  }

  if (n_engines == 0) {
    n_engines = 1;
    engine_names = &default_engine_name;
  }

  const execution_engine_t **engines = malloc(sizeof(execution_engine_t *) * n_engines);
  assert(engines);

  for (size_t i = 0; i < n_engines; i++) {
    engines[i] = NULL;

    for (size_t j = 0; j < N_ENGINES; j++) {
      if (strcmp(engine_names[i], all_engines[j]->name) == 0) {
        engines[i] = all_engines[j];
        break;
      }
    }

    if (engines[i] == NULL) {
      usage(argv);
      return EXIT_FAILURE;
    }
  }

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
    reset_allocator(&allocator, engine->allocator_type, 0);

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

  destroy_suites(suites, n_suites);

  free(engines);

  return ok ? 0 : EXIT_FAILURE;
}

static void usage(char **argv) {
  fprintf(stderr,
          "usage: %s [ex. engine] [...] -- <suite> [...]\navailable execution engines:\n",
          argv[0]);

  for (size_t i = 0; i < N_ENGINES; i++) {
    fprintf(stderr, "  %s\n", all_engines[i]->name);
  }
}

static size_t execute_suite_with_engine(const execution_engine_t *engine,
                                        void *self,
                                        suite_t *suite,
                                        allocator_t *allocator) {
  size_t testcases_passed = 0;

  crex_match_t *matches = NULL;
  crex_match_t *expected_matches = NULL;
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
        max_groups = n_capturing_groups;
        matches = realloc(matches, 2 * sizeof(crex_match_t) * max_groups);
        expected_matches = matches + max_groups;
      }

      prev_pattern_index = pattern_index;
    }

    if (regex == NULL) {
      continue;
    }

    suite_get_testcase_matches(expected_matches, suite, i);

    int ok = engine->run(self, matches, regex, str, size, allocator);
    ok &= memcmp(expected_matches, matches, sizeof(crex_match_t) * n_capturing_groups) == 0;

    testcases_passed += ok;
  }

  if (regex != NULL) {
    engine->destroy_regex(self, regex, allocator);
  }

  free(matches);

  return testcases_passed;
}
