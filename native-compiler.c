#ifdef NATIVE_COMPILER

#include "build/x64.h"

#define R_SCRATCH RAX
#define R_SCRATCH_2 RDI

#define R_CHAR_CLASSES R9
#define R_BUILTIN_CHAR_CLASSES RBX

#define R_BUFFER RCX

#define R_STR RDX

#define R_N_POINTERS R8

#define R_CAPACITY R10
#define R_BUMP_POINTER R11
#define R_FREELIST R12

#define R_FLAGS R13

#define R_CHARACTER R14
#define R_PREV_CHARACTER R15

#define R_STATE RBP
#define R_PREDECESSOR RSI

#define M_RESULT INDIRECT_RD(RSP, 56)
#define M_ALLOCATOR_CONTEXT INDIRECT_RD(RSP, 48)
#define M_ALLOC INDIRECT_RD(RSP, 40)
#define M_FREE INDIRECT_RD(RSP, 32)
#define M_EOF INDIRECT_RD(RSP, 24)
#define M_MATCHED_STATE INDIRECT_RD(RSP, 16)
#define M_SPILL INDIRECT_RD(RSP, 8)
#define M_HEAD INDIRECT_RD(RSP, 0)

#define STACK_FRAME_SIZE 64

#define M_DEREF_HANDLE(reg, offset) INDIRECT_BSXD(R_BUFFER, SCALE_1, reg, offset)

