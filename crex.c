#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include "crex.h"

#ifdef __GNUC__
#define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define WARN_UNUSED_RESULT
#endif

#define REPETITION_INFINITY (~(size_t)0)

/** Anchor type (common to lexer and parser) **/

typedef enum {
  AT_BOF,
  AT_BOL,
  AT_EOF,
  AT_EOL,
  AT_WORD_BOUNDARY,
  AT_NOT_WORD_BOUNDARY
} anchor_type_t;

/** Lexer **/

typedef enum {
  TT_CHARACTER,
  TT_ANCHOR,
  TT_PIPE,
  TT_REPETITION,
  TT_QUESTION_MARK,
  TT_OPEN_PAREN,
  TT_CLOSE_PAREN
} token_type_t;

typedef struct {
  token_type_t type;

  union {
    char character;

    anchor_type_t anchor_type;

    struct {
      size_t lower_bound, upper_bound;
    } repetition;
  } data;
} token_t;

static inline int lex(token_t *result, const char **str, const char *eof) {
  if ((*str) == eof) {
    return 0;
  }

  const char byte = *((*str)++);

  switch (byte) {
  case '^':
    result->type = TT_ANCHOR;
    result->data.anchor_type = AT_BOL;
    break;

  case '$':
    result->type = TT_ANCHOR;
    result->data.anchor_type = AT_EOL;
    break;

  case '|':
    result->type = TT_PIPE;
    break;

  case '*':
    result->type = TT_REPETITION;
    result->data.repetition.lower_bound = 0;
    result->data.repetition.upper_bound = REPETITION_INFINITY;
    break;

  case '+':
    result->type = TT_REPETITION;
    result->data.repetition.lower_bound = 1;
    result->data.repetition.upper_bound = REPETITION_INFINITY;
    break;

  case '?':
    result->type = TT_QUESTION_MARK;
    break;

  case '(':
    result->type = TT_OPEN_PAREN;
    break;

  case ')':
    result->type = TT_CLOSE_PAREN;
    break;

  case '\\':
    result->type = TT_CHARACTER;

    if ((*str) == eof) {
      assert(0 && "FIXME");
    } else {
      switch (*((*str)++)) {
      case 'a':
        result->data.character = '\a';
        break;

      case 'b':
        result->data.character = '\b';
        break;

      case 'f':
        result->data.character = '\f';
        break;

      case 'n':
        result->data.character = '\n';
        break;

      case 'r':
        result->data.character = '\r';
        break;

      case 't':
        result->data.character = '\t';
        break;

      case 'v':
        result->data.character = '\v';
        break;

      case 'x':
        assert(0 && "FIXME");
        break;

      case '.':
      case '|':
      case '*':
      case '+':
      case '?':
      case '(':
      case ')':
      case '[':
      case ']':
      case '{':
      case '}':
      case '^':
      case '$':
      case '\\':
        result->data.character = byte;
        break;

      case 'd':
      case 'D':
      case 's':
      case 'S':
      case 'w':
      case 'W':
        assert(0 && "FIXME");
        break;

      default:
        assert(0 && "FIXME");
      }
    }

    break;

  default:
    result->type = TT_CHARACTER;
    result->data.character = byte;
  }

  return 1;
}

/** Parser **/

typedef enum {
  PT_EMPTY,
  PT_CHARACTER,
  PT_ANCHOR,
  PT_CONCATENATION,
  PT_ALTERNATION,
  PT_REPETITION,
  PT_QUESTION_MARK,
  PT_GROUP
} parsetree_type_t;

typedef struct parsetree {
  parsetree_type_t type;

  union {
    char character;

    anchor_type_t anchor_type;

    struct parsetree *children[2];

    struct {
      size_t lower_bound, upper_bound;
      struct parsetree *child;
    } repetition;

    struct parsetree *child;
  } data;
} parsetree_t;

typedef struct {
  parsetree_type_t type;
  struct {
    size_t lower_bound, upper_bound;
  } repetition;
} operator_t;

typedef struct {
  size_t size, capacity;
  parsetree_t **data;
} tree_stack_t;

