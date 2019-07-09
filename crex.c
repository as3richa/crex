#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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
  TT_GREEDY_REPETITION,
  TT_LAZY_REPETITION,
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

WARN_UNUSED_RESULT static inline crex_status_t
lex(token_t *result, const char **str, const char *eof) {
  assert((*str) < eof);

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
  case '+':
  case '?':
    switch (byte) {
    case '*':
      result->data.repetition.lower_bound = 0;
      result->data.repetition.upper_bound = REPETITION_INFINITY;
      break;

    case '+':
      result->data.repetition.lower_bound = 1;
      result->data.repetition.upper_bound = REPETITION_INFINITY;
      break;

    case '?':
      result->data.repetition.lower_bound = 0;
      result->data.repetition.upper_bound = 1;
      break;
    }

    if ((*str) < eof && *(*str) == '?') {
      (*str)++;
      result->type = TT_LAZY_REPETITION;
    } else {
      result->type = TT_GREEDY_REPETITION;
    }

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
      return CREX_E_BAD_ESCAPE;
    }

    switch (*((*str)++)) {
    case 'a':
      result->data.character = '\a';
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

    case 'A':
      result->type = TT_ANCHOR;
      result->data.anchor_type = AT_BOF;
      break;

    case 'z':
      result->type = TT_ANCHOR;
      result->data.anchor_type = AT_EOF;
      break;

    case 'b':
      result->type = TT_ANCHOR;
      result->data.anchor_type = AT_WORD_BOUNDARY;
      break;

    case 'B':
      result->type = TT_ANCHOR;
      result->data.anchor_type = AT_NOT_WORD_BOUNDARY;
      break;

    case 'x': {
      int value = 0;

      for (int i = 2; i--;) {
        if ((*str) == eof) {
          return CREX_E_BAD_ESCAPE;
        }

        const char hex_byte = *((*str)++);

        int digit;

        if ('0' <= hex_byte && hex_byte <= '9') {
          digit = hex_byte - '0';
        } else if ('a' <= hex_byte && hex_byte <= 'f') {
          digit = hex_byte - 'a' + 0xa;
        } else if ('A' <= hex_byte && hex_byte <= 'F') {
          digit = hex_byte - 'A' + 0xa;
        } else {
          return CREX_E_BAD_ESCAPE;
        }

        value = 16 * value + digit;
      }

      result->data.character = value;

      break;
    }

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
      return CREX_E_BAD_ESCAPE;
    }

    break;

  default:
    result->type = TT_CHARACTER;
    result->data.character = byte;
  }

  return CREX_OK;
}

/** Parser **/

typedef enum {
  PT_CONCATENATION,
  PT_ALTERNATION,
  PT_GREEDY_REPETITION,
  PT_LAZY_REPETITION,
  PT_GROUP,
  PT_EMPTY,
  PT_CHARACTER,
  PT_ANCHOR,
} parsetree_type_t;

/* FIXME: explain this */
static const size_t operator_precedence[] = {1, 0, 2, 2};

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

