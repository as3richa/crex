#ifndef LEXER_H
#define LEXER_H

typedef enum {
  TT_CHARACTER,
  TT_CHAR_CLASS,
  TT_BUILTIN_CHAR_CLASS,
  TT_ANCHOR,
  TT_PIPE,
  TT_GREEDY_REPETITION,
  TT_LAZY_REPETITION,
  TT_OPEN_PAREN,
  TT_NON_CAPTURING_OPEN_PAREN,
  TT_CLOSE_PAREN
} token_type_t;

typedef enum {
  AT_BOF,
  AT_BOL,
  AT_EOF,
  AT_EOL,
  AT_WORD_BOUNDARY,
  AT_NOT_WORD_BOUNDARY
} anchor_type_t;

typedef struct {
  token_type_t type;

  union {
    unsigned char character;

    size_t char_class_index;

    anchor_type_t anchor_type;

    struct {
      size_t lower_bound, upper_bound;
    } repetition;
  } data;
} token_t;

typedef unsigned char char_class_t[32];

typedef struct {
  size_t size;
  size_t capacity;
  char_class_t *buffer;
} char_classes_t;

#define REPETITION_INFINITY SIZE_MAX

WARN_UNUSED_RESULT static status_t lex(char_classes_t *classes,
                                       token_t *token,
                                       const char **pattern,
                                       const char *eof,
                                       const allocator_t *allocator);

#endif