typedef struct {
  size_t size, capacity;
  operator_t *data;
} operator_stack_t;

static inline void free_parsetree(parsetree_t *tree);

WARN_UNUSED_RESULT static inline int push_tree(tree_stack_t *trees, parsetree_t *tree);
WARN_UNUSED_RESULT static inline int push_empty(tree_stack_t *trees);
static inline void free_tree_stack(tree_stack_t *trees);

WARN_UNUSED_RESULT static inline int
push_operator(tree_stack_t *trees, operator_stack_t *operators, const operator_t *operator);
WARN_UNUSED_RESULT static inline int pop_operator(tree_stack_t *trees, operator_stack_t *operators);

crex_status_t parse(parsetree_t **result, const char *str, size_t length) {
  const char *eof = str + length;

  tree_stack_t trees = {0, 0, NULL};
  operator_stack_t operators = {0, 0, NULL};

#define CHECK_ERRORS(condition, code)                                                              \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      free_tree_stack(&trees);                                                                     \
      free(operators.data);                                                                        \
      return code;                                                                                 \
    }                                                                                              \
  } while (0)

  CHECK_ERRORS(push_empty(&trees), CREX_E_NOMEM);

  token_t token;

  while (lex(&token, &str, eof)) {
    switch (token.type) {
    case TT_CHARACTER:
    case TT_ANCHOR: {
      operator_t operator;
      operator.type = PT_CONCATENATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);

      parsetree_t *tree = malloc(sizeof(parsetree_t));
      CHECK_ERRORS(tree != NULL, CREX_E_NOMEM);

      if (token.type == TT_CHARACTER) {
        tree->data.character = token.data.character;
      } else {
        tree->data.anchor_type = token.data.anchor_type;
      }

      CHECK_ERRORS(push_tree(&trees, tree), CREX_E_NOMEM);

      break;
    }

    case TT_PIPE: {
      operator_t operator;
      operator.type = PT_ALTERNATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);
      CHECK_ERRORS(push_empty(&trees), CREX_E_NOMEM);
      break;
    }

    case TT_REPETITION: {
      operator_t operator;
      operator.type = PT_REPETITION;
      operator.repetition.lower_bound = token.data.repetition.lower_bound;
      operator.repetition.upper_bound = token.data.repetition.upper_bound;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);
      break;
    }

    case TT_QUESTION_MARK: {
      operator_t operator;
      operator.type = PT_QUESTION_MARK;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);
      break;
    }

    case TT_OPEN_PAREN: {
      operator_t operator;
      operator.type = PT_GROUP;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);
      break;
    }

    case TT_CLOSE_PAREN:
      while (operators.size > 0) {
        if (operators.data[operators.size - 1].type == PT_GROUP) {
          break;
        }

        CHECK_ERRORS(pop_operator(&trees, &operators), CREX_E_NOMEM);
      }

      CHECK_ERRORS(operators.size > 0, CREX_E_UNMATCHED_CLOSE_PAREN);
      CHECK_ERRORS(pop_operator(&trees, &operators), CREX_E_NOMEM);

      break;

    default:
      assert(0);
    }
  }

  while (operators.size > 0) {
    CHECK_ERRORS(operators.data[operators.size - 1].type != PT_GROUP, CREX_E_UNMATCHED_OPEN_PAREN);
    CHECK_ERRORS(pop_operator(&trees, &operators), CREX_E_NOMEM);
  }

  assert(trees.size == 1);

  (*result) = trees.data[0];

  free(trees.data);
  free(operators.data);

  return CREX_OK;
}

static inline void free_parsetree(parsetree_t *tree) {
  switch (tree->type) {
  case PT_EMPTY:
  case PT_CHARACTER:
  case PT_ANCHOR:
    break;

  case PT_CONCATENATION:
  case PT_ALTERNATION:
    free_parsetree(tree->data.children[0]);
    free_parsetree(tree->data.children[1]);
    break;

  case PT_REPETITION:
    free_parsetree(tree->data.repetition.child);
    break;

  case PT_QUESTION_MARK:
  case PT_GROUP:
    free_parsetree(tree->data.child);
    break;

  default:
    assert(0);
  }

  free(tree);
}

