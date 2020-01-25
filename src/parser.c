#include "parser.h"

// Operators in descending order of precedence, followed by groups. This simplifies the
// implementation slightly
typedef enum { OP_REPETITION, OP_CONCATENATION, OP_ALTERNATION, OP_GROUP } operator_type_t;

typedef struct {
  // Save a few bytes by allowing type and data.repetition.lazy to fit in the same word. It
  // simplifies the implementation if we allow greedy and lazy repetitions to have the same
  // operation type, hence the need for a flag
  operator_type_t type : 2;

  union {
    struct {
      unsigned int lazy : 1;
      size_t lower_bound;
      size_t upper_bound;
    } repetition;

    struct {
      size_t index;
    } group;
  } data;
} operator_t;

#define NAME operator_stack
#define CONTAINED_TYPE operator_t
#define STACK_CAPACITY 32
#include "vector.c"

#define NAME parsetree_stack
#define CONTAINED_TYPE parsetree_t *
#define STACK_CAPACITY 32
#include "vector.c"

WUR static int parser_push_operator(operator_stack_t *operators,
                                    parsetree_stack_t *trees,
                                    operator_t *op,
                                    const allocator_t *allocator);

WUR static int parser_pop_operator(operator_stack_t *operators,
                                   parsetree_stack_t *trees,
                                   const allocator_t *allocator);

WUR static int parser_push_empty(parsetree_stack_t *trees, const allocator_t *allocator);

WUR static parsetree_t *parse(status_t *status,
                              size_t *n_capturing_groups,
                              char_classes_t *classes,
                              const char *str,
                              size_t size,
                              const allocator_t *allocator) {
  const char *eof = str + size;

  // Reserve capturing group 0; we wrap the entire parsetree in a group at the end
  *n_capturing_groups = 1;

  operator_stack_t operators;
  create_operator_stack(&operators);

  parsetree_stack_t trees;
  create_parsetree_stack(&trees);

#define DIE(code)                                                                                  \
  do {                                                                                             \
    destroy_operator_stack(&operators, allocator);                                                 \
    for (size_t i = 0; i < trees.size; i++) {                                                      \
      destroy_parsetree(parsetree_stack_at(&trees, i), allocator);                                 \
    }                                                                                              \
    destroy_parsetree_stack(&trees, allocator);                                                    \
    *status = code;                                                                                \
    return NULL;                                                                                   \
  } while (0)

  if (!parser_push_empty(&trees, allocator)) {
    DIE(CREX_E_NOMEM);
  }

  while (str != eof) {
    token_t token;
    const status_t lex_status = lex(classes, &token, &str, eof, allocator);

    if (lex_status != CREX_OK) {
      DIE(lex_status);
    }

    switch (token.type) {
    case TT_CHARACTER:
    case TT_CHAR_CLASS:
    case TT_BUILTIN_CHAR_CLASS:
    case TT_ANCHOR: {
      // Every scalar token is implicitly preceded by a concatenation operation

      operator_t op;
      op.type = OP_CONCATENATION;

      if (!parser_push_operator(&operators, &trees, &op, allocator)) {
        DIE(CREX_E_NOMEM);
      }

      parsetree_t *tree = ALLOC(allocator, sizeof(parsetree_t));

      if (tree == NULL) {
        DIE(CREX_E_NOMEM);
      }

      switch (token.type) {
      case TT_CHARACTER:
        tree->type = PT_CHARACTER;
        tree->data.character = token.data.character;
        break;

      case TT_CHAR_CLASS:
        tree->type = PT_CHAR_CLASS;
        tree->data.char_class_index = token.data.char_class_index;
        break;

      case TT_BUILTIN_CHAR_CLASS:
        tree->type = PT_BUILTIN_CHAR_CLASS;
        tree->data.char_class_index = token.data.char_class_index;
        break;

      case TT_ANCHOR:
        tree->type = PT_ANCHOR;
        tree->data.anchor_type = token.data.anchor_type;
        break;

      default:
        UNREACHABLE();
      }

      tree->next = NULL;

      if (!parsetree_stack_push(&trees, tree, allocator)) {
        destroy_parsetree(tree, allocator);
        DIE(CREX_E_NOMEM);
      }

      break;
    }

    case TT_PIPE: {
      operator_t op;
      op.type = OP_ALTERNATION;

      if (!parser_push_operator(&operators, &trees, &op, allocator)) {
        DIE(CREX_E_NOMEM);
      }

      if (!parser_push_empty(&trees, allocator)) {
        DIE(CREX_E_NOMEM);
      }

      break;
    }

    case TT_GREEDY_REPETITION:
    case TT_LAZY_REPETITION: {
      operator_t op;
      op.type = OP_REPETITION;
      op.data.repetition.lower_bound = token.data.repetition.lower_bound;
      op.data.repetition.upper_bound = token.data.repetition.upper_bound;
      op.data.repetition.lazy = token.type == TT_LAZY_REPETITION;

      if (!parser_push_operator(&operators, &trees, &op, allocator)) {
        DIE(CREX_E_NOMEM);
      }

      break;
    }

    case TT_OPEN_PAREN:
    case TT_NON_CAPTURING_OPEN_PAREN: {
      // Groups are also preceded by an implicit concatenation

      operator_t op;
      op.type = OP_CONCATENATION;

      if (!parser_push_operator(&operators, &trees, &op, allocator)) {
        DIE(CREX_E_NOMEM);
      }

      op.type = OP_GROUP;

      if (token.type == TT_OPEN_PAREN) {
        op.data.group.index = (*n_capturing_groups)++;
      } else {
        op.data.group.index = SIZE_MAX;
      }

      if (!operator_stack_push(&operators, op, allocator)) {
        DIE(CREX_E_NOMEM);
      }

      if (!parser_push_empty(&trees, allocator)) {
        DIE(CREX_E_NOMEM);
      }

      break;
    }

    case TT_CLOSE_PAREN: {
      while (!operator_stack_empty(&operators)) {
        if (operator_stack_top(&operators)->type == OP_GROUP) {
          break;
        }

        if (!parser_pop_operator(&operators, &trees, allocator)) {
          DIE(CREX_E_NOMEM);
        }
      }

      if (operators.size == 0) {
        DIE(CREX_E_UNMATCHED_CLOSE_PAREN);
      }

      if (!parser_pop_operator(&operators, &trees, allocator)) {
        DIE(CREX_E_NOMEM);
      }

      break;
    }

    default:
      UNREACHABLE();
    }
  }

  while (!operator_stack_empty(&operators)) {
    if (operator_stack_top(&operators)->type == OP_GROUP) {
      DIE(CREX_E_UNMATCHED_OPEN_PAREN);
    }

    if (!parser_pop_operator(&operators, &trees, allocator)) {
      DIE(CREX_E_NOMEM);
    }
  }

  assert(trees.size == 1);

  parsetree_t *tree = ALLOC(allocator, sizeof(parsetree_t));

  if (tree == NULL) {
    DIE(CREX_E_NOMEM);
  }

  tree->type = PT_GROUP;
  tree->data.group.index = 0;
  tree->data.group.child = parsetree_stack_at(&trees, 0);
  tree->next = NULL;

  destroy_operator_stack(&operators, allocator);
  destroy_parsetree_stack(&trees, allocator);

  return tree;

#undef DIE
}

