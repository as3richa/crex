// For MAP_ANONYMOUS
#define _GNU_SOURCE

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crex.h"

#if defined(__x86_64__) && !defined(NO_NATIVE_COMPILER)

#include <sys/mman.h>
#include <unistd.h>

// It's spelled MAP_ANON on Darwin
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define NATIVE_COMPILER

#endif

typedef crex_status_t status_t;

#define WARN_UNUSED_RESULT CREX_WARN_UNUSED_RESULT

#define REPETITION_INFINITY SIZE_MAX
#define NON_CAPTURING SIZE_MAX

// For brevity
typedef crex_regex_t regex_t;

/** Character class plumbing **/

#define CHAR_CLASS_BITMAP_SIZE 32

typedef struct {
  unsigned char bitmap[CHAR_CLASS_BITMAP_SIZE];
} char_class_t;

typedef struct {
  const char *name;
  size_t name_size;
  unsigned char bitmap[CHAR_CLASS_BITMAP_SIZE];
} builtin_char_class_t;

static builtin_char_class_t builtin_classes[] = {
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
                              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}},
    {NULL /* Dot */, 0, {0xff, 0xfb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}};

#define N_BUILTIN_CLASSES sizeof(builtin_classes) / sizeof(*builtin_classes)

#define BCC_DIGIT 5
#define BCC_WHITESPACE 10
#define BCC_WORD 12
#define BCC_NOT_DIGIT 14
#define BCC_NOT_WHITESPACE 15
#define BCC_NOT_WORD 16
#define BCC_ANY 17

#define BCC_TEST(index, character) bitmap_test(builtin_classes[index].bitmap, (character))

static void bitmap_set(unsigned char *bitmap, size_t index) {
  bitmap[index >> 3u] |= 1u << (index & 7u);
}

static void bitmap_union(unsigned char *bitmap, const unsigned char *other_bitmap, size_t size) {
  while (size--) {
    bitmap[size] |= other_bitmap[size];
  }
}

static int bitmap_test(const unsigned char *bitmap, size_t index) {
  return (bitmap[index >> 3u] >> (index & 7u)) & 1u;
}

static void bitmap_clear(unsigned char *bitmap, size_t size) {
  memset(bitmap, 0, size);
}

static size_t bitmap_size_for_bits(size_t bits) {
  return (bits + 7) / 8;
}

/** Allocator **/

typedef crex_allocator_t allocator_t;

static void *default_alloc(void *context, size_t size) {
  (void)context;
  return malloc(size);
}

static void default_free(void *context, void *pointer) {
  (void)context;
  free(pointer);
}

static const allocator_t default_allocator = {NULL, default_alloc, default_free};

#define ALLOC(allocator, size) ((allocator)->alloc)((allocator)->context, size)
#define FREE(allocator, pointer) ((allocator)->free)((allocator)->context, pointer)

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

typedef crex_context_t context_t;

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
  TT_NON_CAPTURING_OPEN_PAREN,
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
  char_class_t *buffer;
} char_classes_t;

WARN_UNUSED_RESULT static size_t parse_size(const char *begin, const char *end);

WARN_UNUSED_RESULT static status_t lex_char_class(char_classes_t *classes,
                                                  token_t *token,
                                                  const char **pattern,
                                                  const char *eof,
                                                  const allocator_t *allocator);

WARN_UNUSED_RESULT static int
lex_escape_code(token_t *token, const char **pattern, const char *eof);

