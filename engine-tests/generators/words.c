#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../suite-builder.h"

#define N_CASES 100000

#define N_WORDS 60

const char *words[N_WORDS] = {"bookery",
                              "bookfair",
                              "bookfold",
                              "bookful",
                              "bookfuls",
                              "bookholder",
                              "bookhood",
                              "bookie",
                              "bookies",
                              "bookiness",
                              "gastralgia",
                              "gastralgic",
                              "gastralgy",
                              "gastraneuria",
                              "gastrasthenia",
                              "gastratrophia",
                              "gastrea",
                              "gastreas",
                              "gastrectasia",
                              "gastrectasis",
                              "lamentational",
                              "lamentations",
                              "lamentatory",
                              "lamented",
                              "lamentedly",
                              "lamenter",
                              "lamenters",
                              "lamentful",
                              "lamenting",
                              "lamentingly",
                              "overinsolent",
                              "overinsolently",
                              "overinstruct",
                              "overinstruction",
                              "overinstructive",
                              "overinstructively",
                              "overinstructiveness",
                              "overinsurance",
                              "overinsure",
                              "overinsured",
                              "sexangular",
                              "sexangularly",
                              "sexannulate",
                              "sexarticulate",
                              "sexavalent",
                              "sexcentenaries",
                              "sexcentenary",
                              "sexcuspidate",
                              "sexdecillion",
                              "sexdecillions",
                              "tacketed",
                              "tackets",
                              "tackety",
                              "tackey",
                              "tackier",
                              "tackies",
                              "tackiest",
                              "tackified",
                              "tackifier",
                              "tackifies"};

int main(int argc, char **argv) {
  suite_builder_t *suite = create_test_suite_argv(argc, argv);

  size_t n_cases = 0;

  for (size_t i = 0; i < N_WORDS && n_cases < N_CASES; i++) {
    char pattern[32];
    sprintf(pattern, "\\A%s\\z", words[i]);

    emit_pattern_str(suite, pattern, 1);

    for (size_t j = 0; j < N_WORDS && n_cases < N_CASES; j++) {
      const size_t size = strlen(words[j]);
      emit_testcase_str(suite, words[j], (i == j) ? SPAN(0, size) : UNMATCHED);
      n_cases++;
    }
  }

  unsigned char has_prefix[N_WORDS][N_WORDS];

  for (size_t i = 0; i < N_WORDS; i++) {
    const size_t size = strlen(words[i]);

    for (size_t j = 0; j < N_WORDS; j++) {
      const size_t other_size = strlen(words[j]);

      if (size < other_size) {
        has_prefix[i][j] = 0;
        continue;
      }

      has_prefix[i][j] = memcmp(words[i], words[j], other_size) == 0;
    }
  }

  str_builder_t *str = create_str_builder();

  const size_t cases_per_pattern = (N_CASES - n_cases + N_WORDS - 1) / N_WORDS;

  for (size_t i = 0; i < N_WORDS && n_cases < N_CASES; i++) {
    emit_pattern_str(suite, words[i], 1);

    for (size_t j = 0; j < cases_per_pattern && n_cases < N_CASES; j++) {
      size_t begin = SIZE_MAX;

      const size_t size_in_words = rand() % N_WORDS;

      sb_clear(str);

      for (size_t k = 0; k < size_in_words; k++) {
        if (k != 0) {
          sb_putchar(str, ' ');
        }

        const size_t word_index = rand() % N_WORDS;

        if (begin == SIZE_MAX && has_prefix[word_index][i]) {
          begin = sb_size(str);
        }

        sb_strcat(str, words[word_index]);
      }

      if (begin == SIZE_MAX) {
        emit_testcase_sb(suite, str, UNMATCHED);
      } else {
        const size_t end = begin + strlen(words[i]);
        emit_testcase_sb(suite, str, SPAN(begin, end));
      }

      n_cases++;
    }
  }

  finalize_test_suite(suite);
  return 0;
}
