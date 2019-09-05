#include <ctype.h>

#define REPETITION_INFINITY SIZE_MAX

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

typedef struct {
  size_t size;
  size_t capacity;
  char_class_t *buffer;
} char_classes_t;

WARN_UNUSED_RESULT static size_t str_to_size(const char *begin, const char *end);

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

      token->data.repetition.lower_bound = str_to_size(lb_begin, lb_end);

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

    token->data.repetition.lower_bound = str_to_size(lb_begin, lb_end);

    if (token->data.repetition.lower_bound == REPETITION_INFINITY) {
      return CREX_E_BAD_REPETITION;
    }

    if (ub_begin == ub_end) {
      token->data.repetition.upper_bound = REPETITION_INFINITY;
    } else {
      token->data.repetition.upper_bound = str_to_size(ub_begin, ub_end);

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

WARN_UNUSED_RESULT static int
lex_escape_code(token_t *token, const char **pattern, const char *eof) {
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

      const char heR_byte = *((*pattern)++);

      int digit;

      if ('0' <= heR_byte && heR_byte <= '9') {
        digit = heR_byte - '0';
      } else if ('a' <= heR_byte && heR_byte <= 'f') {
        digit = heR_byte - 'a' + 0xa;
      } else if ('A' <= heR_byte && heR_byte <= 'F') {
        digit = heR_byte - 'A' + 0xa;
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

  unsigned char *bitmap = classes->buffer[classes->size];
  bitmap_clear(bitmap, sizeof(char_class_t));

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
        if (size == builtin_class_names[i].size &&
            memcmp(name, builtin_class_names[i].name, size) == 0) {
          break;
        }
      }

      if (i == N_BUILTIN_CLASSES) {
        return CREX_E_BAD_CHARACTER_CLASS;
      }

      bitmap_union(bitmap, builtin_classes[i], sizeof(char_class_t));
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
        const unsigned char *other_bitmap = builtin_classes[index];

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

#undef PUSH_CHAR

  if (inverted) {
    for (size_t i = 0; i < sizeof(char_class_t); i++) {
      bitmap[i] = ~bitmap[i];
    }
  }

  for (size_t i = 0; i < N_BUILTIN_CLASSES; i++) {
    if (memcmp(bitmap, builtin_classes[i], sizeof(char_class_t)) == 0) {
      token->type = TT_BUILTIN_CHAR_CLASS;
      token->data.char_class_index = i;
      return CREX_OK;
    }
  }

  token->type = TT_CHAR_CLASS;

  for (size_t i = 0; i < classes->size; i++) {
    if (memcmp(bitmap, classes->buffer[i], sizeof(char_class_t)) == 0) {
      token->data.char_class_index = i;
      return CREX_OK;
    }
  }

  token->data.char_class_index = classes->size++;

  return CREX_OK;
}

WARN_UNUSED_RESULT static size_t str_to_size(const char *begin, const char *end) {
  size_t result = 0;

  for (const char *it = begin; it != end; it++) {
    assert(isdigit(*it));

    // FIXME: can't assume ASCII compiler(?)
    const unsigned int digit = *it - '0';

    if (result > (REPETITION_INFINITY - digit) / 10u) {
      return REPETITION_INFINITY;
    }

    result = 10 * result + digit;
  }

  return result;
}
