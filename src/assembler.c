#ifdef NATIVE_COMPILER

#include <sys/mman.h>
#include <unistd.h>

// It's spelled MAP_ANON on Darwin
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#include "x64-encoding.c"

typedef size_t label_t;

typedef enum { LU_CALL, LU_DEFINITION, LU_JCC, LU_JMP, LU_LEA } label_use_type_t;

typedef enum {
  JCC_JO,
  JCC_JNO,
  JCC_JB,
  JCC_JC = JCC_JB,
  JCC_JNAE = JCC_JB,
  JCC_JAE,
  JCC_JNB = JCC_JAE,
  JCC_JNC = JCC_JAE,
  JCC_JE,
  JCC_JZ = JCC_JE,
  JCC_JNE,
  JCC_JNZ = JCC_JNE,
  JCC_JBE,
  JCC_JNA = JCC_JBE,
  JCC_JA,
  JCC_JNBE = JCC_JA,
  JCC_JS,
  JCC_JNS,
  JCC_JP,
  JCC_JPE = JCC_JP,
  JCC_JNP,
  JCC_JPO = JCC_JNP,
  JCC_JL,
  JCC_JNGE = JCC_JL,
  JCC_JGE,
  JCC_JNL = JCC_JGE,
  JCC_JLE,
  JCC_JNG = JCC_JLE,
  JCC_JG,
  JCC_JNLE = JCC_JG,
  N_JCC_TYPES
} jcc_type_t;

typedef struct {
  label_use_type_t type : 4;

  // For lea
  reg_t reg : 4;

  // For jmp and jcc
  unsigned int is_short : 1;

  // For jcc
  jcc_type_t jcc_type : 5;

  size_t offset;
  label_t label;
} label_use_t;

typedef struct {
  size_t size;
  size_t capacity;
  unsigned char *code;

  size_t n_labels;

  struct {
    size_t size;
    size_t capacity;
    label_use_t *uses;
  } label_uses;

  size_t page_size;
} assembler_t;

#define DISPLACEMENT_IS_REPRESENTABLE(origin, target)                                              \
  (((target) >= (origin) && (long)((target) - (origin)) <= 2147483647L) ||                         \
   ((target) < (origin) && (long)((origin) - (target)) - 1 <= 2147483647L))

WARN_UNUSED_RESULT static int resolve_assembler_labels(assembler_t *as,
                                                       const allocator_t *allocator);

static void create_assembler(assembler_t *as) {
  as->size = 0;
  as->capacity = 0;
  as->code = NULL;

  as->n_labels = 0;

  as->label_uses.size = 0;
  as->label_uses.capacity = 0;
  as->label_uses.uses = NULL;

  const long page_size = sysconf(_SC_PAGESIZE);
  as->page_size = (page_size <= 0) ? 4096 : page_size;
}

static void destroy_assembler(assembler_t *as, const allocator_t *allocator) {
  FREE(allocator, as->label_uses.uses);
  munmap(as->code, as->capacity);
}

WARN_UNUSED_RESULT static unsigned char *
finalize_assembler(size_t *size, assembler_t *as, const allocator_t *allocator) {

  assert(as->size <= as->capacity);
  assert(as->capacity % as->page_size == 0);

  if (!resolve_assembler_labels(as, allocator)) {
    FREE(allocator, as->label_uses.uses);
    return NULL;
  }

  FREE(allocator, as->label_uses.uses);

  const size_t unused_pages = (as->capacity - as->size) / as->page_size;

  if (unused_pages > 0) {
    const size_t unused_size = as->page_size * unused_pages;
    munmap(as->code + as->capacity - unused_size, unused_size);
  }

  if (mprotect(as->code, as->size, PROT_READ | PROT_EXEC)) {
    munmap(as->code, as->size);
    return NULL;
  }

  *size = as->size;
  return as->code;
}

WARN_UNUSED_RESULT static int grow_assembler(assembler_t *as) {
  assert(as->capacity % as->page_size == 0);

  const size_t capacity = 2 * as->capacity + as->page_size;
  assert(capacity % as->page_size == 0);

  const int prot = PROT_READ | PROT_WRITE;
  const int flags = MAP_ANONYMOUS | MAP_PRIVATE;

  // First, try to "extend" the existing mapping by creating a new mapping immediately following.
  // On the first call to grow_assembler, as->code + as->code == NULL
  unsigned char *next = mmap(as->code + as->capacity, capacity - as->capacity, prot, flags, -1, 0);

  // ENOMEM, probably
  if (next == MAP_FAILED) {
    return 0;
  }

  // If in fact the new mapping was created immediately following the old mapping, we're done
  if (next == as->code + as->capacity) {
    as->capacity = capacity;
    return 1;
  }

  // Otherwise, allocate an entirely new mapping
  munmap(next, capacity - as->capacity);
  unsigned char *code = mmap(NULL, capacity, prot, flags, -1, 0);

  if (next == MAP_FAILED) {
    return 0;
  }

  // Copy the old data and destroy the old mapping
  memcpy(code, as->code, as->size);
  munmap(as->code, as->capacity);

  as->code = code;
  as->capacity = capacity;

  return 1;
}

