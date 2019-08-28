#ifdef NATIVE_COMPILER

#define R_SCRATCH RAX
#define R_SCRATCH_2 RDI

#define R_CHAR_CLASSES R9
#define R_BUILTIN_CHAR_CLASSES RBX

#define R_BUFFER R15

#define R_STR RDX

#define R_N_POINTERS R8

#define R_CAPACITY R12
#define R_BUMP_POINTER R13
#define R_FREELIST R10

#define R_FLAGS R11

#define R_CHARACTER R14
#define R_PREV_CHARACTER RCX

#define R_STATE RBP
#define R_PREDECESSOR RSI

#define M_RESULT M_INDIRECT_REG_DISP(RSP, 48)
#define M_ALLOCATOR_CONTEXT M_INDIRECT_REG_DISP(RSP, 40)
#define M_ALLOC M_INDIRECT_REG_DISP(RSP, 32)
#define M_FREE M_INDIRECT_REG_DISP(RSP, 24)
#define M_EOF M_INDIRECT_REG_DISP(RSP, 16)
#define M_MATCHED_STATE M_INDIRECT_REG_DISP(RSP, 8)
#define M_HEAD M_INDIRECT_REG_DISP(RSP, 0)

#define STACK_FRAME_SIZE 56

#define M_DEREF_HANDLE(reg) M_INDIRECT_BSXD(R_BUFFER, SCALE_1, reg, 0)

#define R_IS_CALLEE_SAVED(reg) (reg == RBX || reg == RBP || (R12 <= reg && reg <= R15))

#define ASM0(id)                                                                                   \
  do {                                                                                             \
    if (!id(as, allocator)) {                                                                      \
      return 0;                                                                                    \
    }                                                                                              \
  } while (0)

#define ASM1(id, x)                                                                                \
  do {                                                                                             \
    if (!id(as, x, allocator)) {                                                                   \
      return 0;                                                                                    \
    }                                                                                              \
  } while (0)

#define ASM2(id, x, y)                                                                             \
  do {                                                                                             \
    if (!id(as, x, y, allocator)) {                                                                \
      return 0;                                                                                    \
    }                                                                                              \
  } while (0)

// The vast majority of branches in the (typical) compiled program are used for simple control
// flow and boolean logic within a single VM instruction or self-contained loop; moreover, these
// branches can always be expressed with an 8-bit operand. For this class of branch, we can save
// some cycles and complexity by just building them with local variables and macros, rather than
// using the label substitution engine

#define BRANCH(id)                                                                                 \
  ASM1(id, 0);                                                                                     \
  size_t branch_origin = as->size

#define BRANCH_TARGET()                                                                            \
  do {                                                                                             \
    assert(as->size - branch_origin <= 127);                                                       \
    as->code[branch_origin - 1] = as->size - branch_origin;                                        \
  } while (0)

#define BACKWARDS_BRANCH(id)                                                                       \
  do {                                                                                             \
    assert(as->size + 2 - branch_origin <= 128);                                                   \
    ASM1(id, -(int)(as->size + 2 - branch_origin));                                                \
  } while (0)

#define BACKWARDS_BRANCH_TARGET() size_t branch_origin = as->size

enum {
  LABEL_EPILOGUE,
  LABEL_ALLOC_STATE_BLOCK,
  LABEL_ALLOC_MEMORY,
  LABEL_PUSH_STATE_COPY,
  N_STATIC_LABELS
};

#define INSTR_LABEL(index) (N_STATIC_LABELS + (index))

WARN_UNUSED_RESULT static int
compile_prologue(assembler_t *as, size_t n_flags, const allocator_t *allocator);

WARN_UNUSED_RESULT static int
compile_main_loop(assembler_t *as, size_t n_flags, const allocator_t *allocator);

WARN_UNUSED_RESULT static int compile_epilogue(assembler_t *as, const allocator_t *allocator);

WARN_UNUSED_RESULT static int compile_allocator(assembler_t *as, const allocator_t *allocator);

WARN_UNUSED_RESULT static int compile_bytecode_instruction(assembler_t *as,
                                                           regex_t *regex,
                                                           size_t *index,
                                                           const allocator_t *allocator);

WARN_UNUSED_RESULT static int compile_match(assembler_t *as, const allocator_t *allocator);

