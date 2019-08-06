#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define REX(w, r, x, b) (0x40u | ((w) << 3u) | ((r) << 2u) | ((x) << 1u) | (b))

#define MOD_REG_RM(mod, reg, rm) (((mod) << 6u) | ((reg) << 3u) | (rm))

#define SIB(scale, index, base) (((scale) << 6u) | ((index) << 3u) | (base))

typedef enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 } reg_t;

typedef enum {
  SCALE_1,
  SCALE_2,
  SCALE_4,
  SCALE_8,
  SCALE_NONE,
} scale_t;

typedef struct {
  size_t size;
  size_t capacity;
  unsigned char *data;
} buffer_t;

static unsigned char *extend(buffer_t *buffer, size_t delta) {
  size_t min_capacity = buffer->size + delta;

  if (buffer->capacity >= min_capacity) {
    unsigned char *result = buffer->data + buffer->size;
    buffer->size += delta;

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

  buffer->size += delta;

  return result;
}

typedef struct {
  reg_t base;
  scale_t scale;
  reg_t index;
  long displacement;
} memory_t;

static void copy_displacement(unsigned char *destination, long value, size_t size);

static void
encode_instr_r_r(buffer_t *buffer, unsigned char opcode, reg_t reg, reg_t rm, int rex_w) {
  const int rex_r = reg >> 3u;
  const int rex_b = rm >> 3u;

  unsigned char *data;

  if (rex_w || rex_r || rex_b) {
    data = extend(buffer, 3);
    *(data++) = REX(rex_w, rex_r, 0, rex_b);
  } else {
    data = extend(buffer, 2);
  }

  data[0] = opcode;
  data[1] = MOD_REG_RM(3u, reg & 7u, rm & 7u);
}

static void encode_instr_r_m(buffer_t *buffer,
                             unsigned char opcode,
                             size_t reg_or_extension,
                             const memory_t *rm,
                             int rex_w,
                             int rex_r) {
  size_t displacement_size;
  unsigned char mod;

  // [rbp], [r13], [rbp + reg], and [r13 + reg] can't be encoded with a zero-size displacement
  // (because the corresponding bit patterns are used for RIP-relative addressing in the former case
  // and displacement-only mode in the latter). We can encode these forms as e.g. [rbp + 0], with an
  // 8-bit displacement instead

  if (rm->displacement == 0 && rm->base != RBP && rm->base != R13) {
    displacement_size = 0;
    mod = 0;
  } else if (-128 <= rm->displacement && rm->displacement <= 127) {
    displacement_size = 1;
    mod = 1;
  } else {
    assert(-2147483648 <= rm->displacement && rm->displacement <= 2147483647);
    displacement_size = 4;
    mod = 2;
  }

  if (rm->scale != SCALE_NONE || rm->base == RSP) {
    // Can't use RSP as the index register in a SIB byte
    assert(rm->index != RSP);

    const int rex_x = rm->index >> 3u;
    const int rex_b = rm->base >> 3u;

    unsigned char *data;

    if (rex_w || rex_r || rex_x || rex_b) {
      data = extend(buffer, 4 + displacement_size);
      *(data++) = REX(rex_w, rex_r, rex_x, rex_b);
    } else {
      data = extend(buffer, 3 + displacement_size);
    }

    data[0] = opcode;
    data[1] = MOD_REG_RM(mod, reg_or_extension & 7u, 0x04);
    data[2] = SIB(rm->scale, rm->index & 7u, rm->base & 7u);
    copy_displacement(data + 3, rm->displacement, displacement_size);

    return;
  }

  const int rex_b = rm->base >> 3u;

  unsigned char *data;

  if (rex_w || rex_r || rex_b) {
    data = extend(buffer, 3 + displacement_size);
    *(data++) = REX(rex_w, rex_r, 0, rex_b);
  } else {
    data = extend(buffer, 2 + displacement_size);
  }

  data[0] = opcode;
  data[1] = MOD_REG_RM(mod, reg_or_extension, rm->base);
  copy_displacement(data + 2, rm->displacement, displacement_size);
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

static void mov_qword_reg_reg(buffer_t *buffer, reg_t destination, reg_t source) {
  encode_instr_r_r(buffer, 0x89, source, destination, 1);
}

static void call_mem(buffer_t *buffer, const memory_t *callee) {
  encode_instr_r_m(buffer, 0xff, 0x02, callee, 0, 0);
}

int main(void) {
  buffer_t buffer = {0, 0, NULL};

  for (reg_t r = RAX; r <= R15; r++) {
    for (reg_t s = RAX; s <= R15; s++) {
      // mov_qword_reg_reg(&buffer, r, s);

      memory_t callee;
      callee.base = r;
      callee.scale = (3 * r + s) % 5;
      callee.index = (s == RSP) ? RAX : s;
      callee.displacement = (signed long)(13 * r + 37 * s) % 1001 - 50;
      fprintf(stderr, "%ld\n", callee.displacement);

      call_mem(&buffer, &callee);
    }
  }

  for (size_t i = 0; i < buffer.size; i++) {
    putchar(buffer.data[i]);
  }

  free(buffer.data);

  return 0;
}