WARN_UNUSED_RESULT static status_t lex(char_classes_t *classes,
                                       token_t *token,
                                       const char **pattern,
                                       const char *eof,
                                       const allocator_t *allocator) {
  assert(*pattern < eof);

  const unsigned char character = **pattern;
  (*pattern)++;

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
    if (*pattern < eof && **pattern == '?') {
      (*pattern)++;
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
    if (*pattern < eof - 1 && **pattern == '?' && *((*pattern) + 1) == ':') {
      (*pattern) += 2;
      token->type = TT_NON_CAPTURING_OPEN_PAREN;
    } else {
      token->type = TT_OPEN_PAREN;
    }

    break;

  case ')':
    token->type = TT_CLOSE_PAREN;
    break;

  case '.':
    token->type = TT_BUILTIN_CHAR_CLASS;
    token->data.char_class_index = BCC_ANY;
    break;

  case '[': {
    status_t status = lex_char_class(classes, token, pattern, eof, allocator);

    if (status != CREX_OK) {
      return status;
    }

    break;
  }

  case '{': {
    const char *lb_begin = *pattern;
    const char *lb_end;

    for (lb_end = lb_begin; lb_end != eof && isdigit(*lb_end); lb_end++) {
    }

    if (lb_end == lb_begin || lb_end == eof || (*lb_end != ',' && *lb_end != '}')) {
      token->type = TT_CHARACTER;
      token->data.character = '{';
      break;
    }

    if (*lb_end == '}') {
      (*pattern) = lb_end + 1;

      if (*pattern < eof && **pattern == '?') {
        (*pattern)++;
        token->type = TT_LAZY_REPETITION;
      } else {
        token->type = TT_GREEDY_REPETITION;
      }

      token->data.repetition.lower_bound = parse_size(lb_begin, lb_end);

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

    (*pattern) = ub_end + 1;

    if (*pattern < eof && **pattern == '?') {
      (*pattern)++;
      token->type = TT_LAZY_REPETITION;
    } else {
      token->type = TT_GREEDY_REPETITION;
    }

    token->data.repetition.lower_bound = parse_size(lb_begin, lb_end);

    if (token->data.repetition.lower_bound == REPETITION_INFINITY) {
      return CREX_E_BAD_REPETITION;
    }

    if (ub_begin == ub_end) {
      token->data.repetition.upper_bound = REPETITION_INFINITY;
    } else {
      token->data.repetition.upper_bound = parse_size(ub_begin, ub_end);

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
    if (!lex_escape_code(token, pattern, eof)) {
      return CREX_E_BAD_ESCAPE;
    }
    break;

  default:
    token->type = TT_CHARACTER;
    token->data.character = character;
  }

  return CREX_OK;
}

int lex_escape_code(token_t *token, const char **pattern, const char *eof) {
  assert(*pattern <= eof);

  if (*pattern == eof) {
    return 0;
  }

  unsigned char character = **pattern;
  (*pattern)++;

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
      if (*pattern == eof) {
        return CREX_E_BAD_ESCAPE;
      }

      const char hex_byte = *((*pattern)++);

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

static status_t lex_char_class(char_classes_t *classes,
                               token_t *token,
                               const char **pattern,
                               const char *eof,
                               const allocator_t *allocator) {
  assert(classes->size <= classes->capacity);

  if (classes->size == classes->capacity) {
    const size_t capacity = 2 * classes->capacity + 1;
    char_class_t *buffer = ALLOC(allocator, sizeof(char_class_t) * capacity);

    if (buffer == NULL) {
      return CREX_E_NOMEM;
    }

    safe_memcpy(buffer, classes->buffer, sizeof(char_class_t) * classes->size);
    FREE(allocator, classes->buffer);

    classes->capacity = capacity;
    classes->buffer = buffer;
  }

  unsigned char *bitmap = classes->buffer[classes->size].bitmap;
  bitmap_clear(bitmap, CHAR_CLASS_BITMAP_SIZE);

  assert(*pattern <= eof);

  if (*pattern == eof) {
    return CREX_E_BAD_CHARACTER_CLASS;
  }

  int inverted;

  if (**pattern == '^') {
    (*pattern)++;
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
    assert(*pattern <= eof);

    if (*pattern == eof) {
      return CREX_E_BAD_CHARACTER_CLASS;
    }

    unsigned char character = **pattern;
    (*pattern)++;

    if (character == ']') {
      break;
    }

    switch (character) {
    case '[': {
      const char *end;

      for (end = *pattern; end < eof; end++) {
        if (!isalpha(*end) && *end != ':') {
          break;
        }
      }

      if (end == eof || **pattern != ':' || *(end - 1) != ':' || *end != ']') {
        PUSH_CHAR('[');
        break;
      }

      if (is_range) {
        return CREX_E_BAD_CHARACTER_CLASS;
      }

      const char *name = *pattern + 1;
      const size_t size = (end - 1) - name;

      size_t i;

      for (i = 0; i < N_BUILTIN_CLASSES; i++) {
        const char *class_name = builtin_classes[i].name;
        const size_t class_name_size = builtin_classes[i].name_size;

        if (size == class_name_size && memcmp(name, class_name, size) == 0) {
          break;
        }
      }

      if (i == N_BUILTIN_CLASSES) {
        return CREX_E_BAD_CHARACTER_CLASS;
      }

      bitmap_union(bitmap, builtin_classes[i].bitmap, CHAR_CLASS_BITMAP_SIZE);
      prev_character = -1;
      *pattern = end + 1;

      break;
    }

    case '\\':
      if (!lex_escape_code(token, pattern, eof)) {
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
        const unsigned char *other_bitmap = builtin_classes[index].bitmap;

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

  for (size_t i = 0; i < N_BUILTIN_CLASSES; i++) {
    if (memcmp(bitmap, builtin_classes[i].bitmap, CHAR_CLASS_BITMAP_SIZE) == 0) {
      token->type = TT_BUILTIN_CHAR_CLASS;
      token->data.char_class_index = i;
      return CREX_OK;
    }
  }

  token->type = TT_CHAR_CLASS;

  for (size_t i = 0; i < classes->size; i++) {
    if (memcmp(bitmap, classes->buffer[i].bitmap, CHAR_CLASS_BITMAP_SIZE) == 0) {
      token->data.char_class_index = i;
      return CREX_OK;
    }
  }

  token->data.char_class_index = classes->size++;

  return CREX_OK;
}

size_t parse_size(const char *begin, const char *end) {
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

WARN_UNUSED_RESULT static parsetree_t *parse(status_t *status,
                                             size_t *n_capturing_groups,
                                             char_classes_t *classes,
                                             const char *str,
                                             size_t size,
                                             const allocator_t *allocator) {
  const char *eof = str + size;

  tree_stack_t trees = {0, 0, NULL};
  operator_stack_t operators = {0, 0, NULL};

#define CHECK_ERRORS(condition, code)                                                              \
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

    CHECK_ERRORS(push_operator(&trees, &operators, &outer_group, allocator), CREX_E_NOMEM);
    CHECK_ERRORS(push_empty(&trees, allocator), CREX_E_NOMEM);
  }

  *n_capturing_groups = 1;

  while (str != eof) {
    token_t token;
    const status_t lex_status = lex(classes, &token, &str, eof, allocator);

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

      if (!push_tree(&trees, tree, allocator)) {
        FREE(allocator, tree);
        CHECK_ERRORS(0, CREX_E_NOMEM);
      }

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

    case TT_OPEN_PAREN:
    case TT_NON_CAPTURING_OPEN_PAREN: {
      operator_t operator;

      operator.type = PT_CONCATENATION;
      CHECK_ERRORS(push_operator(&trees, &operators, &operator, allocator), CREX_E_NOMEM);

      operator.type = PT_GROUP;
      operator.data.group_index =(token.type == TT_OPEN_PAREN) ? (*n_capturing_groups)++
                                                               : NON_CAPTURING;
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
  VM_SPLIT_BACKWARDS_PASSIVE,
  VM_SPLIT_BACKWARDS_EAGER,
  VM_WRITE_POINTER
};

#define VM_OP(opcode, operand_size) ((opcode) | ((operand_size) << 5))

#define VM_OPCODE(byte) ((byte)&31u)

#define VM_OPERAND_SIZE(byte) ((byte) >> 5u)

typedef struct {
  size_t size;
  unsigned char *bytecode;
} bytecode_t;

static size_t size_for_operand(size_t operand) {
  assert(operand <= 0xffffffffLU);

  if (operand == 0) {
    return 0;
  }

  if (operand <= 0xffLU) {
    return 1;
  }

  if (operand <= 0xffffLU) {
    return 2;
  }

  return 4;
}

static void serialize_operand(void *destination, size_t operand, size_t size) {
  switch (size) {
  case 0: {
    assert(operand == 0);
    break;
  }

  case 1: {
    assert(operand <= 0xffLU);
    uint8_t u8_operand = operand;
    safe_memcpy(destination, &u8_operand, 1);
    break;
  }

  case 2: {
    assert(operand <= 0xffffLU);
    uint16_t u16_operand = operand;
    safe_memcpy(destination, &u16_operand, 2);
    break;
  }

  case 4: {
    assert(operand <= 0xffffffffLU);
    uint32_t u32_operand = operand;
    safe_memcpy(destination, &u32_operand, 4);
    break;
  }

  default:
    assert(0);
  }
}

static size_t deserialize_operand(void *source, size_t size) {
  switch (size) {
  case 0:
    return 0;

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
  }

  assert(0);
  return 0;
}

static void serialize_operand_le(void *destination, size_t operand, size_t size) {
  unsigned char *bytes = destination;

  switch (size) {
  case 0: {
    assert(operand == 0);
    break;
  }

  case 1: {
    assert(operand <= 0xffLU);
    bytes[0] = operand;
    break;
  }

  case 2: {
    assert(operand <= 0xffffLU);
    bytes[0] = operand & 0xffu;
    bytes[1] = operand >> 8u;
    break;
  }

  case 4: {
    assert(operand <= 0xffffffffLU);
    bytes[0] = operand & 0xffu;
    bytes[1] = (operand >> 8u) & 0xffu;
    bytes[2] = (operand >> 16u) & 0xffu;
    bytes[3] = (operand >> 24u) & 0xffu;
    break;
  }

  default:
    assert(0);
  }
}

static size_t deserialize_operand_le(void *source, size_t size) {
  unsigned char *bytes = source;

  switch (size) {
  case 0:
    return 0;

  case 1:
    return bytes[0];

  case 2: {
    size_t operand = bytes[0];
    operand |= ((size_t)bytes[1]) << 8u;
    return operand;
  }

  case 4: {
    size_t operand = bytes[0];
    operand |= ((size_t)bytes[1]) << 8u;
    operand |= ((size_t)bytes[2]) << 16u;
    operand |= ((size_t)bytes[3]) << 24u;
    return operand;
  }

  default:
    assert(0);
    return 0;
  }
}

WARN_UNUSED_RESULT static status_t
compile(bytecode_t *result, parsetree_t *tree, const allocator_t *allocator) {
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

    result->bytecode[0] = VM_OP(VM_CHARACTER, 1);
    result->bytecode[1] = tree->data.character;

    break;

  case PT_CHAR_CLASS:
  case PT_BUILTIN_CHAR_CLASS: {
    const size_t index = tree->data.char_class_index;
    const size_t operand_size = size_for_operand(index);

    result->size = 1 + operand_size;
    result->bytecode = ALLOC(allocator, result->size);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    const unsigned char opcode =
        (tree->type == PT_CHAR_CLASS) ? VM_CHAR_CLASS : VM_BUILTIN_CHAR_CLASS;

    *result->bytecode = VM_OP(opcode, operand_size);
    serialize_operand(result->bytecode + 1, index, operand_size);

    break;
  }

  case PT_ANCHOR:
    result->size = 1;
    result->bytecode = ALLOC(allocator, 1);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    *result->bytecode = VM_ANCHOR_BOF + tree->data.anchor_type;

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

    size_t max_size;

    if (tree->type == PT_CONCATENATION) {
      max_size = left.size + right.size;
    } else {
      max_size = 1 + 4 + left.size + 1 + 4 + right.size;
    }

    unsigned char *bytecode = ALLOC(allocator, max_size);

    if (bytecode == NULL) {
      FREE(allocator, left.bytecode);
      FREE(allocator, right.bytecode);
      return CREX_E_NOMEM;
    }

    result->bytecode = bytecode;

    if (tree->type == PT_CONCATENATION) {
      safe_memcpy(bytecode, left.bytecode, left.size);
      bytecode += left.size;

      safe_memcpy(bytecode, right.bytecode, right.size);

      result->size = left.size + right.size;
    } else {
      const size_t jump_delta = right.size;
      const size_t jump_operand_size = size_for_operand(jump_delta);

      const size_t split_delta = left.size + 1 + jump_operand_size;
      const size_t split_operand_size = size_for_operand(split_delta);

      *(bytecode++) = VM_OP(VM_SPLIT_PASSIVE, split_operand_size);

      serialize_operand(bytecode, split_delta, split_operand_size);
      bytecode += split_operand_size;

      safe_memcpy(bytecode, left.bytecode, left.size);
      bytecode += left.size;

      *(bytecode++) = VM_OP(VM_JUMP, jump_operand_size);

      serialize_operand(bytecode, jump_delta, jump_operand_size);
      bytecode += jump_operand_size;

      safe_memcpy(bytecode, right.bytecode, right.size);
      bytecode += right.size;

      result->size = bytecode - result->bytecode;
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

    size_t max_size = lower_bound * child.size;

    if (upper_bound == REPETITION_INFINITY) {
      max_size += 1 + 4 + child.size + 1 + 4;
    } else {
      max_size += (upper_bound - lower_bound) * (1 + 4 + child.size);
    }

    result->bytecode = ALLOC(allocator, max_size);

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

      unsigned char leading_split_opcode, trailing_split_opcode;

      if (tree->type == PT_GREEDY_REPETITION) {
        leading_split_opcode = VM_SPLIT_PASSIVE;
        trailing_split_opcode = VM_SPLIT_BACKWARDS_EAGER;
      } else {
        leading_split_opcode = VM_SPLIT_EAGER;
        trailing_split_opcode = VM_SPLIT_BACKWARDS_PASSIVE;
      }

      size_t trailing_split_delta;
      size_t trailing_split_operand_size;

      // Because the source address for a jump or split is the address immediately after the
      // operand, in the case of a backwards split, the magnitude of the delta changes with the
      // operand size. In particular, a smaller operand implies a smaller magnitude. In order to
      // select the smallest possible operand in all cases, we have to compute the required delta
      // for a given operand size, then check if the delta does in fact fit. I would factor this out
      // into its own function, but this is the only instance of a backwards jump in the compiler

      {
        size_t delta;
        size_t operand_size;

        for (operand_size = 1; operand_size <= 4; operand_size *= 2) {
          delta = child.size + 1 + operand_size;

          if (size_for_operand(delta) <= operand_size) {
            break;
          }
        }

        trailing_split_delta = delta;
        trailing_split_operand_size = operand_size;
      }

      const size_t leading_split_delta = child.size + 1 + trailing_split_operand_size;
      const size_t leading_split_operand_size = size_for_operand(leading_split_delta);

      *(bytecode++) = VM_OP(leading_split_opcode, leading_split_operand_size);

      serialize_operand(bytecode, leading_split_delta, leading_split_operand_size);
      bytecode += leading_split_operand_size;

      safe_memcpy(bytecode, child.bytecode, child.size);
      bytecode += child.size;

      *(bytecode++) = VM_OP(trailing_split_opcode, trailing_split_operand_size);

      serialize_operand(bytecode, trailing_split_delta, trailing_split_operand_size);
      bytecode += trailing_split_operand_size;

      result->size = bytecode - result->bytecode;
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

      // In general, we don't know the minimum size of the operands of the earlier splits until
      // we've determined the minimum size of the operands of the later splits. If we compile the
      // later splits first (i.e. by filling the buffer from right to left), we can pick the correct
      // size a priori, and then perform a single copy to account for any saved space

      // In general, this optimization leaves an unbounded amount of memory allocated but unused at
      // the end of the buffer; however, because the top level parsetree is necessarily a PT_GROUP,
      // we always free it before yielding the final compiled regex

      unsigned char *end = result->bytecode + max_size;
      unsigned char *begin = end;

      for (size_t i = 0; i < upper_bound - lower_bound; i++) {
        begin -= child.size;
        safe_memcpy(begin, child.bytecode, child.size);

        const long delta = end - begin;
        const size_t operand_size = size_for_operand(delta);

        begin -= operand_size;
        serialize_operand(begin, delta, operand_size);

        *(--begin) = VM_OP(split_opcode, operand_size);
      }

      assert(bytecode <= begin);

      if (bytecode < begin) {
        result->size = max_size - (begin - bytecode);
        memmove(bytecode, begin, end - begin);
      } else {
        result->size = max_size;
      }
    }

    FREE(allocator, child.bytecode);

    break;
  }

  case PT_GROUP: {
    if (tree->data.group.index == NON_CAPTURING) {
      return compile(result, tree->data.group.child, allocator);
    }

    bytecode_t child;
    status_t status = compile(&child, tree->data.group.child, allocator);

    if (status != CREX_OK) {
      return status;
    }

    const size_t leading_index = 2 * tree->data.group.index;
    const size_t trailing_index = leading_index + 1;

    const size_t leading_operand_size = size_for_operand(leading_index);
    const size_t trailing_operand_size = size_for_operand(trailing_index);

    result->size = 1 + leading_operand_size + child.size + 1 + trailing_operand_size;
    result->bytecode = ALLOC(allocator, result->size);

    if (result->bytecode == NULL) {
      FREE(allocator, child.bytecode);
      return CREX_E_NOMEM;
    }

    unsigned char *bytecode = result->bytecode;

    *(bytecode++) = VM_OP(VM_WRITE_POINTER, leading_operand_size);

    serialize_operand(bytecode, leading_index, leading_operand_size);
    bytecode += leading_operand_size;

    safe_memcpy(bytecode, child.bytecode, child.size);
    bytecode += child.size;

    *(bytecode++) = VM_OP(VM_WRITE_POINTER, trailing_operand_size);

    serialize_operand(bytecode, trailing_index, trailing_operand_size);
    bytecode += trailing_operand_size;

    assert(result->size == (size_t)(bytecode - result->bytecode));

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
    assert(list->context->capacity < list->max_capacity);

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

struct crex_regex {
  size_t size;
  size_t n_capturing_groups;
  size_t n_classes;
  size_t max_concurrent_states;

  unsigned char *bytecode;

  char_class_t *classes;

  void *allocator_context;
  void (*free)(void *, void *);

#ifdef NATIVE_COMPILER
  unsigned char *native_code;
  size_t native_code_size;
#endif
};

static status_t reserve(context_t *context, const regex_t *regex, size_t n_pointers) {
  const size_t min_visited_size = bitmap_size_for_bits(regex->size);

  if (context->visited_size < min_visited_size) {
    unsigned char *visited = ALLOC(&context->allocator, min_visited_size);

    if (visited == NULL) {
      return CREX_E_NOMEM;
    }

    FREE(&context->allocator, context->visited);

    context->visited_size = min_visited_size;
    context->visited = visited;
  }

  const size_t element_size = 2 + n_pointers;
  const size_t max_capacity = regex->max_concurrent_states * element_size;

  if (context->capacity < max_capacity) {
    allocator_slot_t *buffer = ALLOC(&context->allocator, sizeof(allocator_slot_t) * max_capacity);

    if (buffer == NULL) {
      return CREX_E_NOMEM;
    }

    FREE(&context->allocator, context->buffer);

    context->capacity = max_capacity;
    context->buffer = buffer;
  }

  return CREX_OK;
}

#ifdef NATIVE_COMPILER

typedef struct {
  size_t size;
  size_t capacity;
  unsigned char *buffer;
} native_code_t;

static size_t get_page_size(void) {
  const long size = sysconf(_SC_PAGE_SIZE);
  return (size < 0) ? 4096 : size;
}

static unsigned char *my_mremap(unsigned char *buffer, size_t old_size, size_t size) {
#ifndef NDEBUG
  const size_t page_size = get_page_size();
  assert(old_size % page_size == 0 && size % page_size == 0);
#endif

  // "Shrink" an existing mapping by unmapping its tail
  if (size <= old_size) {
    munmap(buffer + size, old_size - size);
    return buffer;
  }

  const int prot = PROT_READ | PROT_WRITE;
  const int flags = MAP_ANONYMOUS | MAP_PRIVATE;

  const size_t delta = size - old_size;

  // First, try to "extend" the existing mapping by creating a new mapping immediately following
  unsigned char *next = mmap(buffer + old_size, delta, prot, flags, -1, 0);

  // ENOMEM, probably
  if (next == MAP_FAILED) {
    return NULL;
  }

  // If in fact the new mapping was created immediately after the old mapping, we're done
  if (next == buffer + old_size) {
    return buffer;
  }

  // Otherwise, we need to create an entirely new, sufficiently-large mapping
  munmap(next, delta);
  next = mmap(NULL, size, prot, flags, -1, 0);

  if (next == MAP_FAILED) {
    return NULL;
  }

  // Copy the old data and destroy the old mapping
  memcpy(next, buffer, old_size);
  munmap(buffer, old_size);

  return next;
}

// x64 instruction encoding

#define REX(w, r, x, b) (0x40u | ((w) << 3u) | ((r) << 2u) | ((x) << 1u) | (b))

#define MOD_REG_RM(mod, reg, rm) (((mod) << 6u) | ((reg) << 3u) | (rm))

#define SIB(scale, index, base) (((scale) << 6u) | ((index) << 3u) | (base))

typedef enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 } reg_t;

typedef enum { SCALE_1, SCALE_2, SCALE_4, SCALE_8 } scale_t;

typedef struct {
  unsigned int rip_relative : 1;
  unsigned int base : 4;
  unsigned int has_index : 1;
  unsigned int scale : 2;
  unsigned int index : 4;
  long displacement;
} indirect_operand_t;

#define INDIRECT_REG(reg) ((indirect_operand_t){0, reg, 0, 0, 0, 0})

#define INDIRECT_RD(reg, displacement) ((indirect_operand_t){0, reg, 0, 0, 0, displacement})

#define INDIRECT_BSXD(base, scale, index, displacement)                                            \
  ((indirect_operand_t){0, base, 1, scale, index, displacement})

#define INDIRECT_RIP(displacement) ((indirect_operand_t){1, 0, 0, 0, 0, displacement})

static unsigned char *reserve_native_code(native_code_t *code, size_t size) {
  if (code->size + size <= code->capacity) {
    return code->buffer + code->size;
  }

  assert(code->size + size <= 2 * code->capacity);

  unsigned char *buffer = my_mremap(code->buffer, code->capacity, 2 * code->capacity);

  if (buffer == MAP_FAILED) {
    return NULL;
  }

  code->buffer = buffer;
  code->capacity *= 2;

  return code->buffer + code->size;
}

static void resize(native_code_t *code, unsigned char *end) {
  code->size = end - code->buffer;
}

static void copy_displacement(unsigned char *destination, long value, size_t size);

static size_t encode_rex_r(unsigned char *data, int rex_w, int rex_r, reg_t rm) {
  const int rex_b = rm >> 3u;

  if (rex_w || rex_r || rex_b) {
    *data = REX(rex_w, rex_r, 0, rex_b);
    return 1;
  }

  return 0;
}

static size_t encode_mod_reg_rm_r(unsigned char *data, size_t reg_or_extension, reg_t rm) {
  *data = MOD_REG_RM(3u, reg_or_extension & 7u, rm & 7u);
  return 1;
}

static size_t encode_rex_m(unsigned char *data, int rex_w, int rex_r, indirect_operand_t rm) {
  const int rex_x = (rm.has_index) ? (rm.index >> 3u) : 0;
  const int rex_b = (rm.rip_relative) ? 0 : rm.base >> 3u;

  if (rex_w || rex_r || rex_x || rex_b) {
    *data = REX(rex_w, rex_r, rex_x, rex_b);
    return 1;
  }

  return 0;
}

static size_t
encode_mod_reg_rm_m(unsigned char *data, size_t reg_or_extension, indirect_operand_t rm) {
  size_t displacement_size;
  unsigned char mod;

  // [rbp], [r13], [rbp + reg], and [r13 + reg] can't be encoded with a zero-size displacement
  // (because the corresponding bit patterns are used for RIP-relative addressing in the former case
  // and displacement-only mode in the latter). We can encode these forms as e.g. [rbp + 0], with an
  // 8-bit displacement instead

  if (rm.rip_relative) {
    displacement_size = 4;
    mod = 0;
  } else if (rm.displacement == 0 && rm.base != RBP && rm.base != R13) {
    displacement_size = 0;
    mod = 0;
  } else if (-128 <= rm.displacement && rm.displacement <= 127) {
    displacement_size = 1;
    mod = 1;
  } else {
    assert(-2147483648 <= rm.displacement && rm.displacement <= 2147483647);
    displacement_size = 4;
    mod = 2;
  }

  if (rm.rip_relative) {
    *(data++) = MOD_REG_RM(0, reg_or_extension & 7u, RBP);
    copy_displacement(data, rm.displacement, displacement_size);

    return 1 + displacement_size;
  }

  if (rm.has_index || rm.base == RSP) {
    // Can't use RSP as the index register in a SIB byte
    assert(rm.index != RSP);

    *(data++) = MOD_REG_RM(mod, reg_or_extension & 7u, 0x04);
    *(data++) = SIB(rm.scale, rm.index & 7u, rm.base & 7u);
    copy_displacement(data, rm.displacement, displacement_size);

    return 2 + displacement_size;
  }

  *(data++) = MOD_REG_RM(mod, reg_or_extension, rm.base);
  copy_displacement(data, rm.displacement, displacement_size);

  return 1 + displacement_size;
}

// FIXME: sane name
static void copy_displacement(unsigned char *destination, long value, size_t size) {
  assert(size == 0 || size == 1 || size == 4);

  if (size == 0) {
    return;
  }

  if (size == 1) {
    assert(-128 <= value && value <= 127);
    *destination = value;
  }

  assert(-2147483648 <= value && value <= 2147483647);

  // Should get optimized down to a mov on x64
  unsigned long unsigned_value = value;
  destination[0] = unsigned_value & 0xffu;
  destination[1] = (unsigned_value >> 8u) & 0xffu;
  destination[2] = (unsigned_value >> 16u) & 0xffu;
  destination[3] = (unsigned_value >> 24u) & 0xffu;
}

#include "build/x64.h"

#define X_SCRATCH RAX
#define X_RESULT RDX
#define X_VISITED RBX
#define X_CHARACTER RCX
#define X_PREV_CHARACTER RSI
#define X_CHAR_CLASSES RDI
#define X_BUILTIN_CHAR_CLASSES R8

WARN_UNUSED_RESULT static status_t compile_to_native(regex_t *regex, size_t n_instructions) {
  const size_t page_size = get_page_size();

  native_code_t native_code;
  native_code.size = 0;

  // We preallocate the buffer because starting with a single-page allocation simplifies
  // reallocation slightly
  native_code.capacity = page_size;
  native_code.buffer = my_mremap(NULL, 0, native_code.capacity);

  if (native_code.buffer == NULL) {
    return CREX_E_NOMEM;
  }

#define ASM0(id)                                                                                   \
  do {                                                                                             \
    if (!id(&native_code)) {                                                                       \
      my_mremap(native_code.buffer, native_code.capacity, 0);                                      \
      return CREX_E_NOMEM;                                                                         \
    }                                                                                              \
  } while (0)

#define ASM1(id, x)                                                                                \
  do {                                                                                             \
    if (!id(&native_code, x)) {                                                                    \
      my_mremap(native_code.buffer, native_code.capacity, 0);                                      \
      return CREX_E_NOMEM;                                                                         \
    }                                                                                              \
  } while (0)

#define ASM2(id, x, y)                                                                             \
  do {                                                                                             \
    if (!id(&native_code, x, y)) {                                                                 \
      my_mremap(native_code.buffer, native_code.capacity, 0);                                      \
      return CREX_E_NOMEM;                                                                         \
    }                                                                                              \
  } while (0)

#define BRANCH(instr)                                                                              \
  instr;                                                                                           \
  size_t branch_origin = native_code.size

#define BRANCH_TARGET()                                                                            \
  do {                                                                                             \
    assert(native_code.size - branch_origin <= 127);                                               \
    native_code.buffer[branch_origin - 1] = native_code.size - branch_origin;                      \
  } while (0)

  for (size_t i = 0, instr_index = 0; i < regex->size; instr_index++) {
    const unsigned char byte = regex->bytecode[i++];

    const unsigned char opcode = VM_OPCODE(byte);
    const size_t operand_size = VM_OPERAND_SIZE(byte);

    const size_t operand = deserialize_operand(regex->bytecode + i, operand_size);
    i += operand_size;
    (void)operand; // FIXME

    if (n_instructions <= 64) {
      assert(instr_index <= 64);

      if (instr_index <= 32) {
        ASM2(bts32_reg_u8, X_VISITED, instr_index);
      } else {
        ASM2(bts64_reg_u8, X_VISITED, instr_index);
      }
    } else {
      ASM2(bts32_mem_u8, INDIRECT_RD(X_VISITED, 4 * (instr_index / 32)), instr_index % 32);
    }

    {
      BRANCH(ASM1(jnc_i8, 0));
      ASM0(ret);
      BRANCH_TARGET();
    }

    switch (opcode) {
    case VM_CHARACTER: {
      assert(operand <= 255);
      ASM2(cmp8_reg_u8, X_CHARACTER, operand);
      BRANCH(ASM1(jne_i8, 0));
      ASM0(ret);
      BRANCH_TARGET();
      break;
    }

    case VM_CHAR_CLASS:
    case VM_BUILTIN_CHAR_CLASS: {
      assert(opcode != VM_CHAR_CLASS || operand < regex->n_classes);
      assert(opcode != VM_BUILTIN_CHAR_CLASS || operand < N_BUILTIN_CLASSES);

      // The bitmap bit corresponding to character k can be found in the (k / 32)th dword. Need
      // movzx because mov doesn't necessarily zero the high-order bits
      ASM2(movzx8to64_reg_reg, X_SCRATCH, X_CHARACTER);
      ASM2(shr8_reg_u8, X_SCRATCH, 5);

      const reg_t base_register =
          (opcode == VM_CHAR_CLASS) ? X_CHAR_CLASSES : X_BUILTIN_CHAR_CLASSES;

      const indirect_operand_t dword =
          INDIRECT_BSXD(base_register, SCALE_4, X_SCRATCH, 32 * operand);

      // bt takes the index operand modulo the operand size, so we don't have to compute it
      // explicitly
      ASM2(bt32_mem_reg, dword, X_CHARACTER);
      break;
    }

    case VM_ANCHOR_BOF:
      break;

    case VM_ANCHOR_BOL:
      break;

    case VM_ANCHOR_EOF:
      break;

    case VM_ANCHOR_EOL:
      break;

    case VM_ANCHOR_WORD_BOUNDARY:
      break;

    case VM_ANCHOR_NOT_WORD_BOUNDARY:
      break;

    case VM_JUMP:
      break;

    case VM_SPLIT_PASSIVE:
      break;

    case VM_SPLIT_EAGER:
      break;

    case VM_SPLIT_BACKWARDS_PASSIVE:
      break;

    case VM_SPLIT_BACKWARDS_EAGER:
      break;

    case VM_WRITE_POINTER:
      break;

    default:
      assert(0);
    }
  }

  assert(native_code.capacity % page_size == 0);

  const size_t n_pages = native_code.capacity / page_size;
  const size_t used_pages = (native_code.size + page_size - 1) / page_size;

  munmap(native_code.buffer + page_size * used_pages, page_size * (n_pages - used_pages));

  regex->native_code = native_code.buffer;
  regex->native_code_size = native_code.size;

  return CREX_OK;
}

#endif

/** Public API **/

#if defined(__GNUC__) || defined(__clang__)
#define PUBLIC __attribute__((visibility("default")))
#else
#define PUBLIC
#endif

// For brevity
typedef crex_match_t match_t;

PUBLIC regex_t *crex_compile(status_t *status, const char *pattern, size_t size) {
  return crex_compile_with_allocator(status, pattern, size, &default_allocator);
}

PUBLIC regex_t *crex_compile_str(status_t *status, const char *pattern) {
  return crex_compile(status, pattern, strlen(pattern));
}

PUBLIC regex_t *crex_compile_with_allocator(status_t *status,
                                            const char *pattern,
                                            size_t size,
                                            const allocator_t *allocator) {
  regex_t *regex = ALLOC(allocator, sizeof(regex_t));

  if (regex == NULL) {
    *status = CREX_E_NOMEM;
    return NULL;
  }

  char_classes_t classes = {0, 0, NULL};

  parsetree_t *tree = parse(status, &regex->n_capturing_groups, &classes, pattern, size, allocator);

  if (tree == NULL) {
    FREE(allocator, classes.buffer);
    FREE(allocator, regex);
    return NULL;
  }

  bytecode_t bytecode;

  *status = compile(&bytecode, tree, allocator);

  destroy_parsetree(tree, allocator);

  if (*status != CREX_OK) {
    FREE(allocator, classes.buffer);
    FREE(allocator, regex);
    return NULL;
  }

  regex->size = bytecode.size;
  regex->bytecode = bytecode.bytecode;

  regex->n_classes = classes.size;
  regex->classes = classes.buffer;

  // At the start of an iteration of the executor's outer loop, every element of the state set has
  // an instruction pointer corresponding to the instruction immediately following a VM_CHARACTER,
  // VM_CHAR_CLASS, or VM_BUILTIN_CHAR_CLASS instruction. Moreover, executing a given instruction
  // causes at most one new state to be enqueued, and each instruction is executed at most once in
  // any given iteration. Finally, the start state may also be enqueued at some point in the outer
  // loop. This gives us an upper bound on the total number of states that can be enqueued
  // concurrently in the executor

  // NB. that we may enqueue new states with the same instruction pointer as already-enqueued states
  // (so we can't tighten this bound by e.g. only counting instructions that can be jumped to)

  size_t n_charish_instructions = 0;
  size_t n_instructions = 0;

  for (size_t i = 0; i < regex->size;) {
    n_instructions++;

    switch (VM_OPCODE(regex->bytecode[i])) {
    case VM_CHARACTER:
    case VM_CHAR_CLASS:
    case VM_BUILTIN_CHAR_CLASS:
      n_charish_instructions++;
      break;
    }

    i += 1 + VM_OPERAND_SIZE(regex->bytecode[i]);
  }

  regex->max_concurrent_states = n_charish_instructions + n_instructions + 1;

  // Stash the free part of the allocator, so it doesn't need to be passed into crex_regex_destroy
  regex->allocator_context = allocator->context;
  regex->free = allocator->free;

#ifdef NATIVE_COMPILER

  *status = compile_to_native(regex, n_instructions);

  if (*status != CREX_OK) {
    FREE(allocator, regex->classes);
    FREE(allocator, regex->bytecode);
    FREE(allocator, regex);
    return NULL;
  }

#endif

  return regex;
}

PUBLIC context_t *crex_create_context(status_t *status) {
  return crex_create_context_with_allocator(status, &default_allocator);
}

PUBLIC context_t *crex_create_context_with_allocator(crex_status_t *status,
                                                     const allocator_t *allocator) {
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

PUBLIC size_t crex_regex_n_capturing_groups(const regex_t *regex) {
  return regex->n_capturing_groups;
}

PUBLIC void crex_destroy_regex(regex_t *regex) {
  void *context = regex->allocator_context;
  regex->free(context, regex->bytecode);
  regex->free(context, regex->classes);
  regex->free(context, regex);
}

PUBLIC void crex_destroy_context(context_t *context) {
  const allocator_t *allocator = &context->allocator;
  FREE(allocator, context->visited);
  FREE(allocator, context->buffer);
  FREE(allocator, context);
}

#define MATCH_BOOLEAN
#include "executor.h" // crex_is_match

PUBLIC status_t crex_is_match_str(int *is_match,
                                  context_t *context,
                                  const regex_t *regex,
                                  const char *str) {
  return crex_is_match(is_match, context, regex, str, strlen(str));
}

#define MATCH_LOCATION
#include "executor.h" // crex_find

PUBLIC status_t crex_find_str(match_t *match,
                              context_t *context,
                              const regex_t *regex,
                              const char *str) {
  return crex_find(match, context, regex, str, strlen(str));
}

#define MATCH_GROUPS
#include "executor.h" // crex_match_groups

PUBLIC status_t crex_match_groups_str(match_t *matches,
                                      context_t *context,
                                      const regex_t *regex,
                                      const char *str) {
  return crex_match_groups(matches, context, regex, str, strlen(str));
}

PUBLIC status_t crex_context_reserve_is_match(context_t *context, const regex_t *regex) {
  return reserve(context, regex, 0);
}

PUBLIC status_t crex_context_reserve_find(context_t *context, const regex_t *regex) {
  return reserve(context, regex, 2);
}

PUBLIC status_t crex_context_reserve_match_groups(context_t *context, const regex_t *regex) {
  return reserve(context, regex, 2 * regex->n_capturing_groups);
}

PUBLIC unsigned char *crex_dump_regex(status_t *status, size_t *size, const regex_t *regex) {
  return crex_dump_regex_with_allocator(status, size, regex, &default_allocator);
}

PUBLIC unsigned char *crex_dump_regex_with_allocator(status_t *status,
                                                     size_t *size,
                                                     const regex_t *regex,
                                                     const allocator_t *allocator) {
  const size_t classes_size = sizeof(char_class_t) * regex->n_classes;

  *size = 5 + 16 + regex->size + classes_size;

  unsigned char *buffer = ALLOC(allocator, *size);

  unsigned char *buf = buffer;

  if (buffer == NULL) {
    *status = CREX_E_NOMEM;
    return NULL;
  }

  // Magic number
  safe_memcpy(buffer, "crex", 4);
  buffer += 4;

  // Version byte
  *(buffer++) = 0;

  // Scalar elements
  serialize_operand_le(buffer, regex->size, 4);
  serialize_operand_le(buffer + 4, regex->n_capturing_groups, 4);
  serialize_operand_le(buffer + 8, regex->n_classes, 4);
  serialize_operand_le(buffer + 12, regex->max_concurrent_states, 4);
  buffer += 16;

  // Bytecode. Need to re-serialize each operand into little endian byte order

  unsigned char *bytecode = regex->bytecode;
  unsigned char *end = bytecode + regex->size;

  while (bytecode != end) {
    const size_t operand_size = VM_OPERAND_SIZE(*bytecode);

    *(buffer++) = *(bytecode++);

    const size_t operand = deserialize_operand(bytecode, operand_size);
    serialize_operand_le(buffer, operand, operand_size);

    buffer += operand_size;
    bytecode += operand_size;
  }

  // Character classes. NB> a character class is just a blob of byes
  safe_memcpy(buffer, regex->classes, classes_size);
  buffer += classes_size;

  assert(buffer == buf + *size);

  return buf;
}

PUBLIC regex_t *crex_load_regex(status_t *status, unsigned char *buffer, size_t size) {
  return crex_load_regex_with_allocator(status, buffer, size, &default_allocator);
}

PUBLIC regex_t *crex_load_regex_with_allocator(status_t *status,
                                               unsigned char *buffer,
                                               size_t size,
                                               const allocator_t *allocator) {
#ifndef NDEBUG
  // For a sanity check
  unsigned char *buf = buffer;
#endif

  // At minimum, the buffer must contain the magic number, version byte, and scalars
  if (size < 5 + 16) {
    // FIXME
    return NULL;
  }

  regex_t *regex = ALLOC(allocator, sizeof(regex_t));

  // Magic number
  if (memcmp(buffer, "crex", 4) != 0) {
    // FIXME
    return NULL;
  }
  buffer += 4;

  // Version byte
  if (*(buffer++) != 0) {
    // FIXME
    return NULL;
  }

  // Scalars
  regex->size = deserialize_operand_le(buffer, 4);
  regex->n_capturing_groups = deserialize_operand_le(buffer + 4, 4);
  regex->n_classes = deserialize_operand_le(buffer + 8, 4);
  regex->max_concurrent_states = deserialize_operand_le(buffer + 12, 4);
  buffer += 16;

  // Bytecode. Need to parse each operand as a little-endian number, then re-serialize it as a
  // native integer

  regex->bytecode = ALLOC(allocator, regex->size);

  if (regex->bytecode == NULL) {
    *status = CREX_E_NOMEM;
    FREE(allocator, regex);
    return NULL;
  }

  unsigned char *bytecode = regex->bytecode;
  unsigned char *end = bytecode + regex->size;

  while (bytecode != end) {
    const size_t operand_size = VM_OPERAND_SIZE(*buffer);

    *(bytecode++) = *(buffer++);

    const size_t operand = deserialize_operand_le(buffer, operand_size);
    serialize_operand(bytecode, operand, operand_size);

    bytecode += operand_size;
    buffer += operand_size;
  }

  // Character classes

  const size_t classes_size = sizeof(char_class_t) * regex->n_classes;

  regex->classes = ALLOC(allocator, classes_size);

  if (regex->classes == NULL) {
    *status = CREX_E_NOMEM;
    FREE(allocator, regex->bytecode);
    FREE(allocator, regex);
  }

  safe_memcpy(regex->classes, buffer, classes_size);
  buffer += classes_size;

  assert(buffer == buf + size);

  *status = CREX_OK;
  return regex;
}

#ifndef NDEBUG

#include <stdio.h>

static const char *anchor_type_to_str(anchor_type_t type) {
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

const char *opcode_to_str(unsigned char opcode) {
  switch (opcode) {
  case VM_CHARACTER:
    return "VM_CHARACTER";

  case VM_CHAR_CLASS:
    return "VM_CHAR_CLASS";

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

  case VM_SPLIT_BACKWARDS_PASSIVE:
    return "VM_SPLIT_BACKWARDS_PASSIVE";

  case VM_SPLIT_BACKWARDS_EAGER:
    return "VM_SPLIT_BACKWARDS_EAGER";

  case VM_WRITE_POINTER:
    return "VM_WRITE_POINTER";

  default:
    assert(0);
    return NULL;
  }
}

static void print_char_class_bitmap(const unsigned char *bitmap, FILE *file) {
  fputc('[', file);

  for (int c = 0; c <= 255;) {
    if (!bitmap_test(bitmap, c)) {
      c++;
      continue;
    }

    int d;

    for (d = c; (d + 1) <= 255 && bitmap_test(bitmap, d + 1); d++) {
    }

    if (c == '-' || c == '[' || c == ']' || c == '^') {
      fprintf(file, "\\%c", c);
    } else if (isprint(c) || c == ' ') {
      fputc(c, file);
    } else {
      fprintf(file, "\\x%02x", c);
    }

    if (c != d) {
      fputc('-', file);

      if (d == '-' || d == '[' || d == ']' || d == '^') {
        fprintf(file, "\\%c", d);
      } else if (isprint(d) || d == ' ') {
        fputc(d, file);
      } else {
        fprintf(file, "\\x%02x", d);
      }
    }

    c = d + 1;
  }

  fputc(']', file);
}

static void print_char_class(const char_class_t *classes, size_t index, FILE *file) {
  print_char_class_bitmap(classes[index].bitmap, file);
}

static void print_builtin_char_class(size_t index, FILE *file) {
  print_char_class_bitmap(builtin_classes[index].bitmap, file);
}

status_t crex_print_tokenization(const char *pattern, size_t size, FILE *file) {
  const char *eof = pattern + size;

  char_classes_t classes = {0, 0, NULL};

  token_t token;

  while (pattern != eof) {
    const status_t status = lex(&classes, &token, &pattern, eof, &default_allocator);

    if (status != CREX_OK) {
      free(classes.buffer);
      return status;
    }

    switch (token.type) {
    case TT_CHARACTER:
      fputs("TT_CHARACTER ", file);

      if (isprint(token.data.character)) {
        fputc(token.data.character, file);
      } else {
        fprintf(file, "'\\x%02x'", (unsigned int)token.data.character);
      }

      fputc('\n', file);

      break;

    case TT_CHAR_CLASS:
      fputs("TT_CHAR_CLASS ", file);
      print_char_class(classes.buffer, token.data.char_class_index, file);
      fputc('\n', file);
      break;

    case TT_BUILTIN_CHAR_CLASS: {
      fputs("TT_BUILTIN_CHAR_CLASS ", file);
      print_builtin_char_class(token.data.char_class_index, file);
      fputc('\n', file);
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

      fprintf(file, "%s %zu .. ", str, token.data.repetition.lower_bound);

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

    case TT_NON_CAPTURING_OPEN_PAREN:
      fputs("TT_NON_CAPTURING_OPEN_PAREN\n", file);
      break;

    case TT_CLOSE_PAREN:
      fputs("TT_CLOSE_PAREN\n", file);
      break;

    default:
      assert(0);
    }
  }

  free(classes.buffer);

  return CREX_OK;
}

static void
print_parsetree(const parsetree_t *tree, size_t depth, const char_classes_t *classes, FILE *file);

status_t crex_print_parsetree(const char *pattern, size_t size, FILE *file) {
  status_t status;

  size_t n_capturing_groups;

  char_classes_t classes = {0, 0, NULL};

  parsetree_t *tree =
      parse(&status, &n_capturing_groups, &classes, pattern, size, &default_allocator);

  if (tree == NULL) {
    free(classes.buffer);
    return status;
  }

  print_parsetree(tree, 0, &classes, file);
  fputc('\n', file);

  free(classes.buffer);
  destroy_parsetree(tree, &default_allocator);

  return CREX_OK;
}

static void
print_parsetree(const parsetree_t *tree, size_t depth, const char_classes_t *classes, FILE *file) {
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
      fprintf(file, "'%c')", tree->data.character);
    } else {
      fprintf(file, "'\\x%02x')", (unsigned int)tree->data.character);
    }

    break;

  case PT_CHAR_CLASS:
    fputs("(PT_CHAR_CLASS ", file);
    print_char_class(classes->buffer, tree->data.char_class_index, file);
    fputc(')', file);
    break;

  case PT_BUILTIN_CHAR_CLASS: {
    fputs("(PT_BUILTIN_CHAR_CLASS ", file);
    print_builtin_char_class(tree->data.char_class_index, file);
    fputc(')', file);
    break;
  }

  case PT_ANCHOR:
    fprintf(file, "(PT_ANCHOR %s)", anchor_type_to_str(tree->data.anchor_type));
    break;

  case PT_CONCATENATION:
    fputs("(PT_CONCATENATION\n", file);
    print_parsetree(tree->data.children[0], depth + 1, classes, file);
    fputc('\n', file);
    print_parsetree(tree->data.children[1], depth + 1, classes, file);
    fputc(')', file);
    break;

  case PT_ALTERNATION:
    fputs("(PT_ALTERNATION\n", file);
    print_parsetree(tree->data.children[0], depth + 1, classes, file);
    fputc('\n', file);
    print_parsetree(tree->data.children[1], depth + 1, classes, file);
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

    print_parsetree(tree->data.repetition.child, depth + 1, classes, file);
    fputc(')', file);

    break;
  }

  case PT_GROUP:
    fprintf(file, "(PT_GROUP ");

    if (tree->data.group.index == NON_CAPTURING) {
      fputs("<non-capturing>\n", file);
    } else {
      fprintf(file, "%zu\n", tree->data.group.index);
    }

    print_parsetree(tree->data.group.child, depth + 1, classes, file);

    fputc(')', file);

    break;

  default:
    assert(0);
  }
}

void crex_print_bytecode(const regex_t *regex, FILE *file) {
  unsigned char *bytecode = regex->bytecode;

  for (;;) {
    const size_t i = bytecode - regex->bytecode;
    assert(i <= regex->size);

    fprintf(file, "%05zd", i);

    if (i == regex->size) {
      fputc('\n', file);
      break;
    }

    const unsigned char byte = *(bytecode++);

    const unsigned char opcode = VM_OPCODE(byte);

    const size_t operand_size = VM_OPERAND_SIZE(byte);

    const size_t operand = deserialize_operand(bytecode, operand_size);
    bytecode += operand_size;

    fprintf(file, " %s", opcode_to_str(opcode));

    switch (opcode) {
    case VM_CHARACTER: {
      assert(operand <= 0xffu);

      if (isprint(operand) || operand == ' ') {
        fprintf(file, " '%c'\n", (char)operand);
      } else {
        fprintf(file, " \\x%02zx\n", operand);
      }

      break;
    }

    case VM_CHAR_CLASS:
    case VM_BUILTIN_CHAR_CLASS: {
      fputc(' ', file);

      if (opcode == VM_CHAR_CLASS) {
        print_char_class(regex->classes, operand, file);
      } else {
        print_builtin_char_class(operand, file);
      }

      fputc('\n', file);

      break;
    }

    case VM_JUMP:
    case VM_SPLIT_PASSIVE:
    case VM_SPLIT_EAGER:
    case VM_SPLIT_BACKWARDS_PASSIVE:
    case VM_SPLIT_BACKWARDS_EAGER: {
      size_t destination;

      if (opcode == VM_SPLIT_BACKWARDS_PASSIVE || opcode == VM_SPLIT_BACKWARDS_EAGER) {
        destination = i - operand;
      } else {
        destination = i + operand;
      }

      assert(destination < regex->size);

      fprintf(file, " %zu (=> %zu)\n", operand, destination);

      break;
    }

    case VM_WRITE_POINTER: {
      fprintf(file, " %zu\n", operand);
      break;
    }

    default:
      assert(operand_size == 0);
      fputc('\n', file);
    }
  }
}

#ifdef NATIVE_COMPILER

void crex_dump_native_code(const regex_t *regex, FILE *file) {
  // FIXME: check IO errors in this and other debug functions?
  fwrite(regex->native_code, 1, regex->native_code_size, file);
}

#endif

#endif
