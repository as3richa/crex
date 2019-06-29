#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#define REPETITION_INFINITY (~(size_t)0)

typedef unsigned char byte_t;

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
    byte_t character;

    anchor_type_t anchor_type;

    struct {
      size_t lower_bound, upper_bound;
    } repetition;
  } data;
} token_t;

static inline int lex(token_t* result, const byte_t** str, const byte_t* eof) {
  if((*str) == eof) {
    return 0;
  }

  const byte_t byte = *((*str) ++);

  switch(byte) {
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

      if((*str) == eof) {
        assert(0 && "FIXME");
      } else {
        switch(*((*str) ++)) {
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
    byte_t character;

    anchor_type_t anchor_type;

    struct parsetree* children[2];

    struct {
      size_t lower_bound, upper_bound;
      struct parsetree* child;
    } repetition;

    struct parsetree* child;
  } data;
} parsetree_t;

typedef struct {
  size_t size, capacity;
  parsetree_t** buffer;
} output_queue_t;

typedef struct {
  parsetree_type_t type;
  size_t lower_bound, upper_bound;
} operator_t;

typedef struct {
  size_t size, capacity;
  operator_t* buffer;
} operator_stack_t;

static inline void push_empty(output_queue_t* queue);
static inline void push_atom(output_queue_t* queue, operator_stack_t* stack, const token_t* token);
static inline void push_operator(output_queue_t* queue, operator_stack_t* stack, const operator_t* operator);
static inline void pop_operator(output_queue_t* queue, operator_stack_t* stack);

static inline void push_empty(output_queue_t* queue) {
  if(queue->size == queue->capacity) {
    queue->capacity = 2 * queue->capacity + 16;
    queue->buffer = realloc(queue->buffer, sizeof(parsetree_t*) * queue->capacity);
    assert(queue->buffer != NULL && "FIXME");
  }

  parsetree_t* tree = malloc(sizeof(parsetree_t));
  assert(tree != NULL && "FIXME");

  tree->type = PT_EMPTY;
  queue->buffer[queue->size ++] = tree;
}

static inline void push_atom(output_queue_t* queue, operator_stack_t* stack, const token_t* token) {
  if(queue->size == queue->capacity) {
    queue->capacity = 2 * queue->capacity + 16;
    queue->buffer = realloc(queue->buffer, sizeof(parsetree_t*) * queue->capacity);
    assert(queue->buffer != NULL && "FIXME");
  }

  parsetree_t* tree = malloc(sizeof(parsetree_t));
  assert(tree != NULL && "FIXME");

  if(token->type == TT_CHARACTER) {
    tree->type = PT_CHARACTER;
    tree->data.character = token->data.character;
  } else {
    assert(token->type == TT_ANCHOR);
    tree->type = PT_ANCHOR;
    tree->data.anchor_type = tree->data.anchor_type;
  }

  queue->buffer[queue->size ++] = tree;
}

static inline void push_operator(output_queue_t* queue, operator_stack_t* stack, const operator_t* operator) {
  const parsetree_type_t type = operator->type;

  while(stack->size > 0) {
    const parsetree_type_t other_type = stack->buffer[stack->size - 1].type;

    const int pop = other_type != PT_GROUP && ((type == PT_ALTERNATION) ||
                    (type == PT_CONCATENATION && other_type != PT_ALTERNATION) ||
                    (type == PT_REPETITION && other_type == PT_REPETITION));

    if(!pop) {
      break;
    }

    pop_operator(queue, stack);
  }

  if(stack->size == stack->capacity) {
    stack->capacity = 2 * stack->capacity + 16;
    stack->buffer = realloc(stack->buffer, sizeof(operator_t) * stack->capacity);
    assert(stack->buffer != NULL && "FIXME");
  }

  stack->buffer[stack->size ++] = *operator;
}

static inline void pop_operator(output_queue_t* queue, operator_stack_t* stack) {
  assert(stack->size > 0);

  parsetree_t* tree = malloc(sizeof(parsetree_t));
  assert(tree != NULL && "FIXME");

  const operator_t* operator = &stack->buffer[-- stack->size];

  tree->type = operator->type;

  if(tree->type == PT_CONCATENATION || tree->type == PT_ALTERNATION) {
    assert(queue->size >= 2 && "FIXME");
    tree->data.children[0] = queue->buffer[queue->size - 2];
    tree->data.children[1] = queue->buffer[queue->size - 1];
    queue->size --;
  } else if(tree->type == PT_REPETITION) {
    assert(queue->size >= 1 && "FIXME");
    tree->data.repetition.child = queue->buffer[queue->size - 1];
    tree->data.repetition.lower_bound = operator->lower_bound;
    tree->data.repetition.upper_bound = operator->upper_bound;
  } else {
    assert(tree->type == PT_QUESTION_MARK || tree->type == PT_GROUP);
    assert(queue->size >= 1 && "FIXME");
    tree->data.child = queue->buffer[queue->size - 1];
  }

  queue->buffer[queue->size - 1] = tree;
}

parsetree_t* parse(const byte_t* str, size_t length) {
  const char* eof = str + length;

  operator_stack_t stack = { 0, 0, NULL };
  output_queue_t queue = { 0, 0, NULL};

  push_empty(&queue);

  token_t token;

  while(lex(&token, &str, eof)) {
    switch(token.type) {
      case TT_CHARACTER:
      case TT_ANCHOR:
      {
        operator_t operator;
        operator.type = PT_CONCATENATION;
        push_operator(&queue, &stack, &operator);
        push_atom(&queue, &stack, &token);
        break;
      }

      case TT_PIPE:
      {
        operator_t operator;
        operator.type = PT_ALTERNATION;
        push_operator(&queue, &stack, &operator);
        push_empty(&queue);
        break;
      }

      case TT_REPETITION:
      {
        operator_t operator;
        operator.type = PT_REPETITION;
        operator.lower_bound = token.data.repetition.lower_bound;
        operator.upper_bound = token.data.repetition.upper_bound;
        push_operator(&queue, &stack, &operator);
        break;
      }

      case TT_QUESTION_MARK:
      {
        operator_t operator;
        operator.type = PT_QUESTION_MARK;
        push_operator(&queue, &stack, &operator);
        break;
      }

      case TT_OPEN_PAREN:
      {
        operator_t operator;
        operator.type = PT_GROUP;
        push_operator(&queue, &stack, &operator);
        break;
      }

      case TT_CLOSE_PAREN:
        while(stack.size > 0) {
          if(stack.buffer[stack.size - 1].type == PT_GROUP) {
            break;
          }
          pop_operator(&queue, &stack);
        }

        if(stack.size == 0) {
          assert(0 && "FIXME");
        }

        pop_operator(&queue, &stack);

        break;

      default:
        assert(0);
    }
  }

  while(stack.size > 0) {
    if(stack.buffer[stack.size - 1].type == PT_GROUP) {
      break;
    }
    pop_operator(&queue, &stack);
  }

  assert(queue.size == 1);

  return queue.buffer[0];
}

#ifdef CREX_DEBUG

#include <ctype.h>
#include <stdio.h>

const char* anchor_type_to_str(anchor_type_t type) {
  switch(type) {
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

void crex_debug_lex(const byte_t* str, size_t length) {
  const char* eof = str + length;

  token_t token;

  while(lex(&token, &str, eof)) {
    switch(token.type) {
      case TT_CHARACTER:
        if(isprint(token.data.character)) {
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
        if(token.data.repetition.upper_bound == REPETITION_INFINITY) {
          fprintf(stderr, "TT_REPETITION %zu ... inf\n", token.data.repetition.lower_bound);
        } else {
          fprintf(stderr, "TT_REPETITION %zu ... %zu\n", token.data.repetition.lower_bound, token.data.repetition.upper_bound);
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

static void crex_print_parsetree(const parsetree_t* tree, size_t depth) {
  for(size_t i = 0; i < depth; i ++) {
    fputc(' ', stderr);
  }

  switch(tree->type) {
    case PT_EMPTY:
      fputs("(PT_EMPTY)", stderr);
      break;

    case PT_CHARACTER:
      if(isprint(tree->data.character)) {
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
      if(tree->data.repetition.upper_bound == REPETITION_INFINITY) {
        fprintf(stderr, "(PT_REPETITION %zu inf\n", tree->data.repetition.lower_bound);
      } else {
        fprintf(stderr, "(PT_REPETITION %zu %zu\n", tree->data.repetition.lower_bound, tree->data.repetition.upper_bound);
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

void crex_debug_parse(const byte_t* str, size_t length) {
  const parsetree_t* tree = parse(str, length);
  crex_print_parsetree(tree, 0);
  fputc('\n', stderr);
}

#endif
