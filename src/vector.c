#define VECTOR_C

#include "vector.h"

MU WUR static int M(heap_allocated)(const TYPE *vector) {
#ifdef STACK_CAPACITY
  assert(vector->capacity >= STACK_CAPACITY);
  return vector->capacity > STACK_CAPACITY;
#else
  (void)vector;
  return 1;
#endif
}

static void L(create)(TYPE *vector) {
  vector->size = 0;

#ifdef STACK_CAPACITY
  vector->capacity = STACK_CAPACITY;
#else
  vector->capacity = 0;
  vector->buffer.heap = NULL;
#endif
}

static void L(destroy)(TYPE *vector, const allocator_t *allocator) {
  if (!M(heap_allocated)(vector)) {
    return;
  }

  FREE(allocator, vector->buffer.heap);
}

MU static CONTAINED_TYPE *L(unpack)(TYPE *vector, size_t *size, const allocator_t *allocator) {
#ifdef STACK_CAPACITY
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
#else
  (void)allocator;
  *size = vector->size;
  return vector->buffer.heap;
#endif
}

MU WUR static CONTAINED_TYPE *M(buffer)(TYPE *vector) {
#ifdef STACK_CAPACITY
  assert(vector->capacity >= STACK_CAPACITY);

  if (vector->capacity == STACK_CAPACITY) {
    return vector->buffer.stack;
  }
#endif

  return vector->buffer.heap;
}

MU static CONTAINED_TYPE const *M(const_buffer)(const TYPE *vector) {
#ifdef STACK_CAPACITY
  assert(vector->capacity >= STACK_CAPACITY);

  if (vector->capacity == STACK_CAPACITY) {
    return vector->buffer.stack;
  }
#endif

  return vector->buffer.heap;
}

MU WUR static CONTAINED_TYPE M(at)(const TYPE *vector, size_t index) {
  assert(index < vector->size);
  return M(const_buffer)(vector)[index];
}

MU WUR static int M(empty)(const TYPE *vector) {
  return vector->size == 0;
}

MU WUR static CONTAINED_TYPE *M(reserve)(TYPE *vector, size_t size, const allocator_t *allocator) {
  assert(vector->size <= vector->capacity);

  if (vector->size + size > vector->capacity) {
    size_t capacity = 2 * (vector->size + size);

#ifdef MIN_ALLOCATION
    if (capacity < MIN_ALLOCATION) {
      capacity = MIN_ALLOCATION;
    }
#endif

    CONTAINED_TYPE *buffer = ALLOC(allocator, sizeof(CONTAINED_TYPE) * capacity);

    if (buffer == NULL) {
      return NULL;
    }

#ifdef STACK_CAPACITY
    // If STACK_CAPACITY is defined, M(buffer)(vector) is guaranteed to be non-null
    memcpy(buffer, M(buffer)(vector), sizeof(CONTAINED_TYPE) * vector->size);
#else
    safe_memcpy(buffer, M(buffer)(vector), sizeof(CONTAINED_TYPE) * vector->size);
#endif

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
#undef STRUCT_DEFINED

#undef PASTE
#undef C
#undef TYPE
#undef M