WARN_UNUSED_RESULT static unsigned char *reserve_assembler_space(assembler_t *as, size_t size) {
  if (as->size + size > as->capacity) {
    if (!grow_assembler(as)) {
      return NULL;
    }
  }

  assert(as->size + size <= as->capacity);

  return as->code + as->size;
}

static void resize_assembler(assembler_t *as, unsigned char *end) {
  as->size = end - as->code;
}

#include "../build/x64.h"

WARN_UNUSED_RESULT static label_t create_label(assembler_t *as) {
  return as->n_labels++;
}

WARN_UNUSED_RESULT static label_use_t *push_label_use(assembler_t *as,
                                                      const allocator_t *allocator) {
  assert(as->label_uses.size <= as->label_uses.capacity);

  if (as->label_uses.size == as->label_uses.capacity) {
    const size_t capacity = 2 * as->label_uses.capacity + 4;
    label_use_t *uses = ALLOC(allocator, sizeof(label_use_t) * capacity);

    if (uses == NULL) {
      return NULL;
    }

    safe_memcpy(uses, as->label_uses.uses, sizeof(label_use_t) * as->label_uses.capacity);
    FREE(allocator, as->label_uses.uses);

    as->label_uses.capacity = capacity;
    as->label_uses.uses = uses;
  }

  return &as->label_uses.uses[as->label_uses.size++];
}

WARN_UNUSED_RESULT static int
define_label(assembler_t *as, label_t label, const allocator_t *allocator) {
  label_use_t *use = push_label_use(as, allocator);

  if (use == NULL) {
    return 0;
  }

  use->type = LU_DEFINITION;
  use->offset = as->size;
  use->label = label;

  return 1;
}

WARN_UNUSED_RESULT static int
call_label(assembler_t *as, label_t label, const allocator_t *allocator) {
  if (reserve_assembler_space(as, 5) == NULL) {
    return 0;
  }

  label_use_t *use = push_label_use(as, allocator);

  if (use == NULL) {
    return 0;
  }

  use->type = LU_CALL;
  use->offset = as->size;
  use->label = label;

  as->size += 5;

  return 1;
}

WARN_UNUSED_RESULT static int
jmp_label(assembler_t *as, label_t label, const allocator_t *allocator) {
  if (reserve_assembler_space(as, 5) == NULL) {
    return 0;
  }

  label_use_t *use = push_label_use(as, allocator);

  if (use == NULL) {
    return 0;
  }

  use->type = LU_JMP;
  use->is_short = 0;
  use->offset = as->size;
  use->label = label;

  as->size += 5;

  return 1;
}

WARN_UNUSED_RESULT static int
lea64_reg_label(assembler_t *as, reg_t reg, label_t label, const allocator_t *allocator) {
  if (reserve_assembler_space(as, 7) == NULL) {
    return 0;
  }

  label_use_t *use = push_label_use(as, allocator);

  if (use == NULL) {
    return 0;
  }

  use->type = LU_LEA;
  use->reg = reg;
  use->offset = as->size;
  use->label = label;

  as->size += 7;

  return 1;
}

WARN_UNUSED_RESULT static int
jcc_label(assembler_t *as, jcc_type_t jcc_type, label_t label, const allocator_t *allocator) {
  if (reserve_assembler_space(as, 6) == NULL) {
    return 0;
  }

  label_use_t *use = push_label_use(as, allocator);

  if (use == NULL) {
    return 0;
  }

  use->type = LU_JCC;
  use->is_short = 0;
  use->jcc_type = jcc_type;
  use->offset = as->size;
  use->label = label;

  as->size += 6;

  return 1;
}

