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

#define M_RESULT INDIRECT_RD(RSP, 48)
#define M_ALLOCATOR_CONTEXT INDIRECT_RD(RSP, 40)
#define M_ALLOC INDIRECT_RD(RSP, 32)
#define M_FREE INDIRECT_RD(RSP, 24)
#define M_EOF INDIRECT_RD(RSP, 16)
#define M_MATCHED_STATE INDIRECT_RD(RSP, 8)
#define M_HEAD INDIRECT_RD(RSP, 0)

#define STACK_FRAME_SIZE 56

#define M_DEREF_HANDLE(reg, offset) INDIRECT_BSXD(R_BUFFER, SCALE_1, reg, offset)

#define DISPLACED(mem, disp)                                                                       \
  ((indirect_operand_t){(mem).rip_relative,                                                        \
                        (mem).base,                                                                \
                        (mem).has_index,                                                           \
                        (mem).scale,                                                               \
                        (mem).index,                                                               \
                        (mem).displacement + disp})

#define CALLEE_SAVED(reg) (reg == RBX || reg == RBP || (R12 <= reg && reg <= R15))

#define ASM0(id)                                                                                   \
  do {                                                                                             \
    if (!id(assembler)) {                                                                          \
      return 0;                                                                                    \
    }                                                                                              \
  } while (0)

#define ASM1(id, x)                                                                                \
  do {                                                                                             \
    if (!id(assembler, x)) {                                                                       \
      return 0;                                                                                    \
    }                                                                                              \
  } while (0)

#define ASM2(id, x, y)                                                                             \
  do {                                                                                             \
    if (!id(assembler, x, y)) {                                                                    \
      return 0;                                                                                    \
    }                                                                                              \
  } while (0)

// The vast majority of branches in the (typical) compiled program are used for simple control
// flow and boolean logic within a single VM instruction or self-contained loop; moreover, these
// branches can always be expressed with an 8-bit operand. For this class of branch, we can save
// some cycles and complexity by just building them with local variables and macros

#define BRANCH(id)                                                                                 \
  ASM1(id, 0);                                                                                     \
  size_t branch_origin = assembler->size

#define BRANCH_TARGET()                                                                            \
  do {                                                                                             \
    assert(assembler->size - branch_origin <= 127);                                                \
    assembler->buffer[branch_origin - 1] = assembler->size - branch_origin;                        \
  } while (0)

#define BACKWARDS_BRANCH(id)                                                                       \
  do {                                                                                             \
    assert(assembler->size + 2 - branch_origin <= 128);                                            \
    ASM1(id, -(int)(assembler->size + 2 - branch_origin));                                         \
  } while (0)

#define BACKWARDS_BRANCH_TARGET() size_t branch_origin = assembler->size

WARN_UNUSED_RESULT static int compile_prologue(assembler_t *assembler, size_t n_flags);

WARN_UNUSED_RESULT static int compile_main_loop(assembler_t *assembler, size_t n_flags);

WARN_UNUSED_RESULT static int compile_epilogue(assembler_t *assembler);

WARN_UNUSED_RESULT static int compile_allocator(assembler_t *assembler);

WARN_UNUSED_RESULT static int
compile_bytecode_instruction(assembler_t *assembler, regex_t *regex, size_t *index);

WARN_UNUSED_RESULT static int compile_match(assembler_t *assembler);

#define LABEL_EPILOGUE 0
#define LABEL_ALLOC_STATE_BLOCK 1
#define LABEL_ALLOC_MEMORY 2
#define N_STATIC_LABELS 3
#define LABEL_BYTECODE_INDEX(i) (N_STATIC_LABELS + (i))