static inline int push_tree(tree_stack_t *trees, parsetree_t *tree) {
  assert(trees->size <= trees->capacity);

  if (trees->size == trees->capacity) {
    trees->capacity = 2 * trees->capacity + 16;
    parsetree_t **data = realloc(trees->data, sizeof(parsetree_t *) * trees->capacity);

    if (data == NULL) {
      return 0;
    }

    trees->data = data;
  }

  trees->data[trees->size++] = tree;

  return 1;
}

static inline int push_empty(tree_stack_t *trees) {
  parsetree_t *tree = malloc(sizeof(parsetree_t));

  if (tree == NULL) {
    return 0;
  }

  tree->type = PT_EMPTY;

  return push_tree(trees, tree);
}

static inline void free_tree_stack(tree_stack_t *trees) {
  while (trees->size > 0) {
    free_parsetree(trees->data[--trees->size]);
  }

  free(trees->data);
}

static inline int
push_operator(tree_stack_t *trees, operator_stack_t *operators, const operator_t *operator) {
  assert(operators->size <= operators->capacity);

  const parsetree_type_t type = operator->type;

  while (operators->size > 0) {
    const parsetree_type_t other_type = operators->data[operators->size - 1].type;

    const int should_pop =
        other_type != PT_GROUP &&
        ((type == PT_ALTERNATION) || (type == PT_CONCATENATION && other_type != PT_ALTERNATION) ||
         (type == PT_REPETITION && other_type == PT_REPETITION));

    if (!should_pop) {
      break;
    }

    if (!pop_operator(trees, operators)) {
      return 0;
    }
  }

  if (operators->size == operators->capacity) {
    operators->capacity = 2 * operators->capacity + 16;

    operator_t *data = realloc(operators->data, sizeof(operator_t) * operators->capacity);

    if (data == NULL) {
      return 0;
    }

    operators->data = data;
  }

  operators->data[operators->size++] = *operator;

  return 1;
}

static inline int pop_operator(tree_stack_t *trees, operator_stack_t *operators) {
  assert(operators->size > 0);

  parsetree_t *tree = malloc(sizeof(parsetree_t));

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

  case PT_REPETITION:
    assert(trees->size >= 1);
    tree->data.repetition.child = trees->data[trees->size - 1];
    tree->data.repetition.lower_bound = operator->repetition.lower_bound;
    tree->data.repetition.upper_bound = operator->repetition.upper_bound;
    break;

  case PT_QUESTION_MARK:
  case PT_GROUP:
    assert(trees->size >= 1);
    tree->data.child = trees->data[trees->size - 1];
    break;

  default:
    assert(0);
  }

  trees->data[trees->size - 1] = tree;

  return 1;
}

#ifdef CREX_DEBUG

#include <ctype.h>
#include <stdio.h>

const char *anchor_type_to_str(anchor_type_t type) {
  switch (type) {
  case AT_BOF:
    return "\\A";

  case AT_BOL:
    return "^";

  case AT_EOF:
    return "\\z";

  case AT_EOL:
    return "$";

  case AT_WORD_BOUNDARY:
    return "\\w";

  case AT_NOT_WORD_BOUNDARY:
    return "\\W";

  default:
    assert(0);
    return NULL;
  }
}

const char *crex_status_to_str(crex_status_t status) {
  switch (status) {
  case CREX_OK:
    return "CREX_OK";

  case CREX_E_NOMEM:
    return "CREX_E_NOMEM";

  case CREX_E_UNMATCHED_OPEN_PAREN:
    return "CREX_E_UNMATCHED_OPEN_PAREN";

  case CREX_E_UNMATCHED_CLOSE_PAREN:
    return "CREX_E_UNMATCHED_CLOSE_PAREN";

  default:
    assert(0);
    return NULL;
  }
}