WARN_UNUSED_RESULT static int resolve_assembler_labels(assembler_t *as,
                                                       const allocator_t *allocator) {
  size_t *label_values = ALLOC(allocator, sizeof(size_t) * as->n_labels);

  if (label_values == NULL) {
    return 0;
  }

#ifndef NDEBUG
  for (size_t i = 0; i < as->n_labels; i++) {
    label_values[i] = SIZE_MAX;
  }
#endif

  const size_t n_uses = as->label_uses.size;
  label_use_t *uses = as->label_uses.uses;

  for (size_t i = 0; i < n_uses; i++) {
    const label_use_t *use = &uses[i];
    assert(use->label < as->n_labels);

    if (use->type != LU_DEFINITION) {
      continue;
    }

    label_values[use->label] = use->offset;
  }

  // Multi-pass label optimization

  size_t prev_shrinkage = 0;

  for (;;) {
    size_t shrinkage = 0;

    for (size_t i = 0; i < n_uses; i++) {
      label_use_t *use = &uses[i];

      switch (use->type) {
      case LU_DEFINITION: {
        // Adjust our best estimate of the label value
        if (shrinkage != 0) {
          label_values[use->label] = use->offset - shrinkage;
        }
        break;
      }

      case LU_JCC:
      case LU_JMP: {
        // When encoded with a relative operand, jcc and jmp are 2 bytes in their short form and
        // 6 or 5 bytes respectively in their near form

        const size_t gain = (use->type == LU_JCC) ? 4 : 3;

        // If the jmp/jcc is already short, there's no further optimization to do
        if (use->is_short) {
          shrinkage += gain;
          break;
        }

        const size_t origin = use->offset - shrinkage + 2;
        size_t target = label_values[use->label];

        // Assert that the label was actually defined
        assert(target != SIZE_MAX);

        // If the target follows the origin, the act of shrinking the instruction encoding would
        // cause the target to move
        if (target > origin) {
          target -= gain;
        }

        // There's no practical difference between an allocation failure and an excessively-large
        // displacement, since the latter implies that the generated code exceeds 2 GiB
        if (!DISPLACEMENT_IS_REPRESENTABLE(origin, target)) {
          FREE(allocator, label_values);
          return 0;
        }

        const long displacement = (long)target - (long)origin;

        if (-128 <= displacement && displacement <= 127) {
          use->is_short = 1;
          shrinkage += gain;
        }

        break;
      }

      case LU_LEA:
      case LU_CALL:
        // lea reg [rip + disp] is exactly 7 bytes irrespective of the magnitude of disp;
        // call foo is exactly 5 bytes when encoded as a rip-relative call. Neither is subject to
        // optimization
        break;

      default:
        assert(0);
      }
    }

    // Quit after the first pass that yielded no improvement
    if (shrinkage == prev_shrinkage) {
      break;
    }

    prev_shrinkage = shrinkage;
  }

  assert(n_uses > 0);

  // The segment of the machine code spanning from offset zero to the offset of the first
  // instruction with a label operand is guaranteed to be unchanged by the label
  // substitution/optimization step, so we can skip over it

  unsigned char *code = as->code + uses[0].offset;

  for (size_t i = 0; i < n_uses; i++) {
    const label_use_t *use = &uses[i];

    assert(label_values[use->label] != SIZE_MAX);

    // It's a bit easier to work with pointers rather than offsets in this context
    unsigned char *target = as->code + label_values[use->label];

    size_t reserved_size = 0;

    // Generate code for the i'th label-using instruction
    switch (use->type) {
    case LU_DEFINITION:
      // Label definitions don't generate any code
      break;

    case LU_CALL: {
      unsigned char *origin = code + 5;
      assert(DISPLACEMENT_IS_REPRESENTABLE(origin, target));

      const long displacement = target - origin;

      *(code++) = 0xe8;
      copy_displacement(code, displacement, 4);
      code += 4;

      reserved_size = 5;

      break;
    }

    case LU_JCC: {
      unsigned char *origin = code + ((use->is_short) ? 2 : 6);
      assert(DISPLACEMENT_IS_REPRESENTABLE(origin, target));

      assert(use->jcc_type < N_JCC_TYPES);

      const long displacement = target - origin;

      if (use->is_short) {
        assert(-128 <= displacement && displacement <= 127);
        *(code++) = 0x70 + use->jcc_type;
        copy_displacement(code, displacement, 1);
        code++;
      } else {
        *(code++) = 0x0f;
        *(code++) = 0x80 + use->jcc_type;
        copy_displacement(code, displacement, 4);
        code += 4;
      }

      reserved_size = 6;
      break;
    }

    case LU_JMP: {
      unsigned char *origin = code + ((use->is_short) ? 2 : 5);
      assert(DISPLACEMENT_IS_REPRESENTABLE(origin, target));

      const long displacement = target - origin;

      if (use->is_short) {
        assert(-128 <= displacement && displacement <= 127);
        *(code++) = 0xeb;
        copy_displacement(code, displacement, 1);
        code++;
      } else {
        *(code++) = 0xe9;
        copy_displacement(code, displacement, 4);
        code += 4;
      }

      reserved_size = 5;

      break;
    }

    case LU_LEA: {
      unsigned char *origin = code + 7;
      assert(DISPLACEMENT_IS_REPRESENTABLE(origin, target));

      const long displacement = target - origin;

      *(code++) = REX(1, use->reg >> 3u, 0, 0);
      *(code++) = 0x8d;
      *(code++) = MOD_REG_RM(0, use->reg & 7u, RBP);
      copy_displacement(code, displacement, 4);
      code += 4;

      reserved_size = 7;

      break;
    }

    default:
      assert(0);
    }

    // Consider the segment of code that lies strictly between the i'th and (i + 1)th label uses (or
    // the segment that lies after the last label use and spans to the end of the code)
    const size_t begin = uses[i].offset + reserved_size;
    const size_t end = (i == n_uses - 1) ? as->size : uses[i + 1].offset;
    assert(begin <= end);

    // Move the segment into the correct post-optimization position
    memmove(code, as->code + begin, end - begin);
    code += end - begin;
  }

  FREE(allocator, label_values);

  // Truncate the assembler to the correct size
  resize_assembler(as, code);

  return 1;
}

#endif