WARN_UNUSED_RESULT static status_t compile_to_native(regex_t *regex) {
  const size_t page_size = get_page_size();

  const size_t n_labels = N_STATIC_LABELS + regex->size;
  assert((n_labels - 1) <= UINT32_MAX);

  assembler_t assembler;

  if (!assembler_init(&assembler, n_labels)) {
    return CREX_E_NOMEM;
  }

#define CHECK(expr)                                                                                \
  do {                                                                                             \
    if (!expr) {                                                                                   \
      assembler_destroy(&assembler);                                                               \
      return CREX_E_NOMEM;                                                                         \
    }                                                                                              \
  } while (0)

  CHECK(compile_prologue(&assembler, regex->n_flags));
  CHECK(compile_epilogue(&assembler));
  CHECK(compile_main_loop(&assembler, regex->n_flags));
  CHECK(compile_allocator(&assembler));

  for (size_t i = 0; i < regex->size;) {
    CHECK(compile_bytecode_instruction(&assembler, regex, &i));
  }

  CHECK(compile_match(&assembler));

  resolve_labels(&assembler);

  assert(assembler.capacity % page_size == 0);

  const size_t n_pages = assembler.capacity / page_size;
  const size_t used_pages = (assembler.size + page_size - 1) / page_size;

  munmap(assembler.buffer + page_size * used_pages, page_size * (n_pages - used_pages));

  regex->native_code = assembler.buffer;
  regex->native_code_size = assembler.size;

  assembler_destroy(&assembler);

  return CREX_OK;
}

