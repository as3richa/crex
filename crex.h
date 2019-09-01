#ifndef CREX_H
#define CREX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define CREX_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define CREX_WARN_UNUSED_RESULT
#endif

typedef enum {
  CREX_OK,
  CREX_E_NOMEM,
  CREX_E_BAD_ESCAPE,
  CREX_E_BAD_CHARACTER_CLASS,
  CREX_E_BAD_REPETITION,
  CREX_E_UNMATCHED_OPEN_PAREN,
  CREX_E_UNMATCHED_CLOSE_PAREN,
  CREX_E_UNKNOWN_VERSION,
  CREX_E_INVALID_BUFFER_SIZE
} crex_status_t;

typedef struct crex_regex crex_regex_t;

typedef struct crex_context crex_context_t;

typedef struct {
  void *context;
  void *(*alloc)(void *, size_t);
  void (*free)(void *, void *);
} crex_allocator_t;

typedef struct {
  const char *begin;
  const char *end;
} crex_match_t;

CREX_WARN_UNUSED_RESULT crex_regex_t *
crex_compile(crex_status_t *status, const char *pattern, size_t size);

CREX_WARN_UNUSED_RESULT crex_regex_t *crex_compile_str(crex_status_t *status, const char *pattern);

CREX_WARN_UNUSED_RESULT crex_regex_t *crex_compile_with_allocator(
    crex_status_t *status, const char *pattern, size_t size, const crex_allocator_t *allocator);

CREX_WARN_UNUSED_RESULT crex_context_t *crex_create_context(crex_status_t *status);

CREX_WARN_UNUSED_RESULT crex_context_t *
crex_create_context_with_allocator(crex_status_t *status, const crex_allocator_t *allocator);

CREX_WARN_UNUSED_RESULT size_t crex_regex_n_capturing_groups(const crex_regex_t *regex);

void crex_destroy_regex(crex_regex_t *regex);

void crex_destroy_context(crex_context_t *context);

CREX_WARN_UNUSED_RESULT crex_status_t crex_is_match(int *is_match,
                                                    crex_context_t *context,
                                                    const crex_regex_t *regex,
                                                    const char *str,
                                                    size_t size);

CREX_WARN_UNUSED_RESULT crex_status_t crex_is_match_str(int *is_match,
                                                        crex_context_t *context,
                                                        const crex_regex_t *regex,
                                                        const char *str);

CREX_WARN_UNUSED_RESULT crex_status_t crex_find(crex_match_t *match,
                                                crex_context_t *context,
                                                const crex_regex_t *regex,
                                                const char *str,
                                                size_t size);

CREX_WARN_UNUSED_RESULT crex_status_t crex_find_str(crex_match_t *match,
                                                    crex_context_t *context,
                                                    const crex_regex_t *regex,
                                                    const char *str);

CREX_WARN_UNUSED_RESULT crex_status_t crex_match_groups(crex_match_t *matches,
                                                        crex_context_t *context,
                                                        const crex_regex_t *regex,
                                                        const char *str,
                                                        size_t size);

CREX_WARN_UNUSED_RESULT crex_status_t crex_match_groups_str(crex_match_t *matches,
                                                            crex_context_t *context,
                                                            const crex_regex_t *regex,
                                                            const char *str);

CREX_WARN_UNUSED_RESULT crex_status_t crex_context_reserve_is_match(crex_context_t *context,
                                                                    const crex_regex_t *regex);

CREX_WARN_UNUSED_RESULT crex_status_t crex_context_reserve_find(crex_context_t *context,
                                                                const crex_regex_t *regex);

CREX_WARN_UNUSED_RESULT crex_status_t crex_context_reserve_match_groups(crex_context_t *context,
                                                                        const crex_regex_t *regex);

CREX_WARN_UNUSED_RESULT unsigned char *
crex_dump_regex(crex_status_t *status, size_t *size, const crex_regex_t *regex);

CREX_WARN_UNUSED_RESULT unsigned char *
crex_dump_regex_with_allocator(crex_status_t *status,
                               size_t *size,
                               const crex_regex_t *regex,
                               const crex_allocator_t *allocator);

CREX_WARN_UNUSED_RESULT crex_regex_t *
crex_load_regex(crex_status_t *status, unsigned char *buffer, size_t size);

CREX_WARN_UNUSED_RESULT crex_regex_t *crex_load_regex_with_allocator(
    crex_status_t *status, unsigned char *buffer, size_t size, const crex_allocator_t *allocator);

#ifdef __cplusplus
}
#endif

#endif