void crex_debug_lex(const char *str, size_t length) {
  const char *eof = str + length;

  token_t token;

  while (lex(&token, &str, eof)) {
    switch (token.type) {
    case TT_CHARACTER:
      if (isprint(token.data.character)) {
        fprintf(stderr, "TT_CHARACTER %c\n", token.data.character);
      } else {
        fprintf(stderr, "TT_CHARACTER %d\n", (int)token.data.character);
      }
      break;

    case TT_ANCHOR:
      fprintf(stderr, "TT_ANCHOR %s\n", anchor_type_to_str(token.data.anchor_type));
      break;

    case TT_PIPE:
      fputs("TT_PIPE\n", stderr);
      break;

    case TT_REPETITION:
      if (token.data.repetition.upper_bound == REPETITION_INFINITY) {
        fprintf(stderr, "TT_REPETITION %zu ... inf\n", token.data.repetition.lower_bound);
      } else {
        fprintf(stderr,
                "TT_REPETITION %zu ... %zu\n",
                token.data.repetition.lower_bound,
                token.data.repetition.upper_bound);
      }
      break;

    case TT_QUESTION_MARK:
      fputs("TT_QUESTION_MARK\n", stderr);
      break;

    case TT_OPEN_PAREN:
      fputs("TT_OPEN_PAREN\n", stderr);
      break;

    case TT_CLOSE_PAREN:
      fputs("TT_CLOSE_PAREN\n", stderr);
      break;

    default:
      assert(0);
    }
  }
}

static void crex_print_parsetree(const parsetree_t *tree, size_t depth) {
  for (size_t i = 0; i < depth; i++) {
    fputc(' ', stderr);
  }

  switch (tree->type) {
  case PT_EMPTY:
    fputs("(PT_EMPTY)", stderr);
    break;

  case PT_CHARACTER:
    if (isprint(tree->data.character)) {
      fprintf(stderr, "(PT_CHARACTER %c)", tree->data.character);
    } else {
      fprintf(stderr, "(PT_CHARACTER 0x%x)", tree->data.character);
    }
    break;

  case PT_ANCHOR:
    fprintf(stderr, "(PT_ANCHOR %s)", anchor_type_to_str(tree->data.anchor_type));
    break;

  case PT_CONCATENATION:
    fputs("(PT_CONCATENATION\n", stderr);
    crex_print_parsetree(tree->data.children[0], depth + 1);
    fputc('\n', stderr);
    crex_print_parsetree(tree->data.children[1], depth + 1);
    fputc(')', stderr);
    break;

  case PT_ALTERNATION:
    fputs("(PT_ALTERNATION\n", stderr);
    crex_print_parsetree(tree->data.children[0], depth + 1);
    fputc('\n', stderr);
    crex_print_parsetree(tree->data.children[1], depth + 1);
    fputc(')', stderr);
    break;

  case PT_REPETITION:
    if (tree->data.repetition.upper_bound == REPETITION_INFINITY) {
      fprintf(stderr, "(PT_REPETITION %zu inf\n", tree->data.repetition.lower_bound);
    } else {
      fprintf(stderr,
              "(PT_REPETITION %zu %zu\n",
              tree->data.repetition.lower_bound,
              tree->data.repetition.upper_bound);
    }
    crex_print_parsetree(tree->data.repetition.child, depth + 1);
    fputc(')', stderr);
    break;

  case PT_QUESTION_MARK:
    fputs("(PT_QUESTION_MARK\n", stderr);
    crex_print_parsetree(tree->data.child, depth + 1);
    fputc(')', stderr);
    break;

  case PT_GROUP:
    fputs("(PT_GROUP\n", stderr);
    crex_print_parsetree(tree->data.child, depth + 1);
    fputc(')', stderr);
    break;

  default:
    assert(0);
  }
}

void crex_debug_parse(const char *str, size_t length) {
  parsetree_t *tree;
  const crex_status_t status = parse(&tree, str, length);
  if (status == CREX_OK) {
    crex_print_parsetree(tree, 0);
    fputc('\n', stderr);
  } else {
    fprintf(stderr, "Parse failed with status %s\n", crex_status_to_str(status));
  }
}

#endif
