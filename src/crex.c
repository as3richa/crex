#define _GNU_SOURCE // For MAP_ANONYMOUS

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "crex.h"

#if defined(__x86_64__) && !defined(NO_NATIVE_COMPILER)
#define NATIVE_COMPILER
#endif

#if (defined(__GNUC__) || defined(__clang__))

#define MAYBE_UNUSED __attribute__((unused))

#define UNREACHABLE()                                                                              \
  do {                                                                                             \
    assert(0);                                                                                     \
    __builtin_unreachable();                                                                       \
  } while (0)

#else

#define MAYBE_UNUSED
#define UNREACHABLE() assert(0)

#endif

// For brevity
typedef crex_allocator_t allocator_t;
typedef crex_context_t context_t;
typedef crex_match_t match_t;
typedef crex_status_t status_t;
typedef crex_regex_t regex_t;
#define WARN_UNUSED_RESULT CREX_WARN_UNUSED_RESULT

static void safe_memcpy(void *destination, const void *source, size_t size) {
  assert((destination != NULL && source != NULL) || size == 0);

  if (size != 0) {
    memcpy(destination, source, size);
  }
}

#include "bytecode-compiler.h"

struct crex_context {
  unsigned char *buffer;
  size_t capacity;
  allocator_t allocator;
};

struct crex_regex {
  size_t n_capturing_groups;
  size_t n_classes;
  size_t n_flags;

  char_class_t *classes;

  bytecode_t bytecode;

#ifdef NATIVE_COMPILER
  struct {
    size_t size;
    void *code;
  } native_code;
#endif

  // For crex_destroy_regex
  struct {
    void *context;
    void (*free)(void *, void *);
  } allocator;
};

// FIXME: clean up this tire fire
#include "serialization.c"

// FIXME: put this somewhere smart
#define NON_CAPTURING_GROUP SIZE_MAX

/** Character class plumbing **/

typedef struct {
  const char *name;
  size_t size;
} class_name_t;

#define N_BUILTIN_CLASSES 18
#define N_NAMED_BUILTIN_CLASSES 14

