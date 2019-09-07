#include "parser.h"

/* FIXME: explain this */
static const size_t operator_precedence[] = {1, 0, 2, 2};

typedef struct {
  parsetree_type_t type;

  union {
    struct {
      size_t lower_bound, upper_bound;
    } repetition;

    size_t group_index;
  } data;
} operator_t;

typedef struct {
  size_t size, capacity;
  parsetree_t **data;
} tree_stack_t;

typedef struct {
  size_t size, capacity;
  operator_t *data;
} operator_stack_t;

WARN_UNUSED_RESULT static int
push_tree(tree_stack_t *trees, parsetree_t *tree, const allocator_t *allocator);

WARN_UNUSED_RESULT static int push_empty(tree_stack_t *trees, const allocator_t *allocator);

static void destroy_tree_stack(tree_stack_t *trees, const allocator_t *allocator);

WARN_UNUSED_RESULT static int push_operator(tree_stack_t *trees,
                                            operator_stack_t *operators,
                                            const operator_t *
                                            operator,
                                            const allocator_t *allocator);

WARN_UNUSED_RESULT static int
pop_operator(tree_stack_t *trees, operator_stack_t *operators, const allocator_t *allocator);

WARN_UNUSED_RESULT static parsetree_t *parse(status_t *status,
                                             size_t *n_capturing_groups,
                                             char_classes_t *classes,
                                             const char *str,
                                             size_t size,
                                             const allocator_t *allocator) {
  const char *eof = str + size;

  tree_stack_t trees = {0, 0, NULL};
  operator_stack_t operators = {0, 0, NULL};

#define CHECK_ERROR(condition, code)                                                               \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      destroy_tree_stack(&trees, allocator);                                                       \
      FREE(allocator, operators.data);                                                             \
      *status = code;                                                                              \
      return NULL;                                                                                 \
    }                                                                                              \
  } while (0)

  {
    operator_t outer_group;
    outer_group.type = PT_GROUP;
    outer_group.data.group_index = 0;

    CHECK_ERROR(push_operator(&trees, &operators, &outer_group, allocator), CREX_E_NOMEM);
    CHECK_ERROR(push_empty(&trees, allocator), CREX_E_NOMEM);
  }

  *n_capturing_groups = 1;

  while (str != eof) {
    token_t token;
    const status_t lex_status = lex(classes, &token, &str, eof, allocator);

    CHECK_ERROR(lex_status == CREX_OK, lex_status);

    switch (token.type) {
    case TT_CHARACTER:
    case TT_CHAR_CLASS:
    case TT_BUILTIN_CHAR_CLASS:
    case TT_ANCHOR: {
      operator_t operator;
      operator.type = PT_CONCATENATION;
      CHECK_ERROR(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);

      parsetree_t *tree = ALLOC(allocator, sizeof(parsetree_t));
      CHECK_ERROR(tree != NULL, CREX_E_NOMEM);

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
        assert(0);
      }

      if (!push_tree(&trees, tree, allocator)) {
        FREE(allocator, tree);
        CHECK_ERROR(0, CREX_E_NOMEM);
      }

      break;
    }

    case TT_PIPE: {
      operator_t operator;
      operator.type = PT_ALTERNATION;
      CHECK_ERROR(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);
      CHECK_ERROR(push_empty(&trees, allocator), CREX_E_NOMEM);
      break;
    }

    case TT_GREEDY_REPETITION:
    case TT_LAZY_REPETITION: {
      operator_t operator;
      operator.type =(token.type == TT_GREEDY_REPETITION) ? PT_GREEDY_REPETITION
                                                          : PT_LAZY_REPETITION;
      operator.data.repetition.lower_bound = token.data.repetition.lower_bound;
      operator.data.repetition.upper_bound = token.data.repetition.upper_bound;
      CHECK_ERROR(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);
      break;
    }

    case TT_OPEN_PAREN:
    case TT_NON_CAPTURING_OPEN_PAREN: {
      operator_t operator;

      operator.type = PT_CONCATENATION;
      CHECK_ERROR(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);

      operator.type = PT_GROUP;
      operator.data.group_index =(token.type == TT_OPEN_PAREN) ? (*n_capturing_groups)++
                                                               : NON_CAPTURING_GROUP;
      CHECK_ERROR(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);

      CHECK_ERROR(push_empty(&trees, allocator), CREX_E_NOMEM);
      break;
    }

    case TT_CLOSE_PAREN:
      while (operators.size > 0) {
        if (operators.data[operators.size - 1].type == PT_GROUP) {
          break;
        }

        CHECK_ERROR(pop_operator(&trees, &operators, allocator), CREX_E_NOMEM);
      }

      CHECK_ERROR(operators.size > 0, CREX_E_UNMATCHED_CLOSE_PAREN);

      assert(operators.data[operators.size - 1].type == PT_GROUP);

      CHECK_ERROR(pop_operator(&trees, &operators, allocator), CREX_E_NOMEM);

      break;

    default:
      assert(0);
    }
  }

  while (operators.size > 0) {
    if (operators.data[operators.size - 1].type == PT_GROUP) {
      CHECK_ERROR(operators.size == 1, CREX_E_UNMATCHED_OPEN_PAREN);
    }

    CHECK_ERROR(pop_operator(&trees, &operators, allocator), CREX_E_NOMEM);
  }