WARN_UNUSED_RESULT crex_status_t parse(parsetree_t **result, const char *str, size_t length) {
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

  while (str != eof) {
    const crex_status_t lex_status = lex(&token, &str, eof);
    CHECK_ERRORS(lex_status == CREX_OK, lex_status);

    switch (token.type) {
    case TT_CHARACTER:
    case TT_ANCHOR: {
      operator_t operator;
      operator.type = PT_CONCATENATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator), CREX_E_NOMEM);

      parsetree_t *tree = malloc(sizeof(parsetree_t));
      CHECK_ERRORS(tree != NULL, CREX_E_NOMEM);

      if (token.type == TT_CHARACTER) {
        tree->type = PT_CHARACTER;
        tree->data.character = token.data.character;
      } else {
        tree->type = PT_ANCHOR;
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

    case TT_GREEDY_REPETITION:
    case TT_LAZY_REPETITION: {
      operator_t operator;
      operator.type =(token.type == TT_GREEDY_REPETITION) ? PT_GREEDY_REPETITION
                                                          : PT_LAZY_REPETITION;
      operator.repetition.lower_bound = token.data.repetition.lower_bound;
      operator.repetition.upper_bound = token.data.repetition.upper_bound;
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

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION:
    free_parsetree(tree->data.repetition.child);
    break;

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

  const size_t precedence = operator_precedence[operator->type];

  while (operators->size > 0) {
    const parsetree_type_t other_type = operators->data[operators->size - 1].type;

    const int should_pop = other_type != PT_GROUP && operator_precedence[other_type] >= precedence;

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

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION:
    assert(trees->size >= 1);
    tree->data.repetition.child = trees->data[trees->size - 1];
    tree->data.repetition.lower_bound = operator->repetition.lower_bound;
    tree->data.repetition.upper_bound = operator->repetition.upper_bound;
    break;

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

/** Compiler */

enum {
  VM_CHARACTER,
  VM_ANCHOR_BOF,
  VM_ANCHOR_BOL,
  VM_ANCHOR_EOF,
  VM_ANCHOR_EOL,
  VM_ANCHOR_WORD_BOUNDARY,
  VM_ANCHOR_NOT_WORD_BOUNDARY,
  VM_JUMP,
  VM_SPLIT_PASSIVE,
  VM_SPLIT_EAGER
};

enum {
  VM_OPERAND_NONE = 0,
  VM_OPERAND_8 = (1u << 5u),
  VM_OPERAND_16 = (1u << 6u),
  VM_OPERAND_32 = (1u << 7u)
};

typedef struct {
  size_t size;
  unsigned char *bytecode;
} regex_t;

crex_status_t compile(regex_t *result, parsetree_t *tree) {
  switch (tree->type) {
  case PT_EMPTY:
    result->size = 0;
    result->bytecode = NULL;
    break;

  case PT_CHARACTER:
    result->size = 2;
    result->bytecode = malloc(2);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    result->bytecode[0] = VM_CHARACTER;
    result->bytecode[1] = tree->data.character;

    break;

  case PT_ANCHOR:
    result->size = 1;
    result->bytecode = malloc(1);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    result->bytecode[0] = VM_ANCHOR_BOF + tree->data.anchor_type;

    break;

  case PT_CONCATENATION:
  case PT_ALTERNATION: {
    regex_t left;
    crex_status_t status = compile(&left, tree->data.children[0]);

    if (status != CREX_OK) {
      return status;
    }

    regex_t right;
    status = compile(&right, tree->data.children[1]);

    if (status != CREX_OK) {
      free(left.bytecode);
      return status;
    }

    if (tree->type == PT_CONCATENATION) {
      result->size = left.size + right.size;
    } else {
      result->size = (1 + sizeof(size_t)) + left.size + (1 + sizeof(size_t)) + right.size;
    }

    result->bytecode = malloc(result->size);

    if (result->bytecode == NULL) {
      free(left.bytecode);
      free(right.bytecode);
      return CREX_E_NOMEM;
    }

    if (tree->type == PT_CONCATENATION) {
      memcpy(result->bytecode, left.bytecode, left.size);
      memcpy(result->bytecode + left.size, right.bytecode, right.size);
    } else {
      const size_t split_location = 0;
      const size_t left_location = 1 + sizeof(long);
      const size_t jump_location = left_location + left.size;
      const size_t right_location = jump_location + 1 + sizeof(long);

      const size_t split_origin = split_location + 1 + sizeof(long);
      const size_t jump_origin = jump_location + 1 + sizeof(long);

      const long split_delta = (long)right_location - (long)split_origin;
      const long jump_delta = (long)right_location + (long)right.size - (long)jump_origin;

      unsigned char *bytecode = result->bytecode;

      bytecode[split_location] = VM_SPLIT_PASSIVE;
      memcpy(bytecode + split_location + 1, &split_delta, sizeof(size_t));

      memcpy(bytecode + left_location, left.bytecode, left.size);

      bytecode[jump_location] = VM_JUMP;
      memcpy(bytecode + jump_location + 1, &jump_delta, sizeof(size_t));

      memcpy(bytecode + right_location, right.bytecode, right.size);
    }

    free(left.bytecode);
    free(right.bytecode);

    break;
  }

  case PT_GREEDY_REPETITION:
    assert(0);
    break;

  case PT_LAZY_REPETITION:
    assert(0);
    break;

  case PT_GROUP:
    assert(0);
    break;

  default:
    assert(0);
  }

  return CREX_OK;
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

  case CREX_E_BAD_ESCAPE:
    return "CREX_E_BAD_ESCAPE";

  case CREX_E_UNMATCHED_OPEN_PAREN:
    return "CREX_E_UNMATCHED_OPEN_PAREN";

  case CREX_E_UNMATCHED_CLOSE_PAREN:
    return "CREX_E_UNMATCHED_CLOSE_PAREN";

  default:
    assert(0);
    return NULL;
  }
}

const char *crex_vm_code_to_str(unsigned char code) {
  switch (code) {
  case VM_CHARACTER:
    return "VM_CHARACTER";

  case VM_ANCHOR_BOF:
    return "VM_ANCHOR_BOF";

  case VM_ANCHOR_BOL:
    return "VM_ANCHOR_BOL";

  case VM_ANCHOR_EOF:
    return "VM_ANCHOR_EOF";

  case VM_ANCHOR_WORD_BOUNDARY:
    return "VM_ANCHOR_WORD_BOUNDARY";

  case VM_ANCHOR_NOT_WORD_BOUNDARY:
    return "VM_ANCHOR_NOT_WORD_BOUNDARY";

  case VM_ANCHOR_EOL:
    return "VM_ANCHOR_EOL";

  case VM_JUMP:
    return "VM_JUMP";

  case VM_SPLIT_PASSIVE:
    return "VM_SPLIT_PASSIVE";

  case VM_SPLIT_EAGER:
    return "VM_SPLIT_EAGER";

  default:
    assert(0);
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
        fprintf(stderr, "TT_CHARACTER 0x%02x\n", 0xff & (int)token.data.character);
      }
      break;

    case TT_ANCHOR:
      fprintf(stderr, "TT_ANCHOR %s\n", anchor_type_to_str(token.data.anchor_type));
      break;

    case TT_PIPE:
      fputs("TT_PIPE\n", stderr);
      break;

    case TT_GREEDY_REPETITION:
    case TT_LAZY_REPETITION: {
      const char *str =
          (token.type == TT_GREEDY_REPETITION) ? "TT_GREEDY_REPETITION" : "TT_LAZY_REPETITION";

      fprintf(stderr, "%s %zu ... ", str, token.data.repetition.lower_bound);

      if (token.data.repetition.upper_bound == REPETITION_INFINITY) {
        fputs("inf\n", stderr);
      } else {
        fprintf(stderr, "%zu\n", token.data.repetition.upper_bound);
      }

      break;
    }

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
      fprintf(stderr, "(PT_CHARACTER 0x%02x)", 0xff & (int)tree->data.character);
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

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION: {
    const char *str =
        (tree->type == PT_GREEDY_REPETITION) ? "PT_GREEDY_REPETITION" : "PT_LAZY_REPETITION";

    fprintf(stderr, "(%s %zu ", str, tree->data.repetition.lower_bound);

    if (tree->data.repetition.upper_bound == REPETITION_INFINITY) {
      fputs("inf\n", stderr);
    } else {
      fprintf(stderr, "%zu\n", tree->data.repetition.upper_bound);
    }

    crex_print_parsetree(tree->data.repetition.child, depth + 1);
    fputc(')', stderr);

    break;
  }

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

void crex_debug_compile(const char *str, size_t length) {
  parsetree_t *tree;
  const crex_status_t status = parse(&tree, str, length);

  if (status == CREX_OK) {
    regex_t regex;
    compile(&regex, tree);

    for (size_t i = 0; i < regex.size; i++) {
      const unsigned char code = regex.bytecode[i];

      fprintf(stderr, "%04zx %s ", i, crex_vm_code_to_str(code));

      if (code == VM_JUMP || code == VM_SPLIT_PASSIVE || code == VM_SPLIT_EAGER) {
        fprintf(stderr, "%04lx\n", *(long *)(regex.bytecode + i + 1));
        i += sizeof(size_t);
      } else if (code == VM_CHARACTER) {
        fprintf(stderr, "%c\n", regex.bytecode[i + 1]);
        i++;
      } else {
        fputc('\n', stderr);
      }
    }
  } else {
    fprintf(stderr, "Parse failed with status %s\n", crex_status_to_str(status));
  }
}

#endif
