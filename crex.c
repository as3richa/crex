#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

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

static inline int push_tree(tree_stack_t *trees, parsetree_t *tree) WARN_UNUSED_RESULT;
static inline int push_empty(tree_stack_t *trees) WARN_UNUSED_RESULT;
// static inline void destroy_tree_stack(tree_stack_t* trees)
// WARN_UNUSED_RESULT;

static inline int
push_operator(tree_stack_t *trees, operator_stack_t *operators, const operator_t *operator)
    WARN_UNUSED_RESULT;
static inline int pop_operator(tree_stack_t *trees, operator_stack_t *operators) WARN_UNUSED_RESULT;
// static inline void destroy_operator_stack(operator_stack_t* operators)
// WARN_UNUSED_RESULT;

parsetree_t *parse(const char *str, size_t length) {
  const char *eof = str + length;

  tree_stack_t trees = {0, 0, NULL};
  operator_stack_t operators = {0, 0, NULL};

  assert(push_empty(&trees) && "FIXME");

  token_t token;

  while (lex(&token, &str, eof)) {
    switch (token.type) {
    case TT_CHARACTER:
    case TT_ANCHOR: {
      operator_t operator;
      operator.type = PT_CONCATENATION;
      assert(push_operator(&trees, &operators, &operator) && "FIXME");

      parsetree_t *tree = malloc(sizeof(parsetree_t));
      assert(tree != NULL && "FIXME");

      if (token.type == TT_CHARACTER) {
        tree->data.character = token.data.character;
      } else {
        tree->data.anchor_type = token.data.anchor_type;
      }

      assert(push_tree(&trees, tree) && "FIXME");

      break;
    }

    case TT_PIPE: {
      operator_t operator;
      operator.type = PT_ALTERNATION;
      assert(push_operator(&trees, &operators, &operator) && "FIXME");
      assert(push_empty(&trees) && "FIXME");
      break;
    }

    case TT_REPETITION: {
      operator_t operator;
      operator.type = PT_REPETITION;
      operator.repetition.lower_bound = token.data.repetition.lower_bound;
      operator.repetition.upper_bound = token.data.repetition.upper_bound;
      assert(push_operator(&trees, &operators, &operator) && "FIXME");
      break;
    }

    case TT_QUESTION_MARK: {
      operator_t operator;
      operator.type = PT_QUESTION_MARK;
      assert(push_operator(&trees, &operators, &operator) && "FIXME");
      break;
    }

    case TT_OPEN_PAREN: {
      operator_t operator;
      operator.type = PT_GROUP;
      assert(push_operator(&trees, &operators, &operator) && "FIXME");
      break;
    }

    case TT_CLOSE_PAREN:
      while (operators.size > 0) {
        if (operators.data[operators.size - 1].type == PT_GROUP) {
          break;
        }
        assert(pop_operator(&trees, &operators) && "FIXME");
      }

      if (operators.size == 0) {
        assert(0 && "FIXME");
      }

      assert(pop_operator(&trees, &operators) && "FIXME");

      break;

    default:
      assert(0);
    }
  }

  while (operators.size > 0) {
    if (operators.data[operators.size - 1].type == PT_GROUP) {
      break;
    }

    assert(pop_operator(&trees, &operators) && "FIXME");
  }

  assert(trees.size == 1);

  return trees.data[0];
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

static inline int
push_operator(tree_stack_t *trees, operator_stack_t *operators, const operator_t *operator) {
  assert(operators->size <= operators->capacity);

  const parsetree_type_t type = operator->type;

  while (operators->size > 0) {
    const parsetree_type_t other_type = operators->data[operators->size - 1].type;

    const int pop =
        other_type != PT_GROUP &&
        ((type == PT_ALTERNATION) || (type == PT_CONCATENATION && other_type != PT_ALTERNATION) ||
         (type == PT_REPETITION && other_type == PT_REPETITION));

    if (!pop) {
      break;
    }

    assert(pop_operator(trees, operators) && "FIXME");
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
  const parsetree_t *tree = parse(str, length);
  crex_print_parsetree(tree, 0);
  fputc('\n', stderr);
}

#endif
