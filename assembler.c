#ifdef NATIVE_COMPILER

typedef struct {
  size_t size;
  size_t capacity;
  unsigned char *buffer;

  size_t n_labels;
  size_t *label_values;

  size_t n_label_uses;
  size_t label_uses_capacity;
  size_t *label_uses;
} assembler_t;

typedef size_t label_t;

WARN_UNUSED_RESULT static int assembler_init(assembler_t *assembler, size_t n_labels) {
  assembler->size = 0;
  assembler->capacity = 0;
  assembler->buffer = NULL;

  assembler->n_labels = n_labels;
  assembler->label_values = malloc(sizeof(size_t) * n_labels); // FIXME: allocator

  if (assembler->label_values == NULL) {
    return 0;
  }

#ifndef NDEBUG
  for (size_t i = 0; i < n_labels; i++) {
    assembler->label_values[i] = SIZE_MAX;
  }
#endif

  assembler->n_label_uses = 0;
  assembler->label_uses_capacity = 0;
  assembler->label_uses = NULL;

  return 1;
}

static void assembler_destroy(assembler_t *assembler) {
  (void)assembler;
  // FIXME
}

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

static unsigned char *reserve_native_code(assembler_t *code, size_t size) {
  if (code->size + size <= code->capacity) {
    return code->buffer + code->size;
  }

  // FIXME: don't hardcode page size
  const size_t capacity = 2 * code->capacity + 4096;
  assert(code->size + size <= capacity);

  unsigned char *buffer = my_mremap(code->buffer, code->capacity, capacity);

  if (buffer == NULL) {
    return NULL;
  }

  code->buffer = buffer;
  code->capacity = capacity;

  return code->buffer + code->size;
}

static void resize(assembler_t *code, unsigned char *end) {
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
    size_t index;

    if (rm.has_index) {
      assert(rm.index != RSP);
      index = rm.index & 7u;
    } else {
      index = RSP;
    }

    *(data++) = MOD_REG_RM(mod, reg_or_extension & 7u, 0x04);
    *(data++) = SIB(rm.scale, index, rm.base & 7u);
    copy_displacement(data, rm.displacement, displacement_size);

    return 2 + displacement_size;
  }

  *(data++) = MOD_REG_RM(mod, reg_or_extension & 7u, rm.base & 7u);
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

enum { JMP_LABEL, CALL_LABEL, LEA_LABEL };

WARN_UNUSED_RESULT static int push_label_use(assembler_t *assembler) {
  assert(assembler->n_label_uses <= assembler->label_uses_capacity);

  if (assembler->n_label_uses == assembler->label_uses_capacity) {
    const size_t capacity = 2 * assembler->label_uses_capacity + 4;
    size_t *label_uses = realloc(assembler->label_uses, capacity);

    if (label_uses == NULL) {
      return 0;
    }

    assembler->label_uses = label_uses;
    assembler->label_uses_capacity = capacity;
  }

  assembler->label_uses[assembler->n_label_uses++] = assembler->size;

  return 1;
}

WARN_UNUSED_RESULT static int jmp64_label(assembler_t *assembler, label_t label) {
  assert(label <= UINT32_MAX);

  if (!push_label_use(assembler)) {
    return 0;
  }

  unsigned char *data = reserve_native_code(assembler, 5);

  if (data == NULL) {
    return 0;
  }

  *(data++) = JMP_LABEL;

  uint32_t u32_label = label;
  memcpy(data, &u32_label, 4);
  data += 4;

  resize(assembler, data);

  return 1;
}

WARN_UNUSED_RESULT static int call64_label(assembler_t *assembler, label_t label) {
  assert(label <= UINT32_MAX);

  if (!push_label_use(assembler)) {
    return 0;
  }

  unsigned char *data = reserve_native_code(assembler, 5);

  if (data == NULL) {
    return 0;
  }

  *(data++) = CALL_LABEL;

  uint32_t u32_label = label;
  memcpy(data, &u32_label, 4);
  data += 4;

  resize(assembler, data);

  return 1;
}

WARN_UNUSED_RESULT static int lea64_reg_label(assembler_t *assembler, label_t label) {
  assert(label <= UINT32_MAX);

  if (!push_label_use(assembler)) {
    return 0;
  }

  unsigned char *data = reserve_native_code(assembler, 6);

  if (data == NULL) {
    return 0;
  }

  *(data++) = LEA_LABEL;

  uint32_t u32_label = label;
  memcpy(data, &u32_label, 4);
  data += 4;

  data++;

  resize(assembler, data);

  return 1;
}

static void define_label(assembler_t *assembler, label_t label) {
  assert(label < assembler->n_labels);
  assembler->label_values[label] = assembler->size;
}

static void resolve_labels(assembler_t *assembler) {
  for (size_t i = 0; i < assembler->n_label_uses; i++) {
    const size_t offset = assembler->label_uses[i];

    const int type = assembler->buffer[offset];
    assert(type == CALL_LABEL || type == JMP_LABEL || type == LEA_LABEL);

    uint32_t u32_label;
    memcpy(&u32_label, assembler->buffer + offset + 1, 4);
    label_t label = u32_label;
    const size_t destination = assembler->label_values[label];
    assert(destination != SIZE_MAX);

    const size_t origin = offset + ((type == LEA_LABEL) ? 6 : 5);

    const long delta = destination - origin;

    switch (assembler->buffer[offset]) {
    case CALL_LABEL: {
      // FIXME
      break;
    }

    case JMP_LABEL: {
      unsigned char *data = assembler->buffer + offset;
      *(data++) = 0xe9;
      copy_displacement(data, delta, 4);
      break;
    }

    case LEA_LABEL: {
      // FIXME
      break;
    }

    default:
      assert(0);
    }
  }
}

#endif