static void destroy_parsetree(parsetree_t *tree, const allocator_t *allocator) {
  for (;;) {
    switch (tree->type) {
    case PT_EMPTY:
    case PT_CHARACTER:
    case PT_CHAR_CLASS:
    case PT_BUILTIN_CHAR_CLASS:
    case PT_ANCHOR: {
      FREE(allocator, tree);
      return;
    }

    case PT_ALTERNATION: {
      // Destroy one subtree recursively
      destroy_parsetree(tree->data.alternation.left, allocator);

      // Destroy the other iteratively
      parsetree_t *child = tree->data.alternation.right;
      FREE(allocator, tree);
      tree = child;
      break;
    }

    case PT_GREEDY_REPETITION:
    case PT_LAZY_REPETITION: {
      parsetree_t *child = tree->data.repetition.child;
      FREE(allocator, tree);
      tree = child;
      break;
    }

    case PT_GROUP: {
      parsetree_t *child = tree->data.group.child;
      FREE(allocator, tree);
      tree = child;
      break;
    }

    default:
      UNREACHABLE();
    }
  }
}

WUR static int parser_push_empty(parsetree_stack_t *trees, const allocator_t *allocator) {
  parsetree_t *tree = ALLOC(allocator, sizeof(parsetree_t));

  if (tree == NULL) {
    return 0;
  }

  tree->type = PT_EMPTY;
  tree->next = NULL;

  if (!parsetree_stack_push(trees, tree, allocator)) {
    destroy_parsetree(tree, allocator);
    return 0;
  }

  return 1;
}

WUR static int parser_push_operator(operator_stack_t *operators,
                                    parsetree_stack_t *trees,
                                    operator_t *op,
                                    const allocator_t *allocator) {
  assert(op->type != OP_GROUP);

  for (;;) {
    if (operator_stack_empty(operators)) {
      break;
    }

    if (operator_stack_top(operators)->type > op->type) {
      // The topmost operator is either an operator of lesser precedence, or a group
      break;
    }

    if (!parser_pop_operator(operators, trees, allocator)) {
      return 0;
    }
  }

  return operator_stack_push(operators, *op, allocator);
}

WUR static int parser_pop_operator(operator_stack_t *operators,
                                   parsetree_stack_t *trees,
                                   const allocator_t *allocator) {
  const operator_t op = operator_stack_pop(operators);

  if (op.type == OP_CONCATENATION) {
    assert(trees->size >= 2);
    parsetree_t *next = parsetree_stack_pop(trees);

    // FIXME: ugh

    parsetree_t *tree;

    for(tree = *parsetree_stack_top(trees); tree->next != NULL; tree = tree->next) {
    }

    tree->next = next;

    return 1;
  }

  parsetree_t *tree = ALLOC(allocator, sizeof(parsetree_t));

  if (tree == NULL) {
    return 0;
  }

  switch (op.type) {
  case OP_ALTERNATION: {
    assert(trees->size >= 2);
    tree->type = PT_ALTERNATION;
    tree->data.alternation.right = parsetree_stack_pop(trees);
    tree->data.alternation.left = parsetree_stack_pop(trees);
    break;
  }

  case OP_REPETITION: {
    assert(trees->size >= 1);
    tree->type = op.data.repetition.lazy ? PT_LAZY_REPETITION : PT_GREEDY_REPETITION;
    tree->data.repetition.lower_bound = op.data.repetition.lower_bound;
    tree->data.repetition.upper_bound = op.data.repetition.upper_bound;
    tree->data.repetition.child = parsetree_stack_pop(trees);
    break;
  }

  case OP_GROUP: {
    assert(trees->size >= 1);
    tree->type = PT_GROUP;
    tree->data.group.index = op.data.group.index;
    tree->data.group.child = parsetree_stack_pop(trees);
    break;
  }

  default:
    UNREACHABLE();
  }

  tree->next = NULL;

  if (!parsetree_stack_push(trees, tree, allocator)) {
    destroy_parsetree(tree, allocator);
    return 0;
  }

  return 1;
}
