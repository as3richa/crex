#undef NDEBUG

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "str-builder.h"

struct str_builder {
  size_t size;
  size_t capacity;
  char *str;
};

void maybe_grow_str_builder(str_builder_t *sb, size_t size) {
  assert(sb->size + 1 <= sb->capacity);

  if (sb->size + size + 1 > sb->capacity) {
    sb->capacity = 2 * sb->capacity + size + 1;
    sb->str = realloc(sb->str, sb->capacity);
    assert(sb->str);
  }
}

str_builder_t *create_str_builder(void) {
  str_builder_t *sb = malloc(sizeof(str_builder_t));
  assert(sb);

  sb->size = 0;
  sb->capacity = 1;

  sb->str = malloc(1);
  assert(sb->str);
  *sb->str = 0;

  return sb;
}

void destroy_str_builder(str_builder_t *sb) {
  free(sb->str);
  free(sb);
}

void sb_strcat(str_builder_t *sb, const char *str) {
  const size_t size = strlen(str);
  maybe_grow_str_builder(sb, size);

  memcpy(sb->str + sb->size, str, size + 1);
  sb->size += size;
}

void sb_putchar(str_builder_t *sb, char c) {
  maybe_grow_str_builder(sb, 1);
  sb->str[sb->size++] = c;
  sb->str[sb->size] = 0;
}

void sb_cat_sprintf(str_builder_t *sb, const char *fmt, ...) {
  const size_t headway = sb->capacity - (sb->size + 1);

  va_list args;
  va_start(args, fmt);
  const size_t would_write = vsnprintf(sb->str + sb->size, headway, fmt, args);
  va_end(args);

  if (would_write + 1 > headway) {
    maybe_grow_str_builder(sb, would_write);
    assert(would_write < sb->capacity - sb->size);

    va_start(args, fmt);
    vsnprintf(sb->str + sb->size, would_write + 1, fmt, args);
    va_end(args);
  }

  sb->size += would_write;
}

void sb_cat_random(str_builder_t *sb, size_t min_size, size_t max_size, const char *characters) {
  const size_t n_characters = strlen(characters);

  const size_t size = min_size + rand() % (max_size - min_size + 1);
  maybe_grow_str_builder(sb, size);

  for (size_t i = 0; i < size; i++) {
    sb->str[sb->size++] = characters[rand() % n_characters];
  }
}

void sb_clear(str_builder_t *sb) {
  sb->size = 0;
}

const char *sb2str(const str_builder_t *sb) {
  return sb->str;
}

size_t sb_size(const str_builder_t *sb) {
  return sb->size;
}
