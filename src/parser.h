#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

typedef enum {
  PT_ALTERNATION,
  PT_GREEDY_REPETITION,
  PT_LAZY_REPETITION,
  PT_GROUP,
  PT_EMPTY,
  PT_CHARACTER,
  PT_CHAR_CLASS,
  PT_BUILTIN_CHAR_CLASS,
  PT_ANCHOR
} parsetree_type_t;

typedef struct parsetree {
  parsetree_type_t type;

  union {
    unsigned char character;

    size_t char_class_index;

    anchor_type_t anchor_type;

    struct {
      struct parsetree *left;
      struct parsetree *right;
    } alternation;

    struct {
      size_t lower_bound;
      size_t upper_bound;
      struct parsetree *child;
    } repetition;

    struct {
      size_t index;
      struct parsetree *child;
    } group;
  } data;

  struct parsetree *next;
} parsetree_t;

WUR static parsetree_t *parse(status_t *status,
                              size_t *n_capturing_groups,
                              char_classes_t *classes,
                              const char *str,
                              size_t size,
                              const allocator_t *allocator);

static void destroy_parsetree(parsetree_t *tree, const allocator_t *allocator);

#endif