WARN_UNUSED_RESULT static status_t compile_to_native(regex_t *regex) {
  const size_t page_size = get_page_size();

  native_code_t native_code;
  native_code.size = 0;

  // Preallocate some space, because it simplifies reallocation slightly
  native_code.capacity = page_size;
  native_code.buffer = my_mremap(NULL, 0, native_code.capacity);

  if (native_code.buffer == NULL) {
    return CREX_E_NOMEM;
  }

#define ASM0(id)                                                                                   \
  do {                                                                                             \
    if (!id(&native_code)) {                                                                       \
      my_mremap(native_code.buffer, native_code.capacity, 0);                                      \
      return CREX_E_NOMEM;                                                                         \
    }                                                                                              \
  } while (0)

#define ASM1(id, x)                                                                                \
  do {                                                                                             \
    if (!id(&native_code, x)) {                                                                    \
      my_mremap(native_code.buffer, native_code.capacity, 0);                                      \
      return CREX_E_NOMEM;                                                                         \
    }                                                                                              \
  } while (0)

#define ASM2(id, x, y)                                                                             \
  do {                                                                                             \
    if (!id(&native_code, x, y)) {                                                                 \
      my_mremap(native_code.buffer, native_code.capacity, 0);                                      \
      return CREX_E_NOMEM;                                                                         \
    }                                                                                              \
  } while (0)

  // The vast majority of branches in the (typical) compiled program are used for simple control
  // flow and boolean logic within a single VM instruction or self-contained loop; moreover, these
  // branches can always be expressed with an 8-bit operand. For this class of branch, we can save
  // some cycles and complexity by just building them with local variables and macros

#define BRANCH(id)                                                                                 \
  ASM1(id, 0);                                                                                     \
  size_t branch_origin = native_code.size

#define BRANCH_TARGET()                                                                            \
  do {                                                                                             \
    assert(native_code.size - branch_origin <= 127);                                               \
    native_code.buffer[branch_origin - 1] = native_code.size - branch_origin;                      \
  } while (0)

#define BACKWARDS_BRANCH(id)                                                                       \
  do {                                                                                             \
    assert(native_code.size + 2 - branch_origin <= 128);                                           \
    ASM1(id, -(int)(native_code.size + 2 - branch_origin));                                        \
  } while (0)

#define BACKWARDS_BRANCH_TARGET() size_t branch_origin = native_code.size;

  // Prologue
  {
    // Assume the SysV x64 calling convention

    // RBX, RBP, and R12 through R15 are callee-saved registers
    ASM1(push64_reg, RBX);
    ASM1(push64_reg, RBP);
    ASM1(push64_reg, R12);
    ASM1(push64_reg, R13);
    ASM1(push64_reg, R14);
    ASM1(push64_reg, R15);

    // We have parameters:
    // - RDI: result pointer  (int* or match_t*)
    // - RSI: context pointer (context_t*)
    // - RDX: str             (const char*)
    // - RCX: eof             (const char*)
    // - R8:  n_pointers      (size_t)
    // - R9:  classes         (const unsigned char*)

    // str, n_pointers, and classes should already be in the correct place
    assert(R_STR == RDX);
    assert(R_N_POINTERS == R8);
    assert(R_CHAR_CLASSES == R9);

    // Save result pointer
    ASM1(push64_reg, RDI);

    // Unpack the allocator from the context onto the stack
    const size_t allocator_offset = offsetof(context_t, allocator);
    ASM1(push64_mem, INDIRECT_RD(RSI, allocator_offset + offsetof(allocator_t, context)));
    ASM1(push64_mem, INDIRECT_RD(RSI, allocator_offset + offsetof(allocator_t, alloc)));
    ASM1(push64_mem, INDIRECT_RD(RSI, allocator_offset + offsetof(allocator_t, free)));

    // Save eof pointer
    ASM1(push64_reg, RCX);

    // Spill space
    ASM2(sub64_reg_i8, RSP, 8);

    // Matched state, if any
    ASM1(push64_i8, -1);

    // Unpack the buffer and capacity from the context
    ASM2(lea64_reg_mem, R_BUFFER, INDIRECT_RD(RSI, offsetof(context_t, buffer)));
    ASM2(lea64_reg_mem, R_CAPACITY, INDIRECT_RD(RSI, offsetof(context_t, capacity)));

    const uint64_t builtin_classes_uint = (uint64_t)builtin_classes;

    if (builtin_classes_uint <= 0xffffffffLU) {
      ASM2(mov32_reg_u32, R_BUILTIN_CHAR_CLASSES, builtin_classes_uint);
    } else {
      ASM2(mov64_reg_u64, R_BUILTIN_CHAR_CLASSES, builtin_classes_uint);
    }

    ASM2(xor32_reg_reg, R_BUMP_POINTER, R_BUMP_POINTER);
    ASM2(mov32_reg_i32, R_FREELIST, -1);

    ASM2(mov32_reg_i32, R_CHARACTER, -1);

    // FIXME: allocate flag buffer if necessary
  }

  // Executor main loop
  {
    ASM2(cmp64_reg_mem, R_STR, M_EOF);

    BACKWARDS_BRANCH_TARGET();

    ASM2(mov32_reg_reg, R_PREV_CHARACTER, R_CHARACTER);

    ASM2(mov32_reg_i32, R_CHARACTER, -1);
    ASM2(cmovne64_reg_mem, R_CHARACTER, INDIRECT_REG(R_STR));

    ASM2(mov32_reg_i32, R_PREDECESSOR, -1);
    ASM2(mov64_reg_mem, R_STATE, M_HEAD);

    assert(regex->n_flags <= 64 && "FIXME");
    ASM2(xor32_reg_reg, R_FLAGS, R_FLAGS);

    // Thread iteration
    // FIXME

    ASM1(inc64_reg, R_STR);

    ASM2(cmp64_reg_mem, R_STR, M_EOF);
    BACKWARDS_BRANCH(jbe_i8);
  }

  // Epilogue
  {
    ASM2(add64_reg_i8, RSP, STACK_FRAME_SIZE);
    ASM1(pop64_reg, R15);
    ASM1(pop64_reg, R14);
    ASM1(pop64_reg, R13);
    ASM1(pop64_reg, R12);
    ASM1(pop64_reg, RBP);
    ASM1(pop64_reg, RBX);

    ASM0(ret);
  }

  for (size_t i = 0; i < regex->size;) {
    const unsigned char byte = regex->bytecode[i++];

    const unsigned char opcode = VM_OPCODE(byte);
    const size_t operand_size = VM_OPERAND_SIZE(byte);

    const size_t operand = deserialize_operand(regex->bytecode + i, operand_size);
    i += operand_size;

    switch (opcode) {
    case VM_CHARACTER: {
      assert(operand <= 0xffu);

      ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);

      if ((operand & (1u << 7)) == 0) {
        ASM2(cmp32_reg_i8, R_CHARACTER, operand);
      } else {
        ASM2(cmp32_reg_i32, R_CHARACTER, operand);
      }

      // R_SCRATCH := 1 if R_CHARACTER == operand
      ASM1(sete8_reg, R_SCRATCH);

      // FIXME: jmp?
      ASM0(ret);

      break;
    }

    case VM_CHAR_CLASS:
    case VM_BUILTIN_CHAR_CLASS: {
      assert(opcode != VM_CHAR_CLASS || operand < regex->n_classes);
      assert(opcode != VM_BUILTIN_CHAR_CLASS || operand < N_BUILTIN_CLASSES);

      ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);

      // Short-circuit for EOF
      ASM2(cmp32_reg_i8, R_CHARACTER, -1);
      BRANCH(je_i8);

      // The bitmap bit corresponding to character k can be found in the (k / 32)th dword
      ASM2(mov32_reg_reg, R_SCRATCH_2, R_CHARACTER);
      ASM2(shr32_reg_u8, R_SCRATCH_2, 5);

      const reg_t base = (opcode == VM_CHAR_CLASS) ? R_CHAR_CLASSES : R_BUILTIN_CHAR_CLASSES;

      // The kth character class is at offset 32 * k from the base pointer
      ASM2(bt32_mem_reg, INDIRECT_BSXD(base, SCALE_4, R_SCRATCH_2, 32 * operand), R_CHARACTER);

      // R_SCRATCH := 1 if the bit corresponding to R_CHARACTER is set
      ASM1(setc8_reg, R_SCRATCH);

      BRANCH_TARGET();

      ASM0(ret);

      break;
    }

    case VM_ANCHOR_BOF: {
      ASM2(cmp32_reg_i8, R_PREV_CHARACTER, -1);
      BRANCH(je_i8);

      ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);
      ASM0(ret);

      BRANCH_TARGET();

      break;
    }

    case VM_ANCHOR_BOL: {
      ASM2(cmp32_reg_i8, R_PREV_CHARACTER, -1);
      BRANCH(je_i8);

      {
        ASM2(cmp32_reg_i8, R_PREV_CHARACTER, '\n');
        BRANCH(je_i8);

        ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);
        ASM0(ret);

        BRANCH_TARGET();
      }

      BRANCH_TARGET();

      break;
    }

    case VM_ANCHOR_EOF: {
      ASM2(cmp32_reg_i8, R_CHARACTER, -1);
      BRANCH(je_i8);

      ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);
      ASM0(ret);

      BRANCH_TARGET();

      break;
    }

    case VM_ANCHOR_EOL: {
      ASM2(cmp32_reg_i8, R_CHARACTER, -1);
      BRANCH(je_i8);

      {
        ASM2(cmp32_reg_i8, R_CHARACTER, '\n');
        BRANCH(je_i8);

        ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);
        ASM0(ret);

        BRANCH_TARGET();
      }

      BRANCH_TARGET();

      break;
    }

    case VM_ANCHOR_WORD_BOUNDARY:
    case VM_ANCHOR_NOT_WORD_BOUNDARY: {
      if (opcode == VM_ANCHOR_WORD_BOUNDARY) {
        ASM2(xor32_reg_reg, R_SCRATCH_2, R_SCRATCH_2);
      } else {
        ASM2(mov8_reg_i8, R_SCRATCH_2, 1);
      }

      // D.R.Y.
      for (size_t k = 0; k <= 1; k++) {
        const reg_t character = (k == 0) ? R_PREV_CHARACTER : R_CHARACTER;

        ASM2(cmp32_reg_i8, character, -1);
        BRANCH(je_i8);

        {
          ASM2(mov32_reg_reg, R_SCRATCH, character);
          ASM2(shr32_reg_u8, R_SCRATCH, 5);

          const indirect_operand_t dword =
              INDIRECT_BSXD(R_BUILTIN_CHAR_CLASSES, SCALE_4, R_SCRATCH, 32 * BCC_WORD);

          ASM2(bt32_mem_reg, dword, character);
          BRANCH(jnc_i8);

          ASM1(inc32_reg, R_SCRATCH_2);

          BRANCH_TARGET();
        }

        BRANCH_TARGET();
      }

      ASM2(test8_reg_i8, R_SCRATCH_2, 1);
      BRANCH(jnz_i8);

      ASM0(ret);

      BRANCH_TARGET();

      break;
    }

    case VM_JUMP:
    case VM_SPLIT_PASSIVE:
    case VM_SPLIT_EAGER:
    case VM_SPLIT_BACKWARDS_PASSIVE:
    case VM_SPLIT_BACKWARDS_EAGER: {
      assert(0);
    }

    case VM_WRITE_POINTER: {
      assert(operand < 2 * regex->n_capturing_groups);

      if (operand < 128) {
        ASM2(cmp32_reg_i8, R_N_POINTERS, operand);
      } else {
        ASM2(cmp32_reg_u32, R_N_POINTERS, operand);
      }

      BRANCH(jbe_i8);

      ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_STATE, 2 + operand), R_STR);

      BRANCH_TARGET();

      break;
    }

    case VM_TEST_AND_SET_FLAG: {
      assert(operand < regex->n_flags);

      if (regex->n_flags <= 64) {
        if (operand <= 32) {
          ASM2(bts32_reg_u8, R_FLAGS, operand);
        } else {
          ASM2(bts64_reg_u8, R_FLAGS, operand);
        }
      } else {
        const size_t offset = 4 * (operand / 4);
        ASM2(bts32_mem_u8, M_DEREF_HANDLE(R_FLAGS, offset), operand % 32);
      }

      BRANCH(jnc_i8);

      ASM0(ret);

      BRANCH_TARGET();

      break;
    }

    default:
      assert(0);
    }
  }

  {
    ASM2(cmp32_reg_i8, R_N_POINTERS, 0);
    BRANCH(je_i8);

    {
      // n_pointers != 0, i.e. we're doing a find or group search; stash the current state in
      // M_MATCHED_STATE

      ASM2(mov64_reg_mem, R_SCRATCH, M_MATCHED_STATE);

      // Deallocate the current M_MATCHED_STATE, if any
      ASM2(cmp64_reg_i8, R_SCRATCH, -1);
      BRANCH(je_i8);

      // Chain the previous M_MATCHED_STATE onto the front of the freelist
      ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_SCRATCH, 0), R_FREELIST);
      ASM2(mov64_reg_reg, R_FREELIST, R_SCRATCH);

      BRANCH_TARGET();

      // Let M_MATCHED_STATE := R_STATE
      ASM2(mov64_mem_reg, M_MATCHED_STATE, R_STATE);

      // Remove R_STATE from the list of active states

      // Let R_SCRATCH := M_NEXT_OF_PRED if R_STATE has a predecessor, M_HEAD otherwise. Two leas
      // and a cmov benchmarked better than the branching version
      ASM2(lea64_reg_mem, R_SCRATCH, M_DEREF_HANDLE(R_PREDECESSOR, 0));
      ASM2(lea64_reg_mem, R_SCRATCH_2, M_HEAD);
      ASM2(cmp64_reg_i8, R_PREDECESSOR, -1);
      ASM2(cmovne64_reg_reg, R_SCRATCH, R_SCRATCH_2);

      // Let R_SCRATCH_2 be the successor of R_STATE
      ASM2(mov64_reg_mem, R_SCRATCH_2, M_DEREF_HANDLE(R_STATE, 0));

      // Let the successor of R_STATE's predecessor be R_STATE's successor
      ASM2(mov64_mem_reg, INDIRECT_REG(R_SCRATCH), R_SCRATCH_2);

      // FIXME: return something meaningful
      ASM0(ret);
    }

    BRANCH_TARGET();

    // n_pointers == 0, i.e. we're performing a boolean search; M_RESULT is
    // (the address on the stack of) a pointer to an int
    ASM2(mov32_reg_mem, R_SCRATCH_2, M_RESULT);

    // 64-bit int isn't totally implausible
    if (sizeof(int) == 4) {
      ASM2(mov32_mem_i32, INDIRECT_REG(R_SCRATCH_2), 0);
    } else {
      assert(sizeof(int) == 8);
      ASM2(mov64_mem_i32, INDIRECT_REG(R_SCRATCH_2), 0);
    }

    // Short-circuit by jumping directly to the function epilogue
    ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);
    // FIXME: jump
  }

  assert(native_code.capacity % page_size == 0);

  const size_t n_pages = native_code.capacity / page_size;
  const size_t used_pages = (native_code.size + page_size - 1) / page_size;

  munmap(native_code.buffer + page_size * used_pages, page_size * (n_pages - used_pages));

  regex->native_code = native_code.buffer;
  regex->native_code_size = native_code.size;

  return CREX_OK;
}

#endif
