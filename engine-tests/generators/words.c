#include <stdio.h>
#include <string.h>

#include "../suite-builder.h"

#define N_WORDS 60

const char *words[] = {"bookery",
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

  for (size_t i = 0; i < N_WORDS; i++) {
    char pattern[32];
    sprintf(pattern, "\\A%s\\z", words[i]);

    emit_pattern_str(suite, pattern, 1);

    for (size_t j = 0; j < N_WORDS; j++) {
      const size_t size = strlen(words[j]);
      emit_testcase_str(suite, words[j], (i == j) ? SPAN(0, size) : UNMATCHED);
    }
  }

  unsigned char contains[N_WORDS][N_WORDS];

  for (size_t i = 0; i < N_WORDS; i++) {
    const size_t size = strlen(words[i]);

    for (size_t j = 0; j < N_WORDS; j++) {
      const size_t other_size = strlen(words[j]);

      if (size < other_size) {
        contains[i][j] = 0;
        continue;
      }

      contains[i][j] = memcmp(words[i], words[j], other_size) == 0;
    }
  }

  for (size_t i = 0; i < N_WORDS; i++) {
    emit_pattern_str(suite, words[i], 1);

    const size_t size = strlen(words[i]);

    for (size_t j = 0; j < N_WORDS; j++) {
      for (size_t k = 0; k < N_WORDS; k++) {
        char str[64];
        sprintf(str, "%s %s", words[j], words[k]);

        if (contains[j][i]) {
          emit_testcase_str(suite, str, SPAN(0, size));
          continue;
        }

        if (contains[k][i]) {
          const size_t begin = strlen(words[j]) + 1;
          emit_testcase_str(suite, str, SPAN(begin, begin + size));
          continue;
        }

        // emit_testcase_str(suite, str, UNMATCHED);
      }
    }
  }

  finalize_test_suite(suite);

  return 0;
}
