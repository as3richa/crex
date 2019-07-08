#ifndef CREX_H
#define CREX_H

enum {
  CREX_OK,
  CREX_E_NOMEM
};

#ifdef CREX_DEBUG

void crex_debug_lex(const char* str, size_t length);
void crex_debug_parse(const char* str, size_t length);

#endif

#endif