WARN_UNUSED_RESULT static status_t compile_to_native(regex_t *regex, const allocator_t *allocator) {
  assembler_t as;
  create_assembler(&as);

  // The (N_STATIC_LABELS + k)th label corresponds to the bytecode instruction at index k
  const size_t n_labels = N_STATIC_LABELS + regex->size;

#define CHECK(expr)                                                                                \
  do {                                                                                             \
    if (!expr) {                                                                                   \
      destroy_assembler(&as, allocator);                                                           \
      return CREX_E_NOMEM;                                                                         \
    }                                                                                              \
  } while (0)

  CHECK(compile_prologue(&as, regex->n_flags, allocator));
  CHECK(compile_epilogue(&as, allocator));
  CHECK(compile_main_loop(&as, regex->n_flags, allocator));
  CHECK(compile_allocator(&as, allocator));

  for (size_t i = 0; i < regex->size;) {
    CHECK(compile_bytecode_instruction(&as, regex, &i, allocator));
  }

  CHECK(compile_match(&as, allocator));

  regex->native_code = finalize_assembler(&regex->native_code_size, &as, n_labels, allocator);

  if (regex->native_code == NULL) {
    return CREX_E_NOMEM;
  }

  return CREX_OK;
}

static int compile_prologue(assembler_t *as, size_t n_flags, const allocator_t *allocator) {
  // Push callee-saved registers
  for (reg_t reg = RAX; reg <= R15; reg++) {
    if (!R_IS_CALLEE_SAVED(reg)) {
      continue;
    }
    ASM1(push64_reg, reg);
  }

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
  ASM1(push64_mem, M_INDIRECT_REG_DISP(RSI, allocator_offset + offsetof(allocator_t, context)));
  ASM1(push64_mem, M_INDIRECT_REG_DISP(RSI, allocator_offset + offsetof(allocator_t, alloc)));
  ASM1(push64_mem, M_INDIRECT_REG_DISP(RSI, allocator_offset + offsetof(allocator_t, free)));

  // Save eof pointer
  ASM1(push64_reg, RCX);

  // Matched state, if any
  ASM1(push64_i8, -1);

  // Unpack the buffer and capacity from the context
  ASM2(lea64_reg_mem, R_BUFFER, M_INDIRECT_REG_DISP(RSI, offsetof(context_t, buffer)));
  ASM2(lea64_reg_mem, R_CAPACITY, M_INDIRECT_REG_DISP(RSI, offsetof(context_t, capacity)));

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
  (void)n_flags;

  return 1;
}

static int compile_main_loop(assembler_t *as, size_t n_flags, const allocator_t *allocator) {
  ASM2(cmp64_reg_mem, R_STR, M_EOF);

  BACKWARDS_BRANCH_TARGET();

  ASM2(mov32_reg_reg, R_PREV_CHARACTER, R_CHARACTER);

  ASM2(mov32_reg_i32, R_CHARACTER, -1);
  ASM2(cmovne64_reg_mem, R_CHARACTER, M_INDIRECT_REG(R_STR));

  ASM2(mov32_reg_i32, R_PREDECESSOR, -1);
  ASM2(mov64_reg_mem, R_STATE, M_HEAD);

  assert(n_flags <= 64 && "FIXME");
  (void)n_flags; // FIXME: wipe in-memory bitmap if necessary
  ASM2(xor32_reg_reg, R_FLAGS, R_FLAGS);

  // Thread iteration
  // FIXME

  ASM1(inc64_reg, R_STR);

  ASM2(cmp64_reg_mem, R_STR, M_EOF);
  BACKWARDS_BRANCH(jbe_i8);

  return 1;
}

static int compile_epilogue(assembler_t *as, const allocator_t *allocator) {
  ASM1(define_label, LABEL_EPILOGUE);

  ASM2(add64_reg_i8, RSP, STACK_FRAME_SIZE);

  // Pop calle-saved registers; careful of overflow
  for (reg_t reg = R15;; reg--) {
    if (R_IS_CALLEE_SAVED(reg)) {
      ASM1(pop64_reg, reg);
    }

    if (reg == RAX) {
      break;
    }
  }

  ASM0(ret);

  return 1;
}

