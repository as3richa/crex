#ifndef CREX_H
#define CREX_H

typedef enum {
  CREX_OK,
  CREX_E_NOMEM,
  CREX_E_BAD_ESCAPE,
  CREX_E_UNMATCHED_OPEN_PAREN,
  CREX_E_UNMATCHED_CLOSE_PAREN
} crex_status_t;

typedef struct {
  size_t size;
  unsigned char *bytecode;
} crex_regex_t;

typedef struct {
  int matched;
} crex_match_result_t;

crex_status_t crex_compile(crex_regex_t *regex, const char *pattern, size_t length);
crex_status_t crex_compile_str(crex_regex_t *regex, const char *pattern);
void crex_free_regex(crex_regex_t *regex);

crex_status_t crex_match(crex_match_result_t *result,
                         const crex_regex_t *regex,
                         const char *buffer,
                         size_t length);
crex_status_t
crex_match_str(crex_match_result_t *result, const crex_regex_t *regex, const char *str);

#endif