static char_class_t builtin_classes[N_BUILTIN_CLASSES] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0xfe, 0xff, 0xff,
     0x07, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff,
     0x07, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0x00, 0xfc, 0x01, 0x00, 0x00,
     0xf8, 0x01, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x3e, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff,
     0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0xfe, 0xff, 0xff,
     0x87, 0xfe, 0xff, 0xff, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0x7e, 0x00, 0x00,
     0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xfc, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
    {0xff, 0xc1, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0xfc, 0x01, 0x00, 0x00,
     0x78, 0x01, 0x00, 0x00, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff},
    {0xff, 0xfb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
     0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

static class_name_t builtin_class_names[N_NAMED_BUILTIN_CLASSES] = {{"alnum", 5},
                                                                    {"alpha", 5},
                                                                    {"ascii", 5},
                                                                    {"blank", 5},
                                                                    {"cntrl", 5},
                                                                    {"digit", 5},
                                                                    {"graph", 5},
                                                                    {"lower", 5},
                                                                    {"print", 5},
                                                                    {"punct", 5},
                                                                    {"space", 5},
                                                                    {"upper", 5},
                                                                    {"word", 4},
                                                                    {"xdigit", 6}};

#define BCC_DIGIT 5
#define BCC_WHITESPACE 10
#define BCC_WORD 12
#define BCC_NOT_DIGIT 14
#define BCC_NOT_WHITESPACE 15
#define BCC_NOT_WORD 16
#define BCC_ANY 17

static void bitmap_set(unsigned char *bitmap, size_t index) {
  bitmap[index >> 3u] |= 1u << (index & 7u);
}

static void bitmap_clear(unsigned char *bitmap, size_t size) {
  memset(bitmap, 0, size);
}

static void bitmap_union(unsigned char *bitmap, const unsigned char *other_bitmap, size_t size) {
  while (size--) {
    bitmap[size] |= other_bitmap[size];
  }
}

static int bitmap_test(const unsigned char *bitmap, size_t index) {
  return (bitmap[index >> 3u] >> (index & 7u)) & 1u;
}

MAYBE_UNUSED static int bitmap_test_and_set(unsigned char *bitmap, size_t index) {
  const int result = bitmap_test(bitmap, index);
  bitmap_set(bitmap, index);
  return result;
}

MAYBE_UNUSED static size_t bitmap_size_for_bits(size_t bits) {
  return (bits + 7) / 8;
}

typedef union {
  size_t size;
  void *pointer;
} size_or_pointer_t;

#include "allocator.c"

#include "lexer.c"

#include "parser.c"

#include "bytecode-compiler.c"

#ifndef NATIVE_COMPILER

typedef size_t handle_t;

#define HANDLE_NULL (~(size_t)0)

typedef struct {
  context_t *context;
  size_t bump_pointer;
  size_t freelist;
} internal_allocator_t;

static void internal_allocator_init(internal_allocator_t *allocator, context_t *context) {
  allocator->context = context;
  allocator->bump_pointer = 0;
  allocator->freelist = HANDLE_NULL;
}

WARN_UNUSED_RESULT static handle_t internal_allocator_alloc(internal_allocator_t *allocator,
                                                            size_t size) {
  const size_t alignment = sizeof(size_or_pointer_t);
  size = alignment * ((size + alignment - 1) / alignment);

  context_t *context = allocator->context;

  if (allocator->bump_pointer + size <= context->capacity) {
    const handle_t result = allocator->bump_pointer;
    allocator->bump_pointer += size;

    return result;
  }

  if (allocator->freelist != HANDLE_NULL) {
    const handle_t result = allocator->freelist;
    allocator->freelist = *(handle_t *)(context->buffer + allocator->freelist);

    return result;
  }

  // FIXME: consider capping capacity based on an analytical bound
  const size_t capacity = 2 * context->capacity + size;
  unsigned char *buffer = ALLOC(&context->allocator, capacity);

  if (buffer == NULL) {
    return HANDLE_NULL;
  }

  safe_memcpy(buffer, context->buffer, allocator->bump_pointer);
  FREE(&context->allocator, context->buffer);

  context->buffer = buffer;
  context->capacity = capacity;

  assert(allocator->bump_pointer + size <= context->capacity);

  const handle_t result = allocator->bump_pointer;
  allocator->bump_pointer += size;

  return result;
}

static void internal_allocator_free(internal_allocator_t *allocator, handle_t handle) {
  const context_t *context = allocator->context;

  assert(handle % sizeof(size_or_pointer_t) == 0);

  *(handle_t *)(context->buffer + handle) = allocator->freelist;
  allocator->freelist = handle;
}

typedef struct {
  size_t n_pointers;
  handle_t head;
  internal_allocator_t allocator;
} state_list_t;

#define LIST_BUFFER(list) ((list)->allocator.context->buffer)

#define LIST_NEXT(list, handle) (*(handle_t *)(LIST_BUFFER(list) + (handle)))

#define LIST_INSTR_POINTER(list, handle)                                                           \
  (*(size_t *)(LIST_BUFFER(list) + (handle) + sizeof(size_or_pointer_t)))

#define LIST_POINTER_BUFFER(list, handle)                                                          \
  ((const char **)(LIST_BUFFER(list) + (handle) + 2 * sizeof(size_or_pointer_t)))

static void state_list_init(state_list_t *list, context_t *context, size_t n_pointers) {
  list->n_pointers = n_pointers;
  list->head = HANDLE_NULL;
  internal_allocator_init(&list->allocator, context);
}

static handle_t state_list_alloc(state_list_t *list) {
  const size_t size = sizeof(size_or_pointer_t) * (2 + list->n_pointers);
  return internal_allocator_alloc(&list->allocator, size);
}

static handle_t state_list_push_initial_state(state_list_t *list, handle_t predecessor) {
  assert(predecessor < list->allocator.context->capacity || predecessor == HANDLE_NULL);

  if (predecessor != HANDLE_NULL) {
    assert(LIST_NEXT(list, predecessor) == HANDLE_NULL);
  }

  const handle_t state = state_list_alloc(list);

  if (state == HANDLE_NULL) {
    return HANDLE_NULL;
  }

  LIST_NEXT(list, state) = HANDLE_NULL;
  LIST_INSTR_POINTER(list, state) = 0;

  const char **pointer_buffer = LIST_POINTER_BUFFER(list, state);

  for (size_t i = 0; i < list->n_pointers; i++) {
    pointer_buffer[i] = NULL;
  }

  if (predecessor == HANDLE_NULL) {
    list->head = state;
  } else {
    assert(LIST_NEXT(list, predecessor) == HANDLE_NULL);
    LIST_NEXT(list, predecessor) = state;
  }

  return state;
}

static int state_list_push_copy(state_list_t *list, handle_t predecessor, size_t instr_pointer) {
  assert(predecessor < list->allocator.context->capacity);

  const handle_t state = state_list_alloc(list);

  if (state == HANDLE_NULL) {
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

static handle_t state_list_pop(state_list_t *list, handle_t predecessor) {
  assert(predecessor < list->allocator.context->capacity || predecessor == HANDLE_NULL);

  const handle_t state = (predecessor == HANDLE_NULL) ? list->head : LIST_NEXT(list, predecessor);

  assert(state < list->allocator.context->capacity);

  const handle_t successor = LIST_NEXT(list, state);

  if (predecessor == HANDLE_NULL) {
    list->head = successor;
  } else {
    LIST_NEXT(list, predecessor) = successor;
  }

  internal_allocator_free(&list->allocator, state);

  return successor;
}

#endif

#include "native-compiler.c"

/** Public API **/

#if defined(__GNUC__) || defined(__clang__)
#define PUBLIC __attribute__((visibility("default")))
#else
#define PUBLIC
#endif

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

  if (!compile_to_bytecode(&regex->bytecode, &regex->n_flags, tree, allocator)) {
    *status = CREX_E_NOMEM;

    destroy_parsetree(tree, allocator);
    FREE(allocator, classes.buffer);
    FREE(allocator, regex);

    return NULL;
  }

  destroy_parsetree(tree, allocator);

  regex->n_classes = classes.size;
  regex->classes = classes.buffer;

  // Stash the free part of the allocator, so it doesn't need to be passed into crex_regex_destroy
  regex->allocator.context = allocator->context;
  regex->allocator.free = allocator->free;

#ifdef NATIVE_COMPILER
  *status = compile_to_native(regex, allocator);

  if (*status != CREX_OK) {
    FREE(allocator, regex->classes);
    DESTROY_BYTECODE(regex->bytecode, allocator);
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

  context->buffer = NULL;
  context->capacity = 0;
  context->allocator = *allocator;

  return context;
}

PUBLIC size_t crex_regex_n_capturing_groups(const regex_t *regex) {
  return regex->n_capturing_groups;
}

PUBLIC void crex_destroy_regex(regex_t *regex) {
  // regex->allocator isn't actually an allocator (it's missing alloc) but our macros don't care

  DESTROY_BYTECODE(regex->bytecode, &regex->allocator);
  FREE(&regex->allocator, regex->classes);

#ifdef NATIVE_COMPILER
  munmap(regex->native_code.code, regex->native_code.size);
#endif

  FREE(&regex->allocator, regex);
}

PUBLIC void crex_destroy_context(context_t *context) {
  const allocator_t *allocator = &context->allocator;
  FREE(allocator, context->buffer);
  FREE(allocator, context);
}

#ifndef NATIVE_COMPILER

#include "executor.c"

PUBLIC crex_status_t crex_is_match(int *is_match,
                                   crex_context_t *context,
                                   const crex_regex_t *regex,
                                   const char *str,
                                   size_t size) {
  return execute_regex(is_match, context, regex, str, size, 0);
}

PUBLIC crex_status_t crex_find(crex_match_t *match,
                               crex_context_t *context,
                               const crex_regex_t *regex,
                               const char *str,
                               size_t size) {
  return execute_regex(match, context, regex, str, size, 2);
}

PUBLIC crex_status_t crex_match_groups(crex_match_t *matches,
                                       crex_context_t *context,
                                       const crex_regex_t *regex,
                                       const char *str,
                                       size_t size) {
  return execute_regex(matches, context, regex, str, size, 2 * regex->n_capturing_groups);
}

#else

typedef status_t (*native_function_t)(
    void *, context_t *, const char *, const char *, size_t, const unsigned char *);

WARN_UNUSED_RESULT static status_t call_regex_native_code(void *result,
                                                          crex_context_t *context,
                                                          const crex_regex_t *regex,
                                                          const char *str,
                                                          size_t size,
                                                          size_t n_pointers) {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

  const native_function_t function = (native_function_t)(regex->native_code.code);

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

  return (*function)(result, context, str, str + size, n_pointers, (unsigned char *)regex->classes);
}

PUBLIC crex_status_t crex_is_match(int *is_match,
                                   crex_context_t *context,
                                   const crex_regex_t *regex,
                                   const char *str,
                                   size_t size) {
  return call_regex_native_code(is_match, context, regex, str, size, 0);
}

PUBLIC crex_status_t crex_find(crex_match_t *match,
                               crex_context_t *context,
                               const crex_regex_t *regex,
                               const char *str,
                               size_t size) {
  return call_regex_native_code(match, context, regex, str, size, 2);
}

PUBLIC crex_status_t crex_match_groups(crex_match_t *matches,
                                       crex_context_t *context,
                                       const crex_regex_t *regex,
                                       const char *str,
                                       size_t size) {
  return call_regex_native_code(matches, context, regex, str, size, 2 * regex->n_capturing_groups);
}

#endif

PUBLIC status_t crex_is_match_str(int *is_match,
                                  context_t *context,
                                  const regex_t *regex,
                                  const char *str) {
  return crex_is_match(is_match, context, regex, str, strlen(str));
}

PUBLIC status_t crex_find_str(match_t *match,
                              context_t *context,
                              const regex_t *regex,
                              const char *str) {
  return crex_find(match, context, regex, str, strlen(str));
}

PUBLIC status_t crex_match_groups_str(match_t *matches,
                                      context_t *context,
                                      const regex_t *regex,
                                      const char *str) {
  return crex_match_groups(matches, context, regex, str, strlen(str));
}

PUBLIC unsigned char *crex_dump_regex(status_t *status, size_t *size, const regex_t *regex) {
  return crex_dump_regex_with_allocator(status, size, regex, &default_allocator);
}

PUBLIC unsigned char *crex_dump_regex_with_allocator(status_t *status,
                                                     size_t *size,
                                                     const regex_t *regex,
                                                     const allocator_t *allocator) {
  // FIXME: reimplement this later
  (void)status;
  (void)size;
  (void)regex;
  (void)allocator;
  return NULL;
}

PUBLIC regex_t *crex_load_regex(status_t *status, unsigned char *buffer, size_t size) {
  return crex_load_regex_with_allocator(status, buffer, size, &default_allocator);
}

PUBLIC regex_t *crex_load_regex_with_allocator(status_t *status,
                                               unsigned char *buffer,
                                               size_t size,
                                               const allocator_t *allocator) {
  // FIXME: reimplement this later
  (void)status;
  (void)buffer;
  (void)size;
  (void)allocator;
  return NULL;
}

#include "debug.c"