static int compile_allocator(assembler_t *as, const allocator_t *allocator) {
  // The macros for the various stack variables are written assuming the stack is in the same
  // state as immediately following the prologue. However, because we reach this function body via
  // a call, we need to take into account the return address that was pushed onto the stack, as
  // well as the pushes we make within the body of the function. We could avoid this burden by
  // just using RBP to track the base of the stack frame, but then we lose a register
  size_t stack_offset = 8;

  // State-push allocation operations enter here
  ASM1(define_label, LABEL_ALLOC_STATE_BLOCK);

  // Compute the size of a state block
  ASM2(mov64_reg_reg, R_SCRATCH, R_N_POINTERS);
  ASM2(add64_reg_i8, R_SCRATCH, 2);
  ASM2(shl64_reg_i8, R_SCRATCH, 3);

  // General allocations (i.e. for the flag bitmap) enter here, and provide their own size
  // parameter in R_SCRATCH
  ASM1(define_label, LABEL_ALLOC_MEMORY);

  // Try bump allocation first
  ASM2(add64_reg_reg, R_SCRATCH, R_BUMP_POINTER);

  ASM2(cmp64_reg_reg, R_SCRATCH, R_CAPACITY);
  BRANCH(ja_i8);

  // Fastest path: bump allocation succeeded. Increment R_BUMP_POINTER and yield the old value
  ASM2(xchg64_reg_reg, R_SCRATCH, R_BUMP_POINTER);
  ASM0(ret);

  BRANCH_TARGET();

  {
    // Check the freelist next
    ASM2(cmp64_reg_i8, R_FREELIST, -1);
    BRANCH(je_i8);

    // Remove and return the freelist head
    ASM2(mov64_reg_reg, R_SCRATCH, R_FREELIST);
    ASM2(mov64_reg_mem, R_FREELIST, M_DEREF_HANDLE(R_FREELIST));
    ASM0(ret);

    BRANCH_TARGET();

    {
      // Slowest path: need to allocate a new, larger buffer, copy the data from the old buffer,
      // and free the old buffer

      // By ensuring that R_{BUFFER,CAPACITY,BUMP_POINTER} are preserve_native_coded across function
      // calls we can save some pushes and pops
      assert(R_IS_CALLEE_SAVED(R_BUFFER));
      assert(R_IS_CALLEE_SAVED(R_CAPACITY));
      assert(R_IS_CALLEE_SAVED(R_BUMP_POINTER));

      // Push all the non-scratch caller-saved registers
      for (reg_t reg = RAX; reg <= R15; reg++) {
        if (R_IS_CALLEE_SAVED(reg) || reg == R_SCRATCH || reg == R_SCRATCH_2 || reg == RSP) {
          continue;
        }

        ASM1(push64_reg, reg);
        stack_offset += 8;
      }

      // The required buffer size that we calculated on the fast path is still in R_SCRATCH.
      // We need to spill this in order to update R_BUMP_POINTER and R_CAPACITY after the calls
      ASM1(push64_reg, R_SCRATCH);
      stack_offset += 8;

      // Allocate twice as much space as is required
      ASM1(shl164_reg, R_SCRATCH);

      // Call the allocator's alloc function, with the first parameter being the allocator context
      // and the second being the requested capacity
      ASM2(mov64_reg_mem, RDI, M_DISPLACED(M_ALLOCATOR_CONTEXT, stack_offset));
      ASM2(mov64_reg_reg, RSI, R_SCRATCH);
      ASM1(call64_mem, M_DISPLACED(M_ALLOC, stack_offset));

      // Sanity check
      const size_t null_int = (size_t)NULL;
      assert(null_int <= 127);

      // If the allocation failed, set the return value to HANDLE_NULL and jump to the function
      // epilogue
      ASM2(mov64_reg_i32, R_SCRATCH_2, -1);
      ASM2(cmp64_reg_i8, R_SCRATCH, null_int);
      ASM2(cmove64_reg_reg, R_SCRATCH, R_SCRATCH_2);
      BRANCH(je_i8);

      {
        // Stash the new buffer in R_CAPACITY, because R_CAPACITY is stale and R_SCRATCH gets
        // clobbered by the call to memcpy
        ASM2(mov64_reg_reg, R_CAPACITY, R_SCRATCH);

        // Call memcpy, with the first parameter being the new buffer, the second being the old
        // buffer, and the third being the total space allocated in the old buffer (i.e.
        // R_BUMP_POINTER)
        ASM2(mov64_reg_reg, RDI, R_SCRATCH);
        ASM2(mov64_reg_reg, RSI, R_BUFFER);
        ASM2(mov64_reg_reg, RDX, R_BUMP_POINTER);
        ASM2(mov64_reg_u64, R_SCRATCH, (size_t)memcpy);
        ASM1(call64_reg, R_SCRATCH);

        // Call the allocator's free function, with the first parameter being the allocator
        // context and the second being the the old buffer
        ASM2(mov64_reg_mem, RDI, M_DISPLACED(M_ALLOCATOR_CONTEXT, stack_offset));
        ASM2(mov64_reg_reg, RSI, R_BUFFER);
        ASM1(call64_mem, M_DISPLACED(M_FREE, stack_offset));

        // Copy the new buffer out of R_CAPACITY
        ASM2(mov64_reg_reg, R_BUFFER, R_CAPACITY);

        // The result is the old value of R_BUMP_POINTER
        ASM2(mov64_reg_reg, R_SCRATCH, R_BUMP_POINTER);

        // Update R_BUMP_POINTER and R_CAPACITY with the information we previously spilled. We use
        // mov rather than pop because it simplifies the control flow
        ASM2(mov64_reg_mem, R_BUMP_POINTER, M_INDIRECT_REG(RSP));
        ASM2(mov64_reg_reg, R_CAPACITY, R_BUMP_POINTER);
        ASM1(shl164_reg, R_CAPACITY);
      }

      BRANCH_TARGET();

      // Discard spilled information
      ASM2(add64_reg_i8, RSP, 8);
      stack_offset -= 8;

      // Restore saved registers. Careful of underflow
      for (reg_t reg = R15;; reg--) {
        if (!R_IS_CALLEE_SAVED(reg) && reg != R_SCRATCH && reg != R_SCRATCH_2 && reg != RSP) {
          ASM1(pop64_reg, reg);
          stack_offset -= 8;
        }

        if (reg == RAX) {
          break;
        }
      }

      ASM0(ret);
    }
  }

  assert(stack_offset == 8);

  return 1;
}