#undef CHECK_ERROR

  assert(trees.size == 1);

  parsetree_t *tree = trees.data[0];

  FREE(allocator, trees.data);
  FREE(allocator, operators.data);

  return tree;
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

    case PT_CONCATENATION:
    case PT_ALTERNATION: {
      // Destroy one subtree recursively
      destroy_parsetree(tree->data.children[0], allocator);

      // Destroy the other iteratively
      parsetree_t *child = tree->data.children[1];
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

static int push_tree(tree_stack_t *trees, parsetree_t *tree, const allocator_t *allocator) {
  assert(trees->size <= trees->capacity);

  if (trees->size == trees->capacity) {
    const size_t capacity = 2 * trees->capacity + 4;
    parsetree_t **data = ALLOC(allocator, sizeof(parsetree_t *) * capacity);

    if (data == NULL) {
      return 0;
    }

    safe_memcpy(data, trees->data, sizeof(parsetree_t *) * trees->capacity);
    FREE(allocator, trees->data);

    trees->data = data;
    trees->capacity = capacity;
  }

  trees->data[trees->size++] = tree;

  return 1;
}

static int push_empty(tree_stack_t *trees, const allocator_t *allocator) {
  parsetree_t *tree = ALLOC(allocator, sizeof(parsetree_t));

  if (tree == NULL) {
    return 0;
  }

  tree->type = PT_EMPTY;

  if (!push_tree(trees, tree, allocator)) {
    FREE(allocator, tree);
    return 0;
  }

  return 1;
}

static void destroy_tree_stack(tree_stack_t *trees, const allocator_t *allocator) {
  while (trees->size > 0) {
    destroy_parsetree(trees->data[--trees->size], allocator);
  }

  FREE(allocator, trees->data);
}

static int push_operator(tree_stack_t *trees,
                         operator_stack_t *operators,
                         const operator_t *
                         operator,
                         const allocator_t *allocator) {
  assert(operators->size <= operators->capacity);

  const parsetree_type_t type = operator->type;

  if (type != PT_GROUP) {
    const size_t precedence = operator_precedence[type];

    assert(type == PT_CONCATENATION || type == PT_ALTERNATION || type == PT_GREEDY_REPETITION ||
           type == PT_LAZY_REPETITION);

    while (operators->size > 0) {
      const parsetree_type_t other_type = operators->data[operators->size - 1].type;

      assert(other_type == PT_GROUP || other_type == PT_CONCATENATION ||
             other_type == PT_ALTERNATION || other_type == PT_GREEDY_REPETITION ||
             other_type == PT_LAZY_REPETITION);

      const int should_pop =
          other_type != PT_GROUP && operator_precedence[other_type] >= precedence;

      if (!should_pop) {
        break;
      }

      if (!pop_operator(trees, operators, allocator)) {
        return 0;
      }
    }
  }

  if (operators->size == operators->capacity) {
    const size_t capacity = 2 * operators->capacity + 4;
    operator_t *data = ALLOC(allocator, sizeof(operator_t) * capacity);

    if (data == NULL) {
      return 0;
    }

    safe_memcpy(data, operators->data, sizeof(operator_t) * operators->capacity);

    FREE(allocator, operators->data);

    operators->data = data;
    operators->capacity = capacity;
  }

  operators->data[operators->size++] = *operator;

  return 1;
}

static int
pop_operator(tree_stack_t *trees, operator_stack_t *operators, const allocator_t *allocator) {
  assert(operators->size > 0);

  parsetree_t *tree = ALLOC(allocator, sizeof(parsetree_t));

  if (tree == NULL) {
    return 0;
  }

  const operator_t *operator= &operators->data[--operators->size];

  tree->type = operator->type;

  switch (tree->type) {
  case PT_CONCATENATION:
  case PT_ALTERNATION:
    assert(trees->size >= 2);
    tree->data.children[0] = trees->data[trees->size - 2];
    tree->data.children[1] = trees->data[trees->size - 1];
    trees->size--;
    break;

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION:
    assert(trees->size >= 1);
    tree->data.repetition.lower_bound = operator->data.repetition.lower_bound;
    tree->data.repetition.upper_bound = operator->data.repetition.upper_bound;
    tree->data.repetition.child = trees->data[trees->size - 1];
    break;

  case PT_GROUP:
    assert(trees->size >= 1);
    tree->data.group.index = operator->data.group_index;
    tree->data.group.child = trees->data[trees->size - 1];
    break;

  default:
    assert(0);
  }

  trees->data[trees->size - 1] = tree;

  return 1;
}
