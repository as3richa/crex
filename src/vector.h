#include "common.h"

#define PASTE(x, y) x##y
#define C(x, y) PASTE(x, y)

#define TYPE C(NAME, _t)

// L for lifecycle (constructor, etc.), M for method
#define L(method_name) C(C(method_name, _), NAME)
#define M(method_name) C(C(NAME, _), method_name)

#ifndef STRUCT_DEFINED

typedef struct {
  size_t size;
  size_t capacity;

  union {
#ifdef STACK_CAPACITY
    CONTAINED_TYPE stack[STACK_CAPACITY];
#endif
    CONTAINED_TYPE *heap;
  } buffer;
} TYPE;

#endif

static void L(create)(TYPE *vector);
static void L(destroy)(TYPE *vector, const allocator_t *allocator);
MU static CONTAINED_TYPE *L(unpack)(TYPE *vector, size_t *size, const allocator_t *allocator);

MU WUR static int M(heap_allocated)(const TYPE *vector);
MU WUR static CONTAINED_TYPE *M(buffer)(TYPE *vector);
MU static CONTAINED_TYPE const *M(const_buffer)(const TYPE *vector);

MU WUR static CONTAINED_TYPE M(at)(const TYPE *vector, size_t index);

MU WUR static int M(empty)(const TYPE *vector);

MU WUR static CONTAINED_TYPE *M(reserve)(TYPE *vector, size_t size, const allocator_t *allocator);
MU static void M(extend)(TYPE *vector, CONTAINED_TYPE *end);

MU WUR static int M(push)(TYPE *vector, CONTAINED_TYPE elem, const allocator_t *allocator);
MU WUR static CONTAINED_TYPE *M(emplace)(TYPE *vector, const allocator_t *allocator);

MU WUR static CONTAINED_TYPE M(pop)(TYPE *vector);
MU WUR static CONTAINED_TYPE *M(top)(TYPE *vector);

#ifndef VECTOR_C

#undef NAME
#undef CONTAINED_TYPE
#undef STACK_CAPACITY
#undef STRUCT_DEFINED

#undef PASTE
#undef C
#undef TYPE
#undef M

#endif