static int compile_bytecode_instruction(assembler_t *as,
                                        regex_t *regex,
                                        size_t *index,
                                        const allocator_t *allocator) {
  ASM1(define_label, INSTR_LABEL(*index));

  const unsigned char byte = regex->bytecode[(*index)++];

  const unsigned char opcode = VM_OPCODE(byte);
  const size_t operand_size = VM_OPERAND_SIZE(byte);

  const size_t operand = deserialize_operand(regex->bytecode + *index, operand_size);
  *index += operand_size;

  switch (opcode) {
  case VM_CHARACTER: {
    assert(operand <= 0xffu);

    ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);

    if ((operand & (1u << 7u)) == 0) {
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
    ASM2(bt32_mem_reg, M_INDIRECT_BSXD(base, SCALE_4, R_SCRATCH_2, 32 * operand), R_CHARACTER);

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

        const memory_t dword =
            M_INDIRECT_BSXD(R_BUILTIN_CHAR_CLASSES, SCALE_4, R_SCRATCH, 32 * BCC_WORD);

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

  case VM_JUMP: {
    ASM1(jmp64_label, INSTR_LABEL(*index + operand));
    break;
  }

  case VM_SPLIT_PASSIVE:
  case VM_SPLIT_EAGER:
  case VM_SPLIT_BACKWARDS_PASSIVE:
  case VM_SPLIT_BACKWARDS_EAGER: {
    label_t passive_target;

    switch (opcode) {
    case VM_SPLIT_PASSIVE: {
      passive_target = INSTR_LABEL(*index + operand);
      break;
    }

    case VM_SPLIT_BACKWARDS_PASSIVE: {
      passive_target = INSTR_LABEL(*index - operand);
      break;
    }

    case VM_SPLIT_EAGER:
    case VM_SPLIT_BACKWARDS_EAGER: {
      passive_target = INSTR_LABEL(*index);
      break;
    }

    default:
      assert(0);
    }

    // WIP: push new state

    ASM1(call64_label, LABEL_ALLOC_STATE_BLOCK);

    // R_SCRATCH contains either HANDLE_NULL (i.e. -1), or a handle corresponding to a
    // newly-allocated state block

    ASM2(cmp64_reg_i8, R_SCRATCH, -1);
    // FIXME: je epilogue

    // Insert the newly-allocated state after the current state
    ASM2(mov64_reg_mem, R_SCRATCH_2, M_DEREF_HANDLE(R_STATE));
    ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_SCRATCH), R_SCRATCH_2);
    ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_STATE), R_SCRATCH);

    // Populate instruction pointer
    ASM2(lea64_reg_label, R_SCRATCH_2, passive_target);
    ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_SCRATCH), 8), R_SCRATCH_2);

    // Copy pointer buffer. Recall that R_N_POINTERS is either 0, 2, or 2 times the number of
    // capturing groups; we can use this observation to unroll the copy loop very efficiently
    {
      ASM2(cmp64_reg_i8, R_N_POINTERS, 0);
      BRANCH(je_i8);

      {
        ASM2(mov64_reg_mem, R_SCRATCH_2, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 16));
        ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_SCRATCH), 16), R_SCRATCH_2);

        ASM2(mov64_reg_mem, R_SCRATCH_2, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 24));
        ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_SCRATCH), 24), R_SCRATCH_2);

        ASM2(cmp64_reg_i8, R_N_POINTERS, 2);
        BRANCH(je_i8);

        for (size_t i = 2; i < 2 * regex->n_capturing_groups; i++) {
          ASM2(mov64_reg_mem, R_SCRATCH_2, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 8 * (2 + i)));
          ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_SCRATCH), 8 * (2 + i)), R_SCRATCH_2);
        }

        BRANCH_TARGET();
      }

      BRANCH_TARGET();
    }

    break;
  }

  case VM_WRITE_POINTER: {
    if (operand < 128) {
      ASM2(cmp32_reg_i8, R_N_POINTERS, operand);
    } else {
      ASM2(cmp32_reg_u32, R_N_POINTERS, operand);
    }

    BRANCH(jbe_i8);

    ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 8 * (2 + operand)), R_STR);

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
      ASM2(bts32_mem_u8, M_DISPLACED(M_DEREF_HANDLE(R_FLAGS), offset), operand % 32);
    }

    BRANCH(jnc_i8);

    ASM0(ret);

    BRANCH_TARGET();

    break;
  }

  default:
    assert(0);
  }

  return 1;
}

