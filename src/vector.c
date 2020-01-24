#include "common.h"

#define PASTE(x, y) x##y
#define C(x, y) PASTE(x, y)

#define TYPE C(NAME, _t)

// L for lifecycle (constructor, etc.), M for method
#define L(method_name) C(C(method_name, _), NAME)
#define M(method_name) C(C(NAME, _), method_name)

typedef struct {
  size_t size;
  size_t capacity;

  union {
    CONTAINED_TYPE stack[STACK_CAPACITY];
    CONTAINED_TYPE *heap;
  } buffer;
} TYPE;

MU WUR static int M(heap_allocated)(const TYPE *vector) {
  assert(vector->capacity >= STACK_CAPACITY);
  return vector->capacity > STACK_CAPACITY;
}

static void L(create)(TYPE *vector) {
  vector->size = 0;
  vector->capacity = STACK_CAPACITY;
}

static void L(destroy)(TYPE *vector, const allocator_t *allocator) {
  if (!M(heap_allocated)(vector)) {
    return;
  }

  FREE(allocator, vector->buffer.heap);
}

MU static CONTAINED_TYPE *L(unpack)(TYPE *vector, size_t *size, const allocator_t *allocator) {
  if (M(heap_allocated)(vector)) {
    *size = vector->size;
    return vector->buffer.heap;
  }

  CONTAINED_TYPE *buffer = ALLOC(allocator, sizeof(CONTAINED_TYPE) * vector->size);

  if (buffer == NULL) {
    return NULL;
  }

  memcpy(buffer, vector->buffer.stack, sizeof(CONTAINED_TYPE) * vector->size);

  *size = vector->size;
  return buffer;
}

MU WUR static CONTAINED_TYPE *M(buffer)(TYPE *vector) {
  assert(vector->capacity >= STACK_CAPACITY);

  if (vector->capacity == STACK_CAPACITY) {
    return vector->buffer.stack;
  }

  return vector->buffer.heap;
}

MU WUR static CONTAINED_TYPE M(at)(TYPE *vector, size_t index) {
  assert(index < vector->size);
  return M(buffer)(vector)[index];
}

MU WUR static int M(empty)(const TYPE *vector) {
  return vector->size == 0;
}

MU WUR static CONTAINED_TYPE *M(reserve)(TYPE *vector, size_t size, const allocator_t *allocator) {
  assert(vector->size <= vector->capacity);

  if (vector->size + size > vector->capacity) {
    const size_t capacity = 2 * (vector->size + size);

    CONTAINED_TYPE *buffer = ALLOC(allocator, sizeof(CONTAINED_TYPE) * capacity);

    if (buffer == NULL) {
      return NULL;
    }

    memcpy(buffer, M(buffer)(vector), sizeof(CONTAINED_TYPE) * vector->size);

    if (M(heap_allocated)(vector)) {
      FREE(allocator, vector->buffer.heap);
    }

    vector->capacity = capacity;
    vector->buffer.heap = buffer;
  }

  assert(vector->size + size <= vector->capacity);

  return M(buffer)(vector) + vector->size;
}

MU static void M(extend)(TYPE *vector, CONTAINED_TYPE *end) {
  vector->size = end - M(buffer)(vector);
  assert(vector->size <= vector->capacity);
}

MU WUR static CONTAINED_TYPE *M(emplace)(TYPE *vector, const allocator_t *allocator) {
  CONTAINED_TYPE *elem = M(reserve)(vector, 1, allocator);
  vector->size++;
  return elem;
}

MU WUR static int M(push)(TYPE *vector, CONTAINED_TYPE elem, const allocator_t *allocator) {
  CONTAINED_TYPE *dest = M(emplace)(vector, allocator);

  if (dest == NULL) {
    return 0;
  }

  *dest = elem;

  return 1;
}

MU WUR static CONTAINED_TYPE M(pop)(TYPE *vector) {
  assert(vector->size > 0);
  return M(buffer)(vector)[--vector->size];
}

MU WUR static CONTAINED_TYPE *M(top)(TYPE *vector) {
  assert(vector->size > 0);
  return M(buffer)(vector) + vector->size - 1;
}

#undef NAME
#undef CONTAINED_TYPE
#undef STACK_CAPACITY
#undef PASTE
#undef C
#undef TYPE
#undef M
