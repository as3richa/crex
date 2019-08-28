#ifdef NATIVE_COMPILER

typedef enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 } reg_t;

typedef enum { SCALE_1, SCALE_2, SCALE_4, SCALE_8 } scale_t;

#define REX(w, r, x, b) (0x40u | ((w) << 3u) | ((r) << 2u) | ((x) << 1u) | (b))

#define MOD_REG_RM(mod, reg, rm) (((mod) << 6u) | ((reg) << 3u) | (rm))

#define SIB(scale, index, base) (((scale) << 6u) | ((index) << 3u) | (base))

typedef struct {
  unsigned int rip_relative : 1;
  unsigned int base : 4;
  unsigned int has_index : 1;
  unsigned int scale : 2;
  unsigned int index : 4;
  long displacement;
} memory_t;

#define M_INDIRECT_REG(reg) ((memory_t){0, reg, 0, 0, 0, 0})

#define M_INDIRECT_REG_DISP(reg, displacement) ((memory_t){0, reg, 0, 0, 0, displacement})

#define M_INDIRECT_BSXD(base, scale, index, displacement)                                          \
  ((memory_t){0, base, 1, scale, index, displacement})

#define M_RIP_RELATIVE(displacement) ((memory_t){1, 0, 0, 0, 0, displacement})

#define M_DISPLACED(mem, disp)                                                                     \
  ((memory_t){(mem).rip_relative,                                                                  \
              (mem).base,                                                                          \
              (mem).has_index,                                                                     \
              (mem).scale,                                                                         \
              (mem).index,                                                                         \
              (mem).displacement + disp})

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

static size_t encode_rex_m(unsigned char *data, int rex_w, int rex_r, memory_t rm) {
  const int rex_x = (rm.has_index) ? (rm.index >> 3u) : 0;
  const int rex_b = (rm.rip_relative) ? 0 : rm.base >> 3u;

  if (rex_w || rex_r || rex_x || rex_b) {
    *data = REX(rex_w, rex_r, rex_x, rex_b);
    return 1;
  }

  return 0;
}

static size_t encode_mod_reg_rm_m(unsigned char *data, size_t reg_or_extension, memory_t rm) {
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

#endif