WARN_UNUSED_RESULT static int compile_match(assembler_t *as, const allocator_t *allocator) {
  ASM2(cmp32_reg_i8, R_N_POINTERS, 0);
  BRANCH(je_i8);

  {
    // n_pointers != 0, i.e. we're doing a find or group search

    ASM2(mov64_reg_mem, R_SCRATCH, M_MATCHED_STATE);

    // Deallocate the current M_MATCHED_STATE, if any
    ASM2(cmp64_reg_i8, R_SCRATCH, -1);
    BRANCH(je_i8);

    // Chain the previous M_MATCHED_STATE onto the front of the freelist
    ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_SCRATCH), R_FREELIST);
    ASM2(mov64_reg_reg, R_FREELIST, R_SCRATCH);

    BRANCH_TARGET();

    // Let M_MATCHED_STATE := R_STATE
    ASM2(mov64_mem_reg, M_MATCHED_STATE, R_STATE);

    // Deallocate any successors to R_STATE (because any matches therein would be of lower
    // priority)
    {
      ASM2(mov64_reg_mem, R_SCRATCH, M_DEREF_HANDLE(R_STATE));

      ASM2(cmp64_reg_i8, R_SCRATCH, -1);
      BRANCH(je_i8);

      {
        BACKWARDS_BRANCH_TARGET();

        // Chain the successor onto the front of the freelist
        ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_SCRATCH), R_FREELIST);
        ASM2(mov64_reg_reg, R_FREELIST, R_SCRATCH);

        ASM2(cmp64_reg_i8, R_SCRATCH, -1);
        BACKWARDS_BRANCH(jne_i8);
      }

      BRANCH_TARGET();
    }

    // Remove R_STATE from the list of active states

    // If R_PREDECESSOR is HANDLE_NULL, let R_SCRATCH be the address of M_HEAD; otherwise, let
    // R_SCRATCH be the address of the next-pointer of R_PREDECESSOR
    ASM2(lea64_reg_mem, R_SCRATCH, M_DEREF_HANDLE(R_PREDECESSOR));
    ASM2(lea64_reg_mem, R_SCRATCH_2, M_HEAD);
    ASM2(cmp64_reg_i8, R_PREDECESSOR, -1);
    ASM2(cmovne64_reg_reg, R_SCRATCH, R_SCRATCH_2);

    // Point the above at HANDLE_NULL
    ASM2(mov64_mem_i32, M_INDIRECT_REG(R_SCRATCH), -1);

    // FIXME: return something meaningful
    ASM0(ret);
  }

  BRANCH_TARGET();

  // n_pointers == 0, i.e. we're performing a boolean search; M_RESULT is
  // (the address on the stack of) a pointer to an int
  ASM2(mov32_reg_mem, R_SCRATCH_2, M_RESULT);

  // 64-bit int isn't totally implausible
  if (sizeof(int) == 4) {
    ASM2(mov32_mem_i32, M_INDIRECT_REG(R_SCRATCH_2), 0);
  } else {
    assert(sizeof(int) == 8);
    ASM2(mov64_mem_i32, M_INDIRECT_REG(R_SCRATCH_2), 0);
  }

  // Short-circuit by jumping directly to the function epilogue
  ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);
  ASM1(jmp64_label, LABEL_EPILOGUE);

  return 1;
}

#endif
