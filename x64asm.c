#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define INDIRECT_BSXD(base, scale, index, displacement)                                            \
  ((indirect_operand_t){0, base, 1, scale, index, displacement})

#define INDIRECT_RIP(displacement) ((indirect_operand_t){1, 0, 0, 0, 0, displacement})

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

typedef struct {
  size_t size;
  size_t capacity;
  unsigned char *data;
} buffer_t;

static unsigned char *reserve(buffer_t *buffer, size_t delta) {
  size_t min_capacity = buffer->size + delta;

  if (buffer->capacity >= min_capacity) {
    unsigned char *result = buffer->data + buffer->size;
    return result;
  }

  if (2 * buffer->capacity > min_capacity) {
    min_capacity = 2 * buffer->capacity;
  }

  unsigned char *data = realloc(buffer->data, min_capacity);

  if (data == NULL) {
    abort();
  }

  unsigned char *result = data + buffer->size;

  buffer->data = data;
  buffer->capacity = min_capacity;

  return result;
}

static void resize(buffer_t *buffer, unsigned char *end) {
  buffer->size = end - buffer->data;
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

#include "x64.h"

int main(void) {
  buffer_t buffer = {0, 0, NULL};

  mov64_reg_mem(&buffer, R13, INDIRECT_BSXD(RSP, SCALE_8, RAX, 0));
  call_mem(&buffer, INDIRECT_REG(RBP));
  bt64_reg_u8(&buffer, RSI, 40);
  bt32_mem_u8(&buffer, INDIRECT_BSXD(R11, SCALE_1, R13, 9999), 0);
  lea64_reg_mem(&buffer, RAX, INDIRECT_RIP(1));
  lea64_reg_mem(&buffer, RDI, INDIRECT_REG(RBP));
  lea64_reg_mem(&buffer, R12, INDIRECT_REG(RSP));
  lea64_reg_mem(&buffer, R13, INDIRECT_BSXD(R13, SCALE_8, R12, 1337));
  lea64_reg_mem(&buffer, RAX, INDIRECT_RIP(0xffffff));

  for (size_t i = 0; i < buffer.size; i++) {
    putchar(buffer.data[i]);
  }

  free(buffer.data);

  return 0;
}