static int compile_prologue(assembler_t *assembler, size_t n_flags) {
  // Push callee-saved registers
  for (reg_t reg = RAX; reg <= R15; reg++) {
    if (!CALLEE_SAVED(reg)) {
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
  ASM1(push64_mem, INDIRECT_RD(RSI, allocator_offset + offsetof(allocator_t, context)));
  ASM1(push64_mem, INDIRECT_RD(RSI, allocator_offset + offsetof(allocator_t, alloc)));
  ASM1(push64_mem, INDIRECT_RD(RSI, allocator_offset + offsetof(allocator_t, free)));

  // Save eof pointer
  ASM1(push64_reg, RCX);

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
  (void)n_flags;

  return 1;
}

static int compile_main_loop(assembler_t *assembler, size_t n_flags) {
  ASM2(cmp64_reg_mem, R_STR, M_EOF);

  BACKWARDS_BRANCH_TARGET();

  ASM2(mov32_reg_reg, R_PREV_CHARACTER, R_CHARACTER);

  ASM2(mov32_reg_i32, R_CHARACTER, -1);
  ASM2(cmovne64_reg_mem, R_CHARACTER, INDIRECT_REG(R_STR));

  ASM2(mov32_reg_i32, R_PREDECESSOR, -1);
  ASM2(mov64_reg_mem, R_STATE, M_HEAD);

  assert(n_flags <= 64 && "FIXME");
  ASM2(xor32_reg_reg, R_FLAGS, R_FLAGS);

  // Thread iteration
  // FIXME

  ASM1(inc64_reg, R_STR);

  ASM2(cmp64_reg_mem, R_STR, M_EOF);
  BACKWARDS_BRANCH(jbe_i8);

  return 1;
}

static int compile_epilogue(assembler_t *assembler) {
  define_label(assembler, LABEL_EPILOGUE);

  ASM2(add64_reg_i8, RSP, STACK_FRAME_SIZE);

  // Pop calle-saved registers; careful of overflow
  for (reg_t reg = R15;; reg--) {
    if (CALLEE_SAVED(reg)) {
      ASM1(pop64_reg, reg);
    }

    if (reg == RAX) {
      break;
    }
  }

  ASM0(ret);

  return 1;
}

static int compile_allocator(assembler_t *assembler) {
  // The macros for the various stack variables are written assuming the stack is in the same
  // state as immediately following the prologue. However, because we reach this function body via
  // a call, we need to take into account the return address that was pushed onto the stack, as
  // well as the pushes we make within the body of the function. We could avoid this burden by
  // just using RBP to track the base of the stack frame, but then we lose a register
  size_t stack_offset = 8;

  // State-push allocation operations enter here
  define_label(assembler, LABEL_ALLOC_STATE_BLOCK);

  // Compute the size of a state block
  ASM2(mov64_reg_reg, R_SCRATCH, R_N_POINTERS);
  ASM2(add64_reg_i8, R_SCRATCH, 2);
  ASM2(shl64_reg_i8, R_SCRATCH, 3);

  // General allocations (i.e. for the flag bitmap) enter here, and provide their own size
  // parameter in R_SCRATCH
  define_label(assembler, LABEL_ALLOC_MEMORY);

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
    ASM2(mov64_reg_mem, R_FREELIST, DISPLACED(M_DEREF_HANDLE(R_FREELIST, 0), stack_offset));
    ASM0(ret);

    BRANCH_TARGET();

    {
      // Slowest path: need to allocate a new, larger buffer, copy the data from the old buffer,
      // and free the old buffer

      // By ensuring that R_{BUFFER,CAPACITY,BUMP_POINTER} are preserve_native_coded across function
      // calls we can save some pushes and pops
      assert(CALLEE_SAVED(R_BUFFER));
      assert(CALLEE_SAVED(R_CAPACITY));
      assert(CALLEE_SAVED(R_BUMP_POINTER));

      // Push all the non-scratch caller-saved registers
      for (reg_t reg = RAX; reg <= R15; reg++) {
        if (CALLEE_SAVED(reg) || reg == R_SCRATCH || reg == R_SCRATCH_2 || reg == RSP) {
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
      ASM2(mov64_reg_mem, RDI, DISPLACED(M_ALLOCATOR_CONTEXT, stack_offset));
      ASM2(mov64_reg_reg, RSI, R_SCRATCH);
      ASM1(call64_mem, DISPLACED(M_ALLOC, stack_offset));

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
        ASM2(mov64_reg_mem, RDI, DISPLACED(M_ALLOCATOR_CONTEXT, stack_offset));
        ASM2(mov64_reg_reg, RSI, R_BUFFER);
        ASM1(call64_mem, DISPLACED(M_FREE, stack_offset));

        // Copy the new buffer out of R_CAPACITY
        ASM2(mov64_reg_reg, R_BUFFER, R_CAPACITY);

        // The result is the old value of R_BUMP_POINTER
        ASM2(mov64_reg_reg, R_SCRATCH, R_BUMP_POINTER);

        // Update R_BUMP_POINTER and R_CAPACITY with the information we previously spilled. We use
        // mov rather than pop because it simplifies the control flow
        ASM2(mov64_reg_mem, R_BUMP_POINTER, INDIRECT_REG(RSP));
        ASM2(mov64_reg_reg, R_CAPACITY, R_BUMP_POINTER);
        ASM1(shl164_reg, R_CAPACITY);
      }

      BRANCH_TARGET();

      // Discard spilled information
      ASM2(add64_reg_i8, RSP, 8);
      stack_offset -= 8;

      // Restore saved registers. Careful of underflow
      for (reg_t reg = R15;; reg--) {
        if (!CALLEE_SAVED(reg) && reg != R_SCRATCH && reg != R_SCRATCH_2 && reg != RSP) {
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

static int compile_bytecode_instruction(assembler_t *assembler, regex_t *regex, size_t *index) {
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

  return 1;
}

WARN_UNUSED_RESULT static int compile_match(assembler_t *assembler) {
  ASM2(cmp32_reg_i8, R_N_POINTERS, 0);
  BRANCH(je_i8);

  {
    // n_pointers != 0, i.e. we're doing a find or group search

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

    // Deallocate any successors to R_STATE (because any matches therein would be of lower
    // priority)
    {
      ASM2(mov64_reg_mem, R_SCRATCH, M_DEREF_HANDLE(R_STATE, 0));

      ASM2(cmp64_reg_i8, R_SCRATCH, -1);
      BRANCH(je_i8);

      {
        BACKWARDS_BRANCH_TARGET();

        // Chain the successor onto the front of the freelist
        ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_SCRATCH, 0), R_FREELIST);
        ASM2(mov64_reg_reg, R_FREELIST, R_SCRATCH);

        ASM2(cmp64_reg_i8, R_SCRATCH, -1);
        BACKWARDS_BRANCH(jne_i8);
      }

      BRANCH_TARGET();
    }

    // Remove R_STATE from the list of active states

    // If R_PREDECESSOR is HANDLE_NULL, let R_SCRATCH be the address of M_HEAD; otherwise, let
    // R_SCRATCH be the address of the next pointer of R_PREDECESSOR
    ASM2(lea64_reg_mem, R_SCRATCH, M_DEREF_HANDLE(R_PREDECESSOR, 0));
    ASM2(lea64_reg_mem, R_SCRATCH_2, M_HEAD);
    ASM2(cmp64_reg_i8, R_PREDECESSOR, -1);
    ASM2(cmovne64_reg_reg, R_SCRATCH, R_SCRATCH_2);

    // Point the above at HANDLE_NULL
    ASM2(mov64_mem_i32, INDIRECT_REG(R_SCRATCH), -1);

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
  ASM1(jmp64_label, LABEL_EPILOGUE);

  return 1;
}

#endif