#include <stdlib.h>
#include <string.h>

#include "../str-builder.h"
#include "../suite-builder.h"

#define N_CASES 50000

const char *schemes[] = {"arbe",
                         "arbela-gaugamela",
                         "baltimore",
                         "baltimorite",
                         "belsky",
                         "belshin",
                         "demicaponier",
                         "demicanton",
                         "enure",
                         "enures",
                         "ethicophysical",
                         "ethicopolitical",
                         "goofier",
                         "goofily",
                         "granniah",
                         "grannies",
                         "indusiate",
                         "indusial",
                         "long-resounding",
                         "long-ribbed",
                         "maxatawny",
                         "maxama",
                         "melodramatize",
                         "melodye",
                         "orientalised",
                         "orientalisation",
                         "paedomorphism",
                         "paedometrical",
                         "pandect",
                         "pandean",
                         "pid",
                         "picunche",
                         "pissarro",
                         "pissabed",
                         "sabella",
                         "sabeing",
                         "sall",
                         "salle",
                         "sonia",
                         "soni",
                         "supersweetly",
                         "supersuspiciously",
                         "tire-filling",
                         "tire-changing",
                         "tritoconid",
                         "tritoma",
                         "undersold",
                         "undersole",
                         "who-does-what",
                         "who-whoop"};

const char *bad_schemes[] = {"arbalos",        "arbela",          "baltic",
                             "baltimorean",    "belshazzaresque", "belsire",
                             "demichamfron",   "demicircle",      "enunciatory",
                             "enured",         "ethicoreligious", "ethicoaesthetic",
                             "goofiness",      "goofiest",        "grannias",
                             "grannie",        "indusia",         "indus",
                             "long-reaching",  "long-range",      "maxantia",
                             "maxa",           "melodrame",       "melody",
                             "orientalising",  "orientalise",     "paedomorphosis",
                             "paedomorphic",   "pandavas",        "pandava",
                             "picuris",        "picus",           "pissant",
                             "pissants",       "sabellan",        "sabed",
                             "sallee",         "salkum",          "sonhoods",
                             "sonhood",        "supersweet",      "supersuspiciousness",
                             "tire-inflating", "tire-heating",    "tritolo",
                             "tritogeneia",    "undersoil",       "undersomething",
                             "who're",         "who've"};

const char *tlds[] = {"anenterous",
                      "anent",
                      "anticensorious",
                      "anticensoriously",
                      "arylamine",
                      "aryl",
                      "cabiria",
                      "cabiri",
                      "capitation",
                      "capitations",
                      "chubby",
                      "chubsucker",
                      "consonantise",
                      "consonantally",
                      "coprolite",
                      "coprology",
                      "demulcents",
                      "demulcent",
                      "disreputable",
                      "disreputability",
                      "epirogenetic",
                      "epipubis",
                      "frying",
                      "fryer",
                      "heild",
                      "heiled",
                      "intenser",
                      "intense",
                      "intermediateness",
                      "intermediates",
                      "lilac-purple",
                      "lilac-mauve",
                      "mau",
                      "matzot",
                      "misgracious",
                      "misgoverns",
                      "mlechchha",
                      "mler",
                      "prepurchased",
                      "prepupal",
                      "pulvilli",
                      "pulvilliform",
                      "reexportation",
                      "reexported",
                      "soave",
                      "soarings",
                      "steep-backed",
                      "steenth",
                      "thallic",
                      "thalli"};

const char *bad_tlds[] = {"anepigraphic",
                          "anepia",
                          "anticensoriousness",
                          "anticensorship",
                          "arylamino",
                          "aryepiglottidean",
                          "cabio",
                          "cabirean",
                          "capitatim",
                          "capitative",
                          "chubs",
                          "chubby-faced",
                          "consonantic",
                          "consonantalizing",
                          "coprolith",
                          "coprolitic",
                          "demulsibility",
                          "demulceate",
                          "disreputableness",
                          "disreputably",
                          "epirhizous",
                          "epipubic",
                          "frying-pan",
                          "fryers",
                          "heil",
                          "heilbronn",
                          "intenseness",
                          "intensely",
                          "intermediately",
                          "intermediated",
                          "lilac-pink",
                          "lilac-headed",
                          "matzos",
                          "matzoth",
                          "misgrade",
                          "misgovernor",
                          "mlf",
                          "mlem",
                          "prepurchase",
                          "prepurchaser",
                          "pulvillar",
                          "pulvillus",
                          "reexporter",
                          "reexporting",
                          "soary",
                          "soars",
                          "steep-ascending",
                          "steep",
                          "thall-",
                          "thalidomide"};

#define STR_ARRAY_SIZE(ary) (sizeof(ary) / sizeof(const char *))

