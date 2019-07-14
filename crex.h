#ifndef CREX_H
#define CREX_H

#if defined(__GNUC__) || defined(__clang__)
#define CREX_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define CREX_WARN_UNUSED_RESULT
#endif

typedef enum {
  CREX_OK,
  CREX_E_NOMEM,
  CREX_E_BAD_ESCAPE,
  CREX_E_UNMATCHED_OPEN_PAREN,
  CREX_E_UNMATCHED_CLOSE_PAREN
} crex_status_t;

/* FIXME: make these opaque */

typedef struct {
  size_t size;
  unsigned char *bytecode;
  size_t n_groups;
} crex_regex_t;

typedef struct {
  size_t size;
  const char *start;
} crex_slice_t;

typedef struct {
  size_t instruction_pointer;
  size_t next;
} crex_ip_list_t;

typedef union {
  char *pointer;
  size_t next;
} pointer_block_t;

typedef struct {
  size_t visited_size;
  unsigned char *visited;

  size_t pointer_block_offsets_size;
  size_t *pointer_block_offsets;

  size_t pointer_block_buffer_size;
  pointer_block_t *pointer_block_buffer;

  size_t list_buffer_size;
  crex_ip_list_t *list_buffer;
} crex_context_t;

CREX_WARN_UNUSED_RESULT crex_status_t crex_compile(crex_regex_t *regex,
                                                   const char *pattern,
                                                   size_t length);

CREX_WARN_UNUSED_RESULT crex_status_t crex_compile_str(crex_regex_t *regex, const char *pattern);

void crex_create_context(crex_context_t *context);

void crex_free_regex(crex_regex_t *regex);

void crex_free_context(crex_context_t *context);

CREX_WARN_UNUSED_RESULT crex_status_t crex_is_match(int *is_match,
                                                    crex_context_t *context,
                                                    const crex_regex_t *regex,
                                                    const char *buffer,
                                                    size_t length);

CREX_WARN_UNUSED_RESULT crex_status_t crex_is_match_str(int *is_match,
                                                        crex_context_t *context,
                                                        const crex_regex_t *regex,
                                                        const char *str);

CREX_WARN_UNUSED_RESULT crex_status_t crex_find(crex_slice_t *match,
                                                crex_context_t *context,
                                                const crex_regex_t *regex,
                                                const char *buffer,
                                                size_t length);

CREX_WARN_UNUSED_RESULT crex_status_t crex_find_str(crex_slice_t *match,
                                                    crex_context_t *context,
                                                    const crex_regex_t *regex,
                                                    const char *str);

CREX_WARN_UNUSED_RESULT crex_status_t crex_match_groups(crex_slice_t *matches,
                                                        crex_context_t *context,
                                                        const crex_regex_t *regex,
                                                        const char *buffer,
                                                        size_t length);

CREX_WARN_UNUSED_RESULT crex_status_t crex_match_groups_str(crex_slice_t *matches,
                                                            crex_context_t *context,
                                                            const crex_regex_t *regex,
                                                            const char *str);

#endif
