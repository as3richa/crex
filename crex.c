#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h> // FIXME

#include "crex.h"

typedef crex_status_t status_t;
typedef crex_regex_t regex_t;
typedef crex_context_t context_t;
typedef crex_allocator_t allocator_t;
#define WARN_UNUSED_RESULT CREX_WARN_UNUSED_RESULT

#define REPETITION_INFINITY SIZE_MAX

typedef struct {
  const char *name;
  size_t name_size;
  unsigned char bitmap[32];
} builtin_char_class_t;

builtin_char_class_t builtin_char_classes[] = {
    {"alnum", 5, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0xfe, 0xff, 0xff,
                  0x07, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"alpha", 5, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff,
                  0x07, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"ascii", 5, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                  0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"blank", 5, {0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"cntrl", 5, {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"digit", 5, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"graph", 5, {0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                  0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"lower", 5, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"print", 5, {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                  0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"punct", 5, {0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0x00, 0xfc, 0x01, 0x00, 0x00,
                  0xf8, 0x01, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"space", 5, {0x00, 0x3e, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"upper", 5, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff,
                  0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"word", 4, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0xfe, 0xff, 0xff,
                 0x87, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {"xdigit", 6, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0x7e, 0x00, 0x00,
                   0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {NULL /* Non-digits */, 0, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xfc, 0xff, 0xff, 0xff,
                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
    {NULL /* Non-whitespace */, 0, {0xff, 0xc1, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
    {NULL /* Non-word */, 0, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xfc, 0x01, 0x00, 0x00,
                              0x78, 0x01, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}};

#define N_BUILTIN_CHAR_CLASSES sizeof(builtin_char_classes) / sizeof(*builtin_char_classes)

#define BCC_DIGIT 5
#define BCC_WHITESPACE 10
#define BCC_WORD 12
#define BCC_NOT_DIGIT 14
#define BCC_NOT_WHITESPACE 15
#define BCC_NOT_WORD 16

#define BCC_TEST(index, character) bitmap_test(builtin_char_classes[index].bitmap, (character))

static void bitmap_set(unsigned char *bitmap, size_t index) {
  bitmap[index >> 3u] |= 1u << (index & 7u);
}

static void bitmap_union(unsigned char *bitmap, const unsigned char *other_bitmap, size_t size) {
  while (size--) {
    bitmap[size] |= other_bitmap[size];
  }
}

static int bitmap_test(unsigned char *bitmap, size_t index) {
  return (bitmap[index >> 3u] >> (index & 7u)) & 1u;
}

static void bitmap_clear(unsigned char *bitmap, size_t size) { memset(bitmap, 0, size); }

static int bitmap_size_for_bits(size_t bits) { return (bits + 7) / 8; }

static void *default_alloc(void *context, size_t size) {
  (void)context;
  return malloc(size);
}

static void default_free(void *context, void *pointer) {
  (void)context;
  free(pointer);
}

static const crex_allocator_t default_allocator = {NULL, default_alloc, default_free};

#define ALLOC(allocator, size) ((allocator)->alloc)((allocator)->context, size)

#define FREE(allocator, pointer) ((allocator)->free)((allocator)->context, pointer)

struct crex_regex {
  size_t size;
  unsigned char *bytecode;

  size_t n_groups;

  unsigned char *char_classes;

  void *allocator_context;
  void (*free)(void *, void *);
};

typedef union {
  size_t size;
  char *pointer;
} allocator_slot_t;

struct crex_context {
  size_t visited_size;
  unsigned char *visited;

  allocator_slot_t *buffer;
  size_t capacity;

  allocator_t allocator;
};

// FIXME: put these somewhere

static void safe_memcpy(void *destination, const void *source, size_t size) {
  assert((destination != NULL && source != NULL) || size == 0);

  if (size != 0) {
    memcpy(destination, source, size);
  }
}

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
  TT_CHAR_CLASS,
  TT_BUILTIN_CHAR_CLASS,
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
    unsigned char character;

    size_t char_class_index;

    anchor_type_t anchor_type;

    struct {
      size_t lower_bound, upper_bound;
    } repetition;
  } data;
} token_t;

typedef struct {
  size_t size;
  size_t capacity;
  unsigned char *buffer;
} char_class_buffer_t;

size_t slice_to_size(const char *begin, const char *end) {
  size_t result = 0;

  for (const char *it = begin; it != end; it++) {
    assert(isdigit(*it));

    // FIXME: can't assume ASCII compiler
    const unsigned int digit = *it - '0';

    if (result > (REPETITION_INFINITY - digit) / 10u) {
      return REPETITION_INFINITY;
    }

    result = 10 * result + digit;
  }

  return result;
}

WARN_UNUSED_RESULT static status_t lex_char_class(char_class_buffer_t *char_classes,
                                                  token_t *token,
                                                  const char **str,
                                                  const char *eof,
                                                  const allocator_t *allocator);

WARN_UNUSED_RESULT static int lex_escape_code(token_t *token, const char **str, const char *eof);

WARN_UNUSED_RESULT static status_t lex(char_class_buffer_t *char_classes,
                                       token_t *token,
                                       const char **str,
                                       const char *eof,
                                       const allocator_t *allocator) {
  assert(*str < eof);

  const unsigned char character = **str;
  (*str)++;

  switch (character) {
  case '^':
    token->type = TT_ANCHOR;
    token->data.anchor_type = AT_BOL;
    break;

  case '$':
    token->type = TT_ANCHOR;
    token->data.anchor_type = AT_EOL;
    break;

  case '|':
    token->type = TT_PIPE;
    break;

  case '*':
  case '+':
  case '?':
    if (*str < eof && **str == '?') {
      (*str)++;
      token->type = TT_LAZY_REPETITION;
    } else {
      token->type = TT_GREEDY_REPETITION;
    }

    switch (character) {
    case '*':
      token->data.repetition.lower_bound = 0;
      token->data.repetition.upper_bound = REPETITION_INFINITY;
      break;

    case '+':
      token->data.repetition.lower_bound = 1;
      token->data.repetition.upper_bound = REPETITION_INFINITY;
      break;

    case '?':
      token->data.repetition.lower_bound = 0;
      token->data.repetition.upper_bound = 1;
      break;
    }

    break;

  case '(':
    token->type = TT_OPEN_PAREN;
    break;

  case ')':
    token->type = TT_CLOSE_PAREN;
    break;

  case '[': {
    status_t status = lex_char_class(char_classes, token, str, eof, allocator);

    if (status != CREX_OK) {
      return status;
    }

    break;
  }

  case '{': {
    const char *lb_begin = *str;
    const char *lb_end;

    for (lb_end = lb_begin; lb_end != eof && isdigit(*lb_end); lb_end++) {
    }

    if (lb_end == lb_begin || lb_end == eof || (*lb_end != ',' && *lb_end != '}')) {
      token->type = TT_CHARACTER;
      token->data.character = '{';
      break;
    }

    if (*lb_end == '}') {
      (*str) = lb_end + 1;

      if (*str < eof && **str == '?') {
        (*str)++;
        token->type = TT_LAZY_REPETITION;
      } else {
        token->type = TT_GREEDY_REPETITION;
      }

      token->data.repetition.lower_bound = slice_to_size(lb_begin, lb_end);

      if (token->data.repetition.lower_bound == REPETITION_INFINITY) {
        return CREX_E_BAD_REPETITION;
      }

      token->data.repetition.upper_bound = token->data.repetition.lower_bound;

      break;
    }

    const char *ub_begin = lb_end + 1;
    const char *ub_end;

    for (ub_end = ub_begin; ub_end != eof && isdigit(*ub_end); ub_end++) {
    }

    if (ub_end == eof || *ub_end != '}') {
      token->type = TT_CHARACTER;
      token->data.character = '{';
      break;
    }

    (*str) = ub_end + 1;

    if (*str < eof && **str == '?') {
      (*str)++;
      token->type = TT_LAZY_REPETITION;
    } else {
      token->type = TT_GREEDY_REPETITION;
    }

    token->data.repetition.lower_bound = slice_to_size(lb_begin, lb_end);

    if (token->data.repetition.lower_bound == REPETITION_INFINITY) {
      return CREX_E_BAD_REPETITION;
    }

    if (ub_begin == ub_end) {
      token->data.repetition.upper_bound = REPETITION_INFINITY;
    } else {
      token->data.repetition.upper_bound = slice_to_size(ub_begin, ub_end);

      if (token->data.repetition.upper_bound == REPETITION_INFINITY) {
        return CREX_E_BAD_REPETITION;
      }

      if (token->data.repetition.lower_bound > token->data.repetition.upper_bound) {
        return CREX_E_BAD_REPETITION;
      }
    }

    break;
  }

  case '\\':
    if (!lex_escape_code(token, str, eof)) {
      return CREX_E_BAD_ESCAPE;
    }
    break;

  default:
    token->type = TT_CHARACTER;
    token->data.character = character;
  }

  return CREX_OK;
}

int lex_escape_code(token_t *token, const char **str, const char *eof) {
  assert(*str <= eof);

  if (*str == eof) {
    return 0;
  }

  unsigned char character = **str;
  (*str)++;

  token->type = TT_CHARACTER;

  switch (character) {
  case 'a':
    token->data.character = '\a';
    break;

  case 'f':
    token->data.character = '\f';
    break;

  case 'n':
    token->data.character = '\n';
    break;

  case 'r':
    token->data.character = '\r';
    break;

  case 't':
    token->data.character = '\t';
    break;

  case 'v':
    token->data.character = '\v';
    break;

  case 'A':
    token->type = TT_ANCHOR;
    token->data.anchor_type = AT_BOF;
    break;

  case 'z':
    token->type = TT_ANCHOR;
    token->data.anchor_type = AT_EOF;
    break;

  case 'b':
    token->type = TT_ANCHOR;
    token->data.anchor_type = AT_WORD_BOUNDARY;
    break;

  case 'B':
    token->type = TT_ANCHOR;
    token->data.anchor_type = AT_NOT_WORD_BOUNDARY;
    break;

  case 'x': {
    int value = 0;

    for (int i = 2; i--;) {
      if (*str == eof) {
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

    token->data.character = value;

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
    token->data.character = character;
    break;

  case 'd':
    token->type = TT_BUILTIN_CHAR_CLASS;
    token->data.char_class_index = BCC_DIGIT;
    break;

  case 'D':
    token->type = TT_BUILTIN_CHAR_CLASS;
    token->data.char_class_index = BCC_NOT_DIGIT;
    break;

  case 's':
    token->type = TT_BUILTIN_CHAR_CLASS;
    token->data.char_class_index = BCC_WHITESPACE;
    break;

  case 'S':
    token->type = TT_BUILTIN_CHAR_CLASS;
    token->data.char_class_index = BCC_NOT_WHITESPACE;
    break;

  case 'w':
    token->type = TT_BUILTIN_CHAR_CLASS;
    token->data.char_class_index = BCC_WORD;
    break;

  case 'W':
    token->type = TT_BUILTIN_CHAR_CLASS;
    token->data.char_class_index = BCC_NOT_WORD;
    break;

  default:
    return 0;
  }

  return 1;
}

static status_t lex_char_class(char_class_buffer_t *char_classes,
                               token_t *token,
                               const char **str,
                               const char *eof,
                               const allocator_t *allocator) {
  assert(char_classes->size <= char_classes->capacity);

  if (char_classes->size == char_classes->capacity) {
    const size_t capacity = 2 * char_classes->capacity + 1;
    unsigned char *buffer = ALLOC(allocator, 32 * capacity);

    if (buffer == NULL) {
      return CREX_E_NOMEM;
    }

    safe_memcpy(buffer, char_classes->buffer, 32 * char_classes->size);
    FREE(allocator, char_classes->buffer);

    char_classes->capacity = capacity;
    char_classes->buffer = buffer;
  }

  unsigned char *bitmap = char_classes->buffer + 32 * char_classes->size;
  memset(bitmap, 0, 32);

  assert(*str <= eof);

  if (*str == eof) {
    return CREX_E_BAD_CHARACTER_CLASS;
  }

  int inverted;

  if (**str == '^') {
    (*str)++;
    inverted = 1;
  } else {
    inverted = 0;
  }

  int prev_character = -1;
  int is_range = 0;

#define PUSH_CHAR(character)                                                                       \
  do {                                                                                             \
    if (is_range) {                                                                                \
      assert(prev_character != -1);                                                                \
      for (unsigned char i = prev_character + 1; i <= character; i++) {                            \
        bitmap_set(bitmap, i);                                                                     \
      }                                                                                            \
      prev_character = -1;                                                                         \
    } else {                                                                                       \
      bitmap_set(bitmap, character);                                                               \
      prev_character = character;                                                                  \
    }                                                                                              \
    is_range = 0;                                                                                  \
  } while (0)

  for (;;) {
    assert(*str <= eof);

    if (*str == eof) {
      return CREX_E_BAD_CHARACTER_CLASS;
    }

    unsigned char character = **str;
    (*str)++;

    if (character == ']') {
      break;
    }

    switch (character) {
    case '[': {
      const char *end;

      for (end = *str; end < eof; end++) {
        if (!isalpha(*end) && *end != ':') {
          break;
        }
      }

      if (end == eof || **str != ':' || *(end - 1) != ':' || *end != ']') {
        PUSH_CHAR('[');
        break;
      }

      if (is_range) {
        return CREX_E_BAD_CHARACTER_CLASS;
      }

      const char *name = *str + 1;
      const size_t size = (end - 1) - name;

      size_t i;

      for (i = 0; i < N_BUILTIN_CHAR_CLASSES; i++) {
        const builtin_char_class_t *bcc = &builtin_char_classes[i];

        if (size == bcc->name_size && memcmp(name, bcc->name, size) == 0) {
          break;
        }
      }

      if (i == N_BUILTIN_CHAR_CLASSES) {
        return CREX_E_BAD_CHARACTER_CLASS;
      }

      bitmap_union(bitmap, builtin_char_classes[i].bitmap, sizeof(bitmap));
      prev_character = -1;
      *str = end + 1;

      break;
    }

    case '\\':
      if (!lex_escape_code(token, str, eof)) {
        return CREX_E_BAD_ESCAPE;
      }

      switch (token->type) {
      case TT_CHARACTER:
        PUSH_CHAR(token->data.character);
        break;

      case TT_ANCHOR:
        return CREX_E_BAD_CHARACTER_CLASS;

      case TT_BUILTIN_CHAR_CLASS: {
        if (is_range) {
          return CREX_E_BAD_CHARACTER_CLASS;
        }

        const size_t index = token->data.char_class_index;
        const unsigned char *other_bitmap = builtin_char_classes[index].bitmap;
        bitmap_union(bitmap, other_bitmap, sizeof(bitmap));

        prev_character = -1;

        break;
      }

      default:
        assert(0);
      }

      break;

    case '-':
      if (is_range || prev_character == -1) {
        PUSH_CHAR('-');
      } else {
        is_range = 1;
      }
      break;

    default:
      PUSH_CHAR(character);
      break;
    }
  }

  if (inverted) {
    for (size_t i = 0; i < sizeof(bitmap); i++) {
      bitmap[i] = ~bitmap[i];
    }
  }

  for (size_t i = 0; i < N_BUILTIN_CHAR_CLASSES; i++) {
    if (memcmp(bitmap, builtin_char_classes[i].bitmap, 32) == 0) {
      token->type = TT_BUILTIN_CHAR_CLASS;
      token->data.char_class_index = i;
      return CREX_OK;
    }
  }

  token->type = TT_CHAR_CLASS;

  for (size_t i = 0; i < char_classes->size; i++) {
    if (memcmp(bitmap, char_classes->buffer + 32 * i, 32) == 0) {
      token->data.char_class_index = i;
      return CREX_OK;
    }
  }

  token->data.char_class_index = char_classes->size++;

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
  PT_CHAR_CLASS,
  PT_BUILTIN_CHAR_CLASS,
  PT_ANCHOR,
} parsetree_type_t;

/* FIXME: explain this */
static const size_t operator_precedence[] = {1, 0, 2, 2};

typedef struct parsetree {
  parsetree_type_t type;

  union {
    unsigned char character;

    size_t char_class_index;

    anchor_type_t anchor_type;

    struct parsetree *children[2];

    struct {
      size_t lower_bound, upper_bound;
      struct parsetree *child;
    } repetition;

    struct {
      size_t index;
      struct parsetree *child;
    } group;
  } data;
} parsetree_t;

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

static void destroy_parsetree(parsetree_t *tree, const allocator_t *allocator);

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

WARN_UNUSED_RESULT parsetree_t *parse(status_t *status,
                                      size_t *n_groups,
                                      unsigned char **char_classes_buffer,
                                      const char *str,
                                      size_t size,
                                      const crex_allocator_t *allocator) {
  const char *eof = str + size;

  tree_stack_t trees = {0, 0, NULL};
  operator_stack_t operators = {0, 0, NULL};

  // FIXME: figure out a better ownership story for char_classes
  char_class_buffer_t char_classes;
  char_classes.size = 0;
  char_classes.capacity = 0;
  char_classes.buffer = NULL;

#define CHECK_ERRORS(condition, code)                                                              \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      destroy_tree_stack(&trees, allocator);                                                       \
      FREE(allocator, operators.data);                                                             \
      FREE(allocator, char_classes.buffer);                                                        \
      *status = code;                                                                              \
      return NULL;                                                                                 \
    }                                                                                              \
  } while (0)

  {
    operator_t outer_group;
    outer_group.type = PT_GROUP;
    outer_group.data.group_index = 0;

    CHECK_ERRORS(push_operator(&trees, &operators, &outer_group, allocator), CREX_E_NOMEM);
    CHECK_ERRORS(push_empty(&trees, allocator), CREX_E_NOMEM);
  }

  *n_groups = 1;

  while (str != eof) {
    token_t token;
    const status_t lex_status = lex(&char_classes, &token, &str, eof, allocator);

    CHECK_ERRORS(lex_status == CREX_OK, lex_status);

    switch (token.type) {
    case TT_CHARACTER:
    case TT_CHAR_CLASS:
    case TT_BUILTIN_CHAR_CLASS:
    case TT_ANCHOR: {
      operator_t operator;
      operator.type = PT_CONCATENATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);

      parsetree_t *tree = ALLOC(allocator, sizeof(parsetree_t));
      CHECK_ERRORS(tree != NULL, CREX_E_NOMEM);

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

      CHECK_ERRORS(push_tree(&trees, tree, allocator), CREX_E_NOMEM);

      break;
    }

    case TT_PIPE: {
      operator_t operator;
      operator.type = PT_ALTERNATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);
      CHECK_ERRORS(push_empty(&trees, allocator), CREX_E_NOMEM);
      break;
    }

    case TT_GREEDY_REPETITION:
    case TT_LAZY_REPETITION: {
      operator_t operator;
      operator.type =(token.type == TT_GREEDY_REPETITION) ? PT_GREEDY_REPETITION
                                                          : PT_LAZY_REPETITION;
      operator.data.repetition.lower_bound = token.data.repetition.lower_bound;
      operator.data.repetition.upper_bound = token.data.repetition.upper_bound;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);
      break;
    }

    case TT_OPEN_PAREN: {
      operator_t operator;

      operator.type = PT_CONCATENATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);

      operator.type = PT_GROUP;
      operator.data.group_index =(*n_groups)++;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);

      CHECK_ERRORS(push_empty(&trees, allocator), CREX_E_NOMEM);
      break;
    }

    case TT_CLOSE_PAREN:
      while (operators.size > 0) {
        if (operators.data[operators.size - 1].type == PT_GROUP) {
          break;
        }

        CHECK_ERRORS(pop_operator(&trees, &operators, allocator), CREX_E_NOMEM);
      }

      CHECK_ERRORS(operators.size > 0, CREX_E_UNMATCHED_CLOSE_PAREN);

      assert(operators.data[operators.size - 1].type == PT_GROUP);

      CHECK_ERRORS(pop_operator(&trees, &operators, allocator), CREX_E_NOMEM);

      break;

    default:
      assert(0);
    }
  }

  while (operators.size > 0) {
    if (operators.data[operators.size - 1].type == PT_GROUP) {
      CHECK_ERRORS(operators.size == 1, CREX_E_UNMATCHED_OPEN_PAREN);
    }

    CHECK_ERRORS(pop_operator(&trees, &operators, allocator), CREX_E_NOMEM);
  }

  // FIXME: nomenclature
  *char_classes_buffer = char_classes.buffer;

  assert(trees.size == 1);

  parsetree_t *tree = trees.data[0];

  FREE(allocator, trees.data);
  FREE(allocator, operators.data);

  return tree;
}

static void destroy_parsetree(parsetree_t *tree, const allocator_t *allocator) {
  switch (tree->type) {
  case PT_EMPTY:
  case PT_CHARACTER:
  case PT_CHAR_CLASS:
  case PT_BUILTIN_CHAR_CLASS:
  case PT_ANCHOR:
    break;

  case PT_CONCATENATION:
  case PT_ALTERNATION:
    destroy_parsetree(tree->data.children[0], allocator);
    destroy_parsetree(tree->data.children[1], allocator);
    break;

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION:
    destroy_parsetree(tree->data.repetition.child, allocator);
    break;

  case PT_GROUP:
    destroy_parsetree(tree->data.group.child, allocator);
    break;

  default:
    assert(0);
  }

  FREE(allocator, tree);
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

  return push_tree(trees, tree, allocator);
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

/** Compiler **/

enum {
  VM_CHARACTER,
  VM_CHAR_CLASS,
  VM_BUILTIN_CHAR_CLASS,
  VM_ANCHOR_BOF,
  VM_ANCHOR_BOL,
  VM_ANCHOR_EOF,
  VM_ANCHOR_EOL,
  VM_ANCHOR_WORD_BOUNDARY,
  VM_ANCHOR_NOT_WORD_BOUNDARY,
  VM_JUMP,
  VM_SPLIT_PASSIVE,
  VM_SPLIT_EAGER,
  VM_WRITE_POINTER
};

enum { VM_OPERAND_SIZE_NONE, VM_OPERAND_SIZE_1, VM_OPERAND_SIZE_2, VM_OPERAND_SIZE_4 };

typedef struct {
  size_t size;
  unsigned char *bytecode;
} bytecode_t;

static size_t serialize_size(void *destination, size_t value) {
  assert(value <= 0xffffffffLU);

  if (value <= 0xffLU) {
    const uint8_t u8_value = value;
    safe_memcpy(destination, &u8_value, 1);
    return 1;
  }

  if (value <= 0xffffLU) {
    const uint16_t u16_value = value;
    safe_memcpy(destination, &u16_value, 2);
    return 2;
  }

  const uint32_t u32_value = value;
  safe_memcpy(destination, &u32_value, 4);
  return 4;
}

static size_t serialize_long(void *destination, long value) {
  assert(-2147483647 <= value && value <= 2147483647);

  if (-127 <= value && value <= 127) {
    const int8_t i8_value = value;
    safe_memcpy(destination, &i8_value, 1);
    return 1;
  }

  if (-32767 <= value && value <= 32767) {
    const int16_t i16_value = value;
    safe_memcpy(destination, &i16_value, 2);
    return 2;
  }

  const int32_t i32_value = value;
  safe_memcpy(destination, &i32_value, 4);
  return 4;
}

static void serialize_long_32(void *destination, long value) {
  assert(-2147483647 <= value && value <= 2147483647);

  const int32_t i32_value = value;
  safe_memcpy(destination, &i32_value, 4);
}

static void serialize_size_32(void *destination, size_t value) {
  assert(value <= 0xffffffffLU);

  const uint32_t u32_value = value;
  safe_memcpy(destination, &u32_value, 4);
}

static size_t deserialize_size(void *source, size_t size) {
  switch (size) {
  case 1: {
    uint8_t u8_value;
    safe_memcpy(&u8_value, source, 1);
    return u8_value;
  }

  case 2: {
    uint16_t u16_value;
    safe_memcpy(&u16_value, source, 2);
    return u16_value;
  }

  case 4: {
    uint32_t u32_value;
    safe_memcpy(&u32_value, source, 4);
    return u32_value;
  }

  default:
    assert(0);
    return 0;
  }
}

static long deserialize_long(void *source, size_t size) {
  switch (size) {
  case 1: {
    int8_t i8_value;
    safe_memcpy(&i8_value, source, 1);
    return i8_value;
  }

  case 2: {
    int16_t i16_value;
    safe_memcpy(&i16_value, source, 2);
    return i16_value;
  }

  case 4: {
    int32_t i32_value;
    safe_memcpy(&i32_value, source, 4);
    return i32_value;
  }

  default:
    assert(0);
    return 0;
  }
}

status_t compile(bytecode_t *result, parsetree_t *tree, const allocator_t *allocator) {
  switch (tree->type) {
  case PT_EMPTY:
    result->size = 0;
    result->bytecode = NULL;
    break;

  case PT_CHARACTER:
    result->size = 2;
    result->bytecode = ALLOC(allocator, 2);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    result->bytecode[0] = VM_CHARACTER;
    result->bytecode[1] = tree->data.character;

    break;

  case PT_CHAR_CLASS:
  case PT_BUILTIN_CHAR_CLASS:
    result->size = 5;
    result->bytecode = ALLOC(allocator, 5);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    if (tree->type == PT_CHAR_CLASS) {
      result->bytecode[0] = VM_CHAR_CLASS;
    } else {
      result->bytecode[0] = VM_BUILTIN_CHAR_CLASS;
    }

    serialize_size_32(result->bytecode + 1, tree->data.char_class_index);

    break;

  case PT_ANCHOR:
    result->size = 1;
    result->bytecode = ALLOC(allocator, 1);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    result->bytecode[0] = VM_ANCHOR_BOF + tree->data.anchor_type;

    break;

  case PT_CONCATENATION:
  case PT_ALTERNATION: {
    bytecode_t left, right;

    status_t status = compile(&left, tree->data.children[0], allocator);

    if (status != CREX_OK) {
      return status;
    }

    status = compile(&right, tree->data.children[1], allocator);

    if (status != CREX_OK) {
      FREE(allocator, left.bytecode);
      return status;
    }

    if (tree->type == PT_CONCATENATION) {
      result->size = left.size + right.size;
    } else {
      result->size = 1 + 4 + left.size + 1 + 4 + right.size;
    }

    result->bytecode = ALLOC(allocator, result->size);

    if (result->bytecode == NULL) {
      FREE(allocator, left.bytecode);
      FREE(allocator, right.bytecode);
      return CREX_E_NOMEM;
    }

    if (tree->type == PT_CONCATENATION) {
      safe_memcpy(result->bytecode, left.bytecode, left.size);
      safe_memcpy(result->bytecode + left.size, right.bytecode, right.size);
    } else {
      unsigned char* bytecode = result->bytecode;

      *(bytecode++) = VM_SPLIT_PASSIVE;

      serialize_long_32(bytecode, left.size + 1 + 4);
      bytecode += 4;

      safe_memcpy(bytecode, left.bytecode, left.size);
      bytecode += left.size;

      *(bytecode++) = VM_JUMP;

      serialize_long_32(bytecode, right.size);
      bytecode += 4;

      safe_memcpy(bytecode, right.bytecode, right.size);
    }

    FREE(allocator, left.bytecode);
    FREE(allocator, right.bytecode);

    break;
  }

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION: {
    bytecode_t child;

    status_t status = compile(&child, tree->data.repetition.child, allocator);

    if (status != CREX_OK) {
      return status;
    }

    const size_t lower_bound = tree->data.repetition.lower_bound;
    const size_t upper_bound = tree->data.repetition.upper_bound;

    assert(lower_bound <= upper_bound && lower_bound != REPETITION_INFINITY);

    result->size = lower_bound * child.size;

    if (upper_bound == REPETITION_INFINITY) {
      result->size += 1 + 4 + child.size + 1 + 4;
    } else {
      result->size += (upper_bound - lower_bound) * (1 + 4 + child.size);
    }

    result->bytecode = ALLOC(allocator, result->size);

    if (result->bytecode == NULL) {
      FREE(allocator, child.bytecode);
      return CREX_E_NOMEM;
    }

    unsigned char *bytecode = result->bytecode;

    for (size_t i = 0; i < lower_bound; i++) {
      safe_memcpy(bytecode, child.bytecode, child.size);
      bytecode += child.size;
    }

    if (upper_bound == REPETITION_INFINITY) {
      /*
       * ...
       * VM_SPLIT_{PASSIVE,EAGER} end
       * child:
       * <child bytecode>
       * VM_SPLIT_{EAGER,PASSIVE} child
       * end:
       */

      unsigned char forward_split_opcode, backward_split_opcode;

      if (tree->type == PT_GREEDY_REPETITION) {
        forward_split_opcode = VM_SPLIT_PASSIVE;
        backward_split_opcode = VM_SPLIT_EAGER;
      } else {
        forward_split_opcode = VM_SPLIT_EAGER;
        backward_split_opcode = VM_SPLIT_PASSIVE;
      }

      const long forward_split_delta = child.size + 1 + 4;
      const long backward_split_delta = -(child.size + 1 + 4);

      *(bytecode++) = forward_split_opcode;

      serialize_long_32(bytecode, forward_split_delta);
      bytecode += 4;

      safe_memcpy(bytecode, child.bytecode, child.size);
      bytecode += child.size;

      *(bytecode++) = backward_split_opcode;
      serialize_long_32(bytecode, backward_split_delta);
    } else {
      /*
       * ...
       * VM_SPLIT_{PASSIVE,EAGER} end
       * <child bytecode>
       * VM_SPLIT_{PASSIVE,EAGER} end
       * <child bytecode>
       * ...
       * VM_SPLIT_{PASSIVE,EAGER} end
       * <child bytecode>
       * end:
       */

      const unsigned char split_opcode =
          (tree->type == PT_GREEDY_REPETITION) ? VM_SPLIT_PASSIVE : VM_SPLIT_EAGER;

      for (size_t i = 0; i < upper_bound - lower_bound; i++) {
        const size_t offset = lower_bound * child.size + i * (1 + 4 + child.size);

        const size_t split_origin = offset + 1 + 4;
        const long split_delta = (long)result->size - (long)split_origin;

        bytecode[offset] = split_opcode;
        serialize_long_32(bytecode + offset + 1, split_delta);

        safe_memcpy(bytecode + offset + 1 + 4, child.bytecode, child.size);
      }
    }

    FREE(allocator, child.bytecode);

    break;
  }

  case PT_GROUP: {
    bytecode_t child;
    status_t status = compile(&child, tree->data.group.child, allocator);

    if (status != CREX_OK) {
      return status;
    }

    const size_t max_size = 1 + 4 + child.size + 1 + 4;

    result->bytecode = ALLOC(allocator, max_size);

    if (result->bytecode == NULL) {
      FREE(allocator, child.bytecode);
      return CREX_E_NOMEM;
    }

    unsigned char *bytecode = result->bytecode;

    *(bytecode ++) = VM_WRITE_POINTER;

    serialize_size_32(bytecode, 2 * tree->data.group.index);
    bytecode += 4;

    safe_memcpy(bytecode, child.bytecode, child.size);
    bytecode += child.size;

    *(bytecode ++) = VM_WRITE_POINTER;

    serialize_size_32(bytecode, 2 * tree->data.group.index + 1);
    bytecode += 4;

    result->size = bytecode - result->bytecode;

    FREE(allocator, child.bytecode);

    break;
  }

  default:
    assert(0);
  }

  return CREX_OK;
}

/** Instruction pointer (i.e. size_t) list with custom allocator **/

typedef size_t state_list_handle_t;

#define STATE_LIST_EMPTY (~(state_list_handle_t)0)

typedef struct {
  context_t *context;

  size_t n_pointers;
  size_t element_size;
  size_t max_capacity;

  size_t bump_allocator;
  state_list_handle_t freelist;

  state_list_handle_t head;
} state_list_t;

#define LIST_NEXT(list, handle) (*(size_t *)((list)->context->buffer + (handle)))
#define LIST_INSTR_POINTER(list, handle) (*(size_t *)((list)->context->buffer + (handle) + 1))
#define LIST_POINTER_BUFFER(list, handle) ((const char **)((list)->context->buffer + (handle) + 2))

static void
state_list_create(state_list_t *list, context_t *context, size_t n_pointers, size_t max_elements) {
  list->context = context;

  list->n_pointers = n_pointers;
  list->element_size = 2 + n_pointers;
  list->max_capacity = max_elements * list->element_size;

  list->bump_allocator = 0;
  list->freelist = STATE_LIST_EMPTY;

  list->head = STATE_LIST_EMPTY;
}

static state_list_handle_t state_list_alloc(state_list_t *list) {
  state_list_handle_t state;

#ifndef NDEBUG
  state = STATE_LIST_EMPTY;
#endif

  if (list->bump_allocator + list->element_size <= list->context->capacity) {
    state = list->bump_allocator;
    list->bump_allocator += list->element_size;
  } else if (list->freelist != STATE_LIST_EMPTY) {
    state = list->freelist;
    list->freelist = LIST_NEXT(list, state);
  } else {
    // FIXME: think about a better resize strategy?
    size_t capacity = 2 * list->context->capacity + list->element_size;

    if (capacity > list->max_capacity) {
      capacity = list->max_capacity;
    }

    allocator_slot_t *buffer =
        ALLOC(&list->context->allocator, sizeof(allocator_slot_t) * capacity);

    if (buffer == NULL) {
      return STATE_LIST_EMPTY;
    }

    safe_memcpy(buffer, list->context->buffer, sizeof(allocator_slot_t) * list->context->capacity);

    FREE(&list->context->allocator, list->context->buffer);

    list->context->buffer = buffer;
    list->context->capacity = capacity;

    assert(list->bump_allocator + list->element_size <= capacity);

    state = list->bump_allocator;
    list->bump_allocator += list->element_size;
  }

  assert(state + list->element_size <= list->context->capacity);
  assert(state % list->element_size == 0);

  return state;
}

static int state_list_push_initial_state(state_list_t *list, state_list_handle_t predecessor) {
  assert(predecessor < list->context->capacity || predecessor == STATE_LIST_EMPTY);

  const state_list_handle_t state = state_list_alloc(list);

  if (state == STATE_LIST_EMPTY) {
    return 0;
  }

  LIST_INSTR_POINTER(list, state) = 0;

  const char **pointer_buffer = LIST_POINTER_BUFFER(list, state);

  for (size_t i = 0; i < list->n_pointers; i++) {
    pointer_buffer[i] = NULL;
  }

  if (predecessor == STATE_LIST_EMPTY) {
    LIST_NEXT(list, state) = list->head;
    list->head = state;
  } else {
    LIST_NEXT(list, state) = LIST_NEXT(list, predecessor);
    LIST_NEXT(list, predecessor) = state;
  }

  return 1;
}

static int
state_list_push_copy(state_list_t *list, state_list_handle_t predecessor, size_t instr_pointer) {
  assert(predecessor < list->context->capacity);

  const state_list_handle_t state = state_list_alloc(list);

  if (state == STATE_LIST_EMPTY) {
    return 0;
  }

  LIST_INSTR_POINTER(list, state) = instr_pointer;

  const char **dest_pointer_buffer = LIST_POINTER_BUFFER(list, state);
  const char **source_pointer_buffer = LIST_POINTER_BUFFER(list, predecessor);
  safe_memcpy(dest_pointer_buffer, source_pointer_buffer, sizeof(char *) * list->n_pointers);

  LIST_NEXT(list, state) = LIST_NEXT(list, predecessor);
  LIST_NEXT(list, predecessor) = state;

  return 1;
}

static state_list_handle_t state_list_pop(state_list_t *list, state_list_handle_t predecessor) {
  assert(predecessor < list->context->capacity || predecessor == STATE_LIST_EMPTY);

  const state_list_handle_t state =
      (predecessor == STATE_LIST_EMPTY) ? list->head : LIST_NEXT(list, predecessor);
  assert(state < list->context->capacity);

  const state_list_handle_t successor = LIST_NEXT(list, state);

  if (predecessor == STATE_LIST_EMPTY) {
    list->head = successor;
  } else {
    LIST_NEXT(list, predecessor) = successor;
  }

  LIST_NEXT(list, state) = list->freelist;
  list->freelist = state;

  return successor;
}

#define MATCH_BOOLEAN
#include "executor.h"

#define MATCH_LOCATION
#include "executor.h"

#define MATCH_GROUPS
#include "executor.h"

/** Public API **/

regex_t *crex_compile(status_t *status, const char *pattern, size_t size) {
  return crex_compile_with_allocator(status, pattern, size, &default_allocator);
}

regex_t *crex_compile_str(status_t *status, const char *pattern) {
  return crex_compile(status, pattern, strlen(pattern));
}

regex_t *crex_compile_with_allocator(status_t *status,
                                     const char *pattern,
                                     size_t size,
                                     const crex_allocator_t *allocator) {
  regex_t *regex = ALLOC(allocator, sizeof(regex_t));

  if (regex == NULL) {
    *status = CREX_E_NOMEM;
    return NULL;
  }

  parsetree_t *tree =
      parse(status, &regex->n_groups, &regex->char_classes, pattern, size, allocator);

  if (tree == NULL) {
    FREE(allocator, regex);
    return NULL;
  }

  // bytecode_t is structurally a prefix of regex_t
  *status = compile((bytecode_t *)regex, tree, allocator);

  destroy_parsetree(tree, allocator);

  if (*status != CREX_OK) {
    FREE(allocator, regex);
    return NULL;
  }

  regex->allocator_context = allocator->context;
  regex->free = allocator->free;

  return regex;
}

context_t *crex_create_context(status_t *status) {
  return crex_create_context_with_allocator(status, &default_allocator);
}

context_t *crex_create_context_with_allocator(crex_status_t *status,
                                              const crex_allocator_t *allocator) {
  context_t *context = ALLOC(allocator, sizeof(context_t));

  if (context == NULL) {
    *status = CREX_E_NOMEM;
    return NULL;
  }

  context->allocator = *allocator;

  context->visited_size = 0;
  context->visited = NULL;

  context->buffer = NULL;
  context->capacity = 0;

  return context;
}

void crex_destroy_regex(regex_t *regex) {
  void *context = regex->allocator_context;
  regex->free(context, regex->bytecode);
  regex->free(context, regex->char_classes);
  regex->free(context, regex);
}

void crex_destroy_context(context_t *context) {
  const allocator_t *allocator = &context->allocator;
  FREE(allocator, context->visited);
  FREE(allocator, context->buffer);
  FREE(allocator, context);
}

size_t crex_regex_n_groups(const regex_t *regex) { return regex->n_groups; }

status_t
crex_is_match_str(int *is_match, context_t *context, const regex_t *regex, const char *str) {
  return crex_is_match(is_match, context, regex, str, strlen(str));
}

status_t
crex_find_str(crex_slice_t *match, context_t *context, const regex_t *regex, const char *str) {
  return crex_find(match, context, regex, str, strlen(str));
}

status_t crex_match_groups_str(crex_slice_t *matches,
                               context_t *context,
                               const regex_t *regex,
                               const char *str) {
  return crex_match_groups(matches, context, regex, str, strlen(str));
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

const char *status_to_str(status_t status) {
  switch (status) {
  case CREX_OK:
    return "CREX_OK";

  case CREX_E_NOMEM:
    return "CREX_E_NOMEM";

  case CREX_E_BAD_ESCAPE:
    return "CREX_E_BAD_ESCAPE";

  case CREX_E_BAD_REPETITION:
    return "CREX_E_BAD_REPETITION";

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

  case VM_BUILTIN_CHAR_CLASS:
    return "VM_BUILTIN_CHAR_CLASS";

  case VM_ANCHOR_BOF:
    return "VM_ANCHOR_BOF";

  case VM_ANCHOR_BOL:
    return "VM_ANCHOR_BOL";

  case VM_ANCHOR_EOF:
    return "VM_ANCHOR_EOF";

  case VM_ANCHOR_EOL:
    return "VM_ANCHOR_EOL";

  case VM_ANCHOR_WORD_BOUNDARY:
    return "VM_ANCHOR_WORD_BOUNDARY";

  case VM_ANCHOR_NOT_WORD_BOUNDARY:
    return "VM_ANCHOR_NOT_WORD_BOUNDARY";

  case VM_JUMP:
    return "VM_JUMP";

  case VM_SPLIT_PASSIVE:
    return "VM_SPLIT_PASSIVE";

  case VM_SPLIT_EAGER:
    return "VM_SPLIT_EAGER";

  case VM_WRITE_POINTER:
    return "VM_WRITE_POINTER";

  default:
    assert(0);
  }
}

void crex_debug_lex(const char *str, FILE *file) {
  const char *eof = str + strlen(str);

  char_class_buffer_t char_classes;
  char_classes.size = 0;
  char_classes.capacity = 0;
  char_classes.buffer = NULL;

  token_t token;

  while (str != eof) {
    const status_t status = lex(&char_classes, &token, &str, eof);

    if (status != CREX_OK) {
      fprintf(file, "Lex failed with status %s\n", status_to_str(status));
      return;
    }

    switch (token.type) {
    case TT_CHARACTER:
      fputs("TT_CHARACTER ", file);

      if (isprint(token.data.character)) {
        fputc(token.data.character, file);
      } else {
        fprintf(file, "0x%02x", 0xff & (int)token.data.character);
      }

      fputc('\n', file);

      break;

    case TT_CHAR_CLASS:
      fprintf(file, "TT_CHAR_CLASS %zu\n", token.data.char_class_index);
      break;

    case TT_BUILTIN_CHAR_CLASS: {
      const char *name = builtin_char_classes[token.data.char_class_index].name;

      fputs("TT_BUILTIN_CHAR_CLASS ", file);

      if (name != NULL) {
        fprintf(file, "[[:%s:]]\n", name);
      } else {
        fprintf(file, "%zu\n", token.data.char_class_index);
      }

      break;
    }

    case TT_ANCHOR:
      fprintf(file, "TT_ANCHOR %s\n", anchor_type_to_str(token.data.anchor_type));
      break;

    case TT_PIPE:
      fputs("TT_PIPE\n", file);
      break;

    case TT_GREEDY_REPETITION:
    case TT_LAZY_REPETITION: {
      const char *str =
          (token.type == TT_GREEDY_REPETITION) ? "TT_GREEDY_REPETITION" : "TT_LAZY_REPETITION";

      fprintf(file, "%s %zu ... ", str, token.data.repetition.lower_bound);

      if (token.data.repetition.upper_bound == REPETITION_INFINITY) {
        fputs("inf\n", file);
      } else {
        fprintf(file, "%zu\n", token.data.repetition.upper_bound);
      }

      break;
    }

    case TT_OPEN_PAREN:
      fputs("TT_OPEN_PAREN\n", file);
      break;

    case TT_CLOSE_PAREN:
      fputs("TT_CLOSE_PAREN\n", file);
      break;

    default:
      assert(0);
    }
  }

  free(char_classes.buffer);
}

static void crex_print_parsetree(const parsetree_t *tree, size_t depth, FILE *file) {
  for (size_t i = 0; i < depth; i++) {
    fputc(' ', file);
  }

  switch (tree->type) {
  case PT_EMPTY:
    fputs("(PT_EMPTY)", file);
    break;

  case PT_CHARACTER:
    fputs("(PT_CHARACTER ", file);

    if (isprint(tree->data.character)) {
      fprintf(file, "%c)", tree->data.character);
    } else {
      fprintf(file, "0x%02x)", 0xff & (int)tree->data.character);
    }

    break;

  case PT_BUILTIN_CHAR_CLASS: {
    const char *name = builtin_char_classes[tree->data.char_class_index].name;

    fputs("(PT_BUILTIN_CHAR_CLASS ", file);

    if (name != NULL) {
      fprintf(file, "[[:%s:]])", name);
    } else {
      fprintf(file, "%zu)", tree->data.char_class_index);
    }

    break;
  }

  case PT_ANCHOR:
    fprintf(file, "(PT_ANCHOR %s)", anchor_type_to_str(tree->data.anchor_type));
    break;

  case PT_CONCATENATION:
    fputs("(PT_CONCATENATION\n", file);
    crex_print_parsetree(tree->data.children[0], depth + 1, file);
    fputc('\n', file);
    crex_print_parsetree(tree->data.children[1], depth + 1, file);
    fputc(')', file);
    break;

  case PT_ALTERNATION:
    fputs("(PT_ALTERNATION\n", file);
    crex_print_parsetree(tree->data.children[0], depth + 1, file);
    fputc('\n', file);
    crex_print_parsetree(tree->data.children[1], depth + 1, file);
    fputc(')', file);
    break;

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION: {
    const char *str =
        (tree->type == PT_GREEDY_REPETITION) ? "PT_GREEDY_REPETITION" : "PT_LAZY_REPETITION";

    fprintf(file, "(%s %zu ", str, tree->data.repetition.lower_bound);

    if (tree->data.repetition.upper_bound == REPETITION_INFINITY) {
      fputs("inf\n", file);
    } else {
      fprintf(file, "%zu\n", tree->data.repetition.upper_bound);
    }

    crex_print_parsetree(tree->data.repetition.child, depth + 1, file);
    fputc(')', file);

    break;
  }

  case PT_GROUP:
    fprintf(file, "(PT_GROUP %zu\n", tree->data.group.index);
    crex_print_parsetree(tree->data.group.child, depth + 1, file);
    fputc(')', file);
    break;

  default:
    assert(0);
  }
}

void crex_debug_parse(const char *str, FILE *file) {
  status_t status;

  size_t n_groups;

  unsigned char *char_classes;

  parsetree_t *tree = parse(&status, &n_groups, &char_classes, str, strlen(str));

  free(char_classes);

  if (status != CREX_OK) {
    fprintf(file, "Parse failed with status %s\n", status_to_str(status));
    return;
  }

  crex_print_parsetree(tree, 0, file);
  fputc('\n', file);

  destroy_parsetree(tree);
}

void crex_debug_compile(const char *str, FILE *file) {
  status_t status;

  size_t n_groups;
  unsigned char *char_classes;

  parsetree_t *tree = parse(&status, &n_groups, &char_classes, str, strlen(str));

  free(char_classes);

  if (status != CREX_OK) {
    fprintf(file, "Parse failed with status %s\n", status_to_str(status));
    return;
  }

  bytecode_t result;
  status = compile(&result, tree);

  destroy_parsetree(tree);

  if (status != CREX_OK) {
    fprintf(file, "Compilation failed with status %s\n", status_to_str(status));
    return;
  }

  for (size_t i = 0; i < result.size; i++) {
    const unsigned char code = result.bytecode[i];

    fprintf(file, "%05zd %s ", i, crex_vm_code_to_str(code));

    if (code == VM_JUMP || code == VM_SPLIT_PASSIVE || code == VM_SPLIT_EAGER) {
      const size_t origin = i + 1 + 4;
      const long delta = deserialize_long(result.bytecode + i + 1, 4);
      const size_t destination = origin + delta;
      fprintf(file, "%ld (=> %zu)\n", delta, destination);
      i += 4;
    } else if (code == VM_CHARACTER) {
      fprintf(file, "%c\n", result.bytecode[i + 1]);
      i++;
    } else if (code == VM_WRITE_POINTER || code == VM_BUILTIN_CHAR_CLASS) {
      fprintf(file, "%ld\n", deserialize_long(result.bytecode + i + 1, 4)); // FIXME: signedness
      i += 4;
    } else {
      fputc('\n', file);
    }
  }

  fprintf(file, "%05zd\n ", result.size);

  free(result.bytecode);
}

#endif