#define N_SCHEMES STR_ARRAY_SIZE(schemes)
#define N_BAD_SCHEMES STR_ARRAY_SIZE(bad_schemes)
#define N_TLDS STR_ARRAY_SIZE(tlds)
#define N_BAD_TLDS STR_ARRAY_SIZE(bad_tlds)

const char *alnum_dash = "abcdefghjiklmnopqrstuvwxyz0123456789-";
const char *alnum_dash_slash = "abcdefghjiklmnopqrstuvwxyz0123456789-/";

str_builder_t *or_pattern(const char **strs, size_t n_strs);

int main(int argc, char **argv) {
  suite_builder_t *suite = create_test_suite_argv(argc, argv);

  str_builder_t *scheme_pattern = or_pattern(schemes, N_SCHEMES);
  str_builder_t *tld_pattern = or_pattern(tlds, N_TLDS);

  const char *template =
      "\\A(%s):(?://"
      ")?(?:([a-z0-9-]*(?::[a-z0-9-]*))@)?((?:[a-z0-9-]+\\.)+(%s))(?::([1-9][0-9]*))?(/[a-z0-9/"
      "-]+)?\\z";

  str_builder_t *pattern = create_str_builder();
  sb_cat_sprintf(pattern, template, sb2str(scheme_pattern), sb2str(tld_pattern));

  destroy_str_builder(scheme_pattern);
  destroy_str_builder(tld_pattern);

  emit_pattern_sb(suite, pattern, 7);

  destroy_str_builder(pattern);

  for (size_t i = 0; i < N_CASES; i++) {
    str_builder_t *str = create_str_builder();
    int should_match = 1;

    const char *scheme;

    if (rand() % 4 == 0) {
      scheme = bad_schemes[rand() % N_BAD_SCHEMES];
      should_match = 0;
    } else {
      scheme = schemes[rand() % N_SCHEMES];
    }

    sb_strcat(str, scheme);

    sb_putchar(str, ':');

    if (rand() % 2 == 0) {
      sb_strcat(str, "//");
    }

    size_t auth_begin;
    size_t auth_end;

    if (rand() % 2 == 0) {
      auth_begin = sb_size(str);

      sb_cat_random(str, 0, 20, alnum_dash);
      sb_putchar(str, ':');
      sb_cat_random(str, 0, 20, alnum_dash);

      auth_end = sb_size(str);

      sb_putchar(str, '@');
    } else {
      auth_begin = SIZE_MAX;
      auth_end = SIZE_MAX;
    }

    size_t n_non_top_level_domains = 1; // + rand() % 20;

    const size_t domains_begin = sb_size(str);

    for (size_t j = 0; j < n_non_top_level_domains; j++) {
      switch (rand() % 3) {
      case 0: {
        sb_strcat(str, tlds[rand() % N_TLDS]);
        break;
      }

      case 1: {
        sb_strcat(str, bad_tlds[rand() % N_BAD_TLDS]);
        break;
      }

      default:
        sb_cat_random(str, 1, 30, alnum_dash);
      }

      sb_putchar(str, '.');
    }

    const size_t tld_begin = sb_size(str);

    const char *tld;

    if (rand() % 3 == 0) {
      tld = bad_tlds[rand() % N_BAD_TLDS];
      should_match = 0;
    } else {
      tld = tlds[rand() % N_TLDS];
    }

    sb_strcat(str, tld);

    const size_t domains_end = sb_size(str);

    size_t port_begin;
    size_t port_end;

    if (rand() % 2 == 0) {
      sb_putchar(str, ':');
      port_begin = sb_size(str);
      sb_cat_sprintf(str, "%d", 1 + rand() % 9999);
      port_end = sb_size(str);
    } else {
      port_begin = SIZE_MAX;
      port_end = SIZE_MAX;
    }

    size_t path_begin;
    size_t path_end;

    if (rand() % 2 == 0) {
      path_begin = sb_size(str);
      sb_putchar(str, '/');
      sb_cat_random(str, 1, 5, alnum_dash_slash);
      path_end = sb_size(str);
    } else {
      path_begin = SIZE_MAX;
      path_end = SIZE_MAX;
    }

    if (!should_match) {
      emit_testcase_sb(suite, str, UNMATCHED);
      continue;
    }

    emit_testcase_sb(suite,
                     str,
                     SPAN(0, sb_size(str)),
                     SPAN(0, strlen(scheme)),
                     SPAN(auth_begin, auth_end),
                     SPAN(domains_begin, domains_end),
                     SPAN(tld_begin, domains_end),
                     SPAN(port_begin, port_end),
                     SPAN(path_begin, path_end));
  }

  finalize_test_suite(suite);

  return 0;
}

str_builder_t *or_pattern(const char **strs, size_t n_strs) {
  str_builder_t *pattern = create_str_builder();

  for (size_t i = 0; i < n_strs; i++) {
    if (i != 0) {
      sb_putchar(pattern, '|');
    }

    sb_strcat(pattern, strs[i]);
  }

  return pattern;
}
