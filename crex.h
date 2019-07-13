#ifndef CREX_H
#define CREX_H

typedef enum {
  CREX_OK,
  CREX_E_NOMEM,
  CREX_E_BAD_ESCAPE,
  CREX_E_UNMATCHED_OPEN_PAREN,
  CREX_E_UNMATCHED_CLOSE_PAREN
} crex_status_t;

#endif
