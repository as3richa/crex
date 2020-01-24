#ifdef NATIVE_COMPILER

#include "assembler.c"

#define R_SCRATCH RAX
#define R_SCRATCH_2 RCX

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
#define R_PREV_CHARACTER RDI

#define R_STATE RBP
#define R_PREDECESSOR RSI

#define M_CONTEXT M_INDIRECT_REG_DISP(RSP, 64)
#define M_RESULT M_INDIRECT_REG_DISP(RSP, 56)
#define M_ALLOCATOR_CONTEXT M_INDIRECT_REG_DISP(RSP, 48)
#define M_ALLOC M_INDIRECT_REG_DISP(RSP, 40)
#define M_FREE M_INDIRECT_REG_DISP(RSP, 32)
#define M_EOF M_INDIRECT_REG_DISP(RSP, 24)
#define M_MATCHED_STATE M_INDIRECT_REG_DISP(RSP, 16)
#define M_HEAD M_INDIRECT_REG_DISP(RSP, 8)
#define M_INITIAL_STATE_PUSHED M_INDIRECT_REG_DISP(RSP, 0)

#define STACK_FRAME_SIZE 72

#define M_FLAG_BUFFER M_INDIRECT_REG(R_BUFFER)

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
// branches can always be expressed as short jccs. For this class of branch, we can save
// some cycles and complexity by just building them with local variables and macros, rather than
// using the label substitution engine

#define BRANCH(jcc, id)                                                                            \
  ASM1(jcc, 0);                                                                                    \
  const size_t id = as->size

#define BRANCH_TARGET(id)                                                                          \
  do {                                                                                             \
    assert(as->size - id <= 127);                                                                  \
    as->code[id - 1] = as->size - id;                                                              \
  } while (0)

#define BACKWARDS_BRANCH_TARGET(id) const size_t id = as->size

#define BACKWARDS_BRANCH(jcc, id)                                                                  \
  do {                                                                                             \
    assert(as->size + 2 - id <= 128);                                                              \
    ASM1(jcc, -(int)(as->size + 2 - id));                                                          \
  } while (0)

enum {
  LABEL_DESTROY_STATE,
  LABEL_REMOVE_STATE,
  LABEL_KEEP_STATE,
  LABEL_EPILOGUE,
  LABEL_ALLOC_STATE_BLOCK,
  LABEL_ALLOC_MEMORY,
  LABEL_PUSH_STATE_COPY,
  // These labels are used for some particularly hairy local control flow in the executor body
  LABEL_STRING_LOOP_HEAD,
  LABEL_POST_STRING_LOOP,
  LABEL_STATE_LOOP_HEAD,
  LABEL_HAVE_STATE,
  LABEL_POST_STATE_LOOP,
  LABEL_FIND_OR_GROUPS_CODA,
  LABEL_FIND_OR_GROUPS_NO_MATCH,
  LABEL_RETURN_OK,
  N_STATIC_LABELS
};

// INSTR_LABEL(k) is a label pointing to the bytecode instruction starting at index k, if any
#define INSTR_LABEL(index) (N_STATIC_LABELS + (index))

// The first 64 flags are stored in a register, the remainder in memory
#define FLAG_BUFFER_SIZE(n_flags) (((((n_flags)-64) + 63) / 64) * 8)

WUR static int compile_prologue(assembler_t *as, size_t n_flags, const allocator_t *allocator);

WUR static int
compile_string_loop(assembler_t *as, const regex_t *regex, const allocator_t *allocator);

WUR static int
compile_state_list_loop(assembler_t *as, const regex_t *regex, const allocator_t *allocator);

WUR static int compile_epilogue(assembler_t *as, const allocator_t *allocator);

WUR static int compile_allocator(assembler_t *as, const allocator_t *allocator);

WUR static int
compile_push_state_copy(assembler_t *as, size_t n_capturing_groups, const allocator_t *allocator);

WUR static int compile_bytecode_instruction(assembler_t *as,
                                            regex_t *regex,
                                            size_t *index,
                                            const allocator_t *allocator);

WUR static int compile_match(assembler_t *as, const allocator_t *allocator);

WUR static int compile_debugging_boundary(assembler_t *as, const allocator_t *allocator);

WUR static status_t compile_to_native(regex_t *regex, const allocator_t *allocator) {
  assembler_t as;
  create_assembler(&as);

  // Preallocate static labels and bytecode instruction labels
  for (size_t i = 0; i < N_STATIC_LABELS + regex->bytecode.size; i++) {
    const label_t label = create_label(&as);

#ifndef NDEBUG
    assert(label == i);
#else
    (void)label;
#endif
  }

#define CHECK_ERROR(expr)                                                                          \
  do {                                                                                             \
    if (!expr) {                                                                                   \
      destroy_assembler(&as, allocator);                                                           \
      return CREX_E_NOMEM;                                                                         \
    }                                                                                              \
  } while (0)

  // Main executor function body

  CHECK_ERROR(compile_prologue(&as, regex->n_flags, allocator));
  CHECK_ERROR(compile_debugging_boundary(&as, allocator));

  CHECK_ERROR(compile_string_loop(&as, regex, allocator));
  CHECK_ERROR(compile_debugging_boundary(&as, allocator));

  CHECK_ERROR(compile_epilogue(&as, allocator));
  CHECK_ERROR(compile_debugging_boundary(&as, allocator));

  // Utility functions called from elsewhere

  CHECK_ERROR(compile_allocator(&as, allocator));
  CHECK_ERROR(compile_debugging_boundary(&as, allocator));

  CHECK_ERROR(compile_push_state_copy(&as, regex->n_capturing_groups, allocator));
  CHECK_ERROR(compile_debugging_boundary(&as, allocator));

  // Compiled regex program

  for (size_t i = 0; i < regex->bytecode.size;) {
    CHECK_ERROR(compile_bytecode_instruction(&as, regex, &i, allocator));
  }

  CHECK_ERROR(compile_match(&as, allocator));

#undef CHECK_ERROR

  regex->native_code.code = finalize_assembler(&regex->native_code.size, &as, allocator);

  if (regex->native_code.code == NULL) {
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

  // M_CONTEXT
  ASM1(push64_reg, RSI);

  // M_RESULT
  ASM1(push64_reg, RDI);

  const size_t allocator_offset = offsetof(context_t, allocator);

  // M_ALLOCATOR_CONTEXT
  ASM1(push64_mem, M_INDIRECT_REG_DISP(RSI, allocator_offset + offsetof(allocator_t, context)));

  // M_ALLOC
  ASM1(push64_mem, M_INDIRECT_REG_DISP(RSI, allocator_offset + offsetof(allocator_t, alloc)));

  // M_FREE
  ASM1(push64_mem, M_INDIRECT_REG_DISP(RSI, allocator_offset + offsetof(allocator_t, free)));

  // M_EOF
  ASM1(push64_reg, RCX);

  // M_MATCHED_STATE
  ASM1(push64_i8, -1);

  // M_HEAD
  ASM1(push64_i8, -1);

  // M_INITIAL_STATE_PUSHED
  ASM2(sub64_reg_i8, RSP, 8);

  // Unpack the buffer and capacity from the context
  ASM2(mov64_reg_mem, R_BUFFER, M_INDIRECT_REG_DISP(RSI, offsetof(context_t, buffer)));
  ASM2(mov64_reg_mem, R_CAPACITY, M_INDIRECT_REG_DISP(RSI, offsetof(context_t, capacity)));

  const uint64_t builtin_classes_uint = (uint64_t)builtin_classes;

  if (builtin_classes_uint <= 0xffffffffLU) {
    ASM2(mov32_reg_u32, R_BUILTIN_CHAR_CLASSES, builtin_classes_uint);
  } else {
    ASM2(mov64_reg_u64, R_BUILTIN_CHAR_CLASSES, builtin_classes_uint);
  }

  ASM2(xor32_reg_reg, R_BUMP_POINTER, R_BUMP_POINTER);
  ASM2(mov64_reg_i32, R_FREELIST, -1);

  if (n_flags > 64) {
    ASM2(mov32_reg_i32, R_SCRATCH, FLAG_BUFFER_SIZE(n_flags));
    ASM1(call_label, LABEL_ALLOC_MEMORY);

    // The result is in R_SCRATCH, but it is necessarily 0 (because that was the first allocation)
  }

  return 1;
}

static int
compile_string_loop(assembler_t *as, const regex_t *regex, const allocator_t *allocator) {
  ASM2(mov32_reg_i32, R_CHARACTER, -1);

  // Loop over the string, up to and including the EOF position

  // Here, and in several other instances in the string and state loops, we need to use the label
  // engine proper rather than the branch macros. This is because the code to e.g. initialize a
  // state or clear the flag bitmap can be arbitrarily long
  ASM1(define_label, LABEL_STRING_LOOP_HEAD);

  ASM2(mov32_reg_reg, R_PREV_CHARACTER, R_CHARACTER);

  // Let R_CHARACTER := -1 if R_STR == M_EOF, [R_STR] otherwise. N.B. there's no 8-bit cmov

  ASM2(mov32_reg_i32, R_CHARACTER, -1);

  // This branch doesn't span any variable-length code, so the macro is fine here
  ASM2(cmp64_reg_mem, R_STR, M_EOF);
  BRANCH(je_i8, eof);

  ASM2(movzx328_reg_mem, R_CHARACTER, M_INDIRECT_REG(R_STR));

  BRANCH_TARGET(eof);

  // Process the list of states
  if (!compile_state_list_loop(as, regex, allocator)) {
    return 0;
  }

  // Break if R_STR == M_EOF. We need to check this before the increment to prevent an overflow when
  // M_EOF == 0xffffffffffffffff; this means we need two cmps instead of one
  ASM2(cmp64_reg_mem, R_STR, M_EOF);
  ASM2(jcc_label, JCC_JE, LABEL_POST_STRING_LOOP);

  ASM1(inc64_reg, R_STR);

  ASM1(jmp_label, LABEL_STRING_LOOP_HEAD);

  ASM1(define_label, LABEL_POST_STRING_LOOP);

  // Populate the result

  ASM2(mov64_reg_mem, R_SCRATCH, M_RESULT);

  ASM2(cmp64_reg_i8, R_N_POINTERS, 0);
  ASM2(jcc_label, JCC_JNE, LABEL_FIND_OR_GROUPS_CODA);

  // R_N_POINTERS == 0, i.e. we're performing a boolean search. Because boolean searches
  // short-circuit on match, if we reach this point there was no match. Set the result equal to zero

  assert(sizeof(int) == 4 || sizeof(int) == 8);

  if (sizeof(int) == 4) {
    ASM2(mov32_mem_i32, M_INDIRECT_REG(R_SCRATCH), 0);
  } else {
    ASM2(mov64_mem_i32, M_INDIRECT_REG(R_SCRATCH), 0);
  }

  // Don't fall through to the next case
  ASM1(jmp_label, LABEL_RETURN_OK);

  ASM1(define_label, LABEL_FIND_OR_GROUPS_CODA);

  // Two possibilities: R_N_POINTERS is exactly 2 (in the case of a find), or R_N_POINTERS is
  // exactly 2 * regex->n_capturing_groups (in the case of a group query)

  ASM2(mov64_reg_mem, R_SCRATCH_2, M_MATCHED_STATE);

  ASM2(cmp64_reg_i8, R_SCRATCH_2, -1);
  ASM2(jcc_label, JCC_JE, LABEL_FIND_OR_GROUPS_NO_MATCH);

  // Copy the pointer buffer of the matched state into the result

  for (size_t i = 0; i < 2 * regex->n_capturing_groups; i++) {
    if (i == 2) {
      ASM2(cmp64_reg_i8, R_N_POINTERS, 2);
      ASM2(jcc_label, JCC_JE, LABEL_RETURN_OK);
    }

    // Clobber R_PREDECESSOR; we won't need it again

    ASM2(mov64_reg_mem, R_PREDECESSOR, M_DISPLACED(M_DEREF_HANDLE(R_SCRATCH_2), 16 + 8 * i));
    ASM2(mov64_mem_reg, M_DISPLACED(M_INDIRECT_REG(R_SCRATCH), 8 * i), R_PREDECESSOR);
  }

  // Don't fall through
  ASM1(jmp_label, LABEL_RETURN_OK);

  ASM1(define_label, LABEL_FIND_OR_GROUPS_NO_MATCH);

  // Fill the result with NULLs

  for (size_t i = 0; i < 2 * regex->n_capturing_groups; i++) {
    if (i == 2) {
      ASM2(cmp64_reg_i8, R_N_POINTERS, 2);
      ASM2(jcc_label, JCC_JE, LABEL_RETURN_OK);
    }

    assert((size_t)NULL == 0);
    ASM2(mov64_mem_i32, M_DISPLACED(M_INDIRECT_REG(R_SCRATCH), 8 * i), 0);
  }

  ASM1(define_label, LABEL_RETURN_OK);

  // Set the return value to CREX_OK
  assert(CREX_OK == 0);
  ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);

  // Fall through to the function epilogue

  return 1;
}

WUR static int
compile_state_list_loop(assembler_t *as, const regex_t *regex, const allocator_t *allocator) {
  ASM2(mov64_reg_i32, R_PREDECESSOR, -1);
  ASM2(mov64_reg_mem, R_STATE, M_HEAD);

  if (regex->n_flags > 0) {
    ASM2(xor32_reg_reg, R_FLAGS, R_FLAGS);

    if (regex->n_flags >= 64) {
      const size_t quadwords = FLAG_BUFFER_SIZE(regex->n_flags) / 8;

      for (size_t i = 0; i < quadwords; i++) {
        ASM2(mov64_mem_i32, M_DISPLACED(M_FLAG_BUFFER, 8 * i), 0);
      }
    }
  }

  // If we've already found a match, we needn't push the initial state; clear M_INITIAL_STATE_PUSHED
  // only if we haven't found a match

  ASM2(cmp64_mem_i8, M_MATCHED_STATE, -1);
  BRANCH(jne_i8, match_found);

  // FIXME: consider skipping this if R_N_POINTERS == 0
  ASM2(mov32_mem_i32, M_INITIAL_STATE_PUSHED, 0);

  BRANCH_TARGET(match_found);

  ASM1(define_label, LABEL_STATE_LOOP_HEAD);

  ASM2(cmp64_reg_i8, R_STATE, -1);
  ASM2(jcc_label, JCC_JNE, LABEL_HAVE_STATE);

  // We've reached the end of the list. If we have not yet pushed the initial state, push it and
  // continue; otherwise, break
  ASM2(bts32_mem_u8, M_INITIAL_STATE_PUSHED, 0);
  ASM2(jcc_label, JCC_JC, LABEL_POST_STATE_LOOP);

  ASM1(call_label, LABEL_ALLOC_STATE_BLOCK);

  // R_SCRATCH contains a handle pointing to a newly-allocated state

  ASM2(mov64_reg_reg, R_STATE, R_SCRATCH);

  // Point the incoming pointer at the new state
  // FIXME: kill M_HEAD, put the head in the buffer instead to avoid this cmov nonsense
  ASM2(lea64_reg_mem, R_SCRATCH, M_HEAD);
  ASM2(lea64_reg_mem, R_SCRATCH_2, M_DEREF_HANDLE(R_PREDECESSOR));
  ASM2(cmp64_reg_i8, R_PREDECESSOR, -1);
  ASM2(cmovne64_reg_reg, R_SCRATCH, R_SCRATCH_2);
  ASM2(mov64_mem_reg, M_INDIRECT_REG(R_SCRATCH), R_STATE);

  ASM2(lea64_reg_label, R_SCRATCH_2, INSTR_LABEL(0));

  // Set the successor to HANDLE_NULL
  ASM2(mov64_mem_i32, M_DEREF_HANDLE(R_STATE), -1);

  // Set the instruction pointer to the start of the regex program
  ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 8), R_SCRATCH_2);

  // Initialize the pointer buffer. Recall that R_N_POINTERS is either 0, 2, or 2 *
  // regex->n_capturing_groups
  for (size_t i = 0; i < 2 * regex->n_capturing_groups; i++) {
    if (i == 0 || i == 2) {
      ASM2(cmp64_reg_i8, R_N_POINTERS, i);
      ASM2(jcc_label, JCC_JE, LABEL_HAVE_STATE);
    }

    assert((size_t)NULL == 0);
    ASM2(mov64_mem_i32, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 16 + 8 * i), 0);
  }

  ASM1(define_label, LABEL_HAVE_STATE);

  // Resume execution of the regex program from where the state last left off
  ASM1(jmp_mem, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 8));

  // Control flow comes back to:
  // - LABEL_DESTROY_STATE, if a character, character class, anchor, or bit test failed and the
  // state needs to be removed and freed
  // - LABEL_REMOVE_STATE, if the state matched and must be removed from the list but not destroyed
  // - LABEL_KEEP_STATE, if the state should be kept for the next iteration of the outer loop
  // - LABEL_EPILOGUE, in the case of a match on a boolean search or in the case of an allocation
  // error

  ASM1(define_label, LABEL_DESTROY_STATE);

  // Cache the successor
  ASM2(mov64_reg_mem, R_SCRATCH, M_DEREF_HANDLE(R_STATE));

  // Chain the state onto the freelist
  ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_STATE), R_FREELIST);
  ASM2(mov64_reg_reg, R_FREELIST, R_STATE);

  // Fall through to the next case - destroyed states also need to be removed

  ASM1(define_label, LABEL_REMOVE_STATE);

  // Assume that R_SCRATCH contains the successor to R_STATE (but N.B. we may have arrived here
  // via a jump to LABEL_REMOVE_STATE)

  // Proceed to the next state
  ASM2(mov64_reg_reg, R_STATE, R_SCRATCH);

  // Point the incoming pointer at the next state (thereby removing the state from the list)
  // FIXME: remove nonsense here too
  ASM2(lea64_reg_mem, R_SCRATCH, M_HEAD);
  ASM2(lea64_reg_mem, R_SCRATCH_2, M_DEREF_HANDLE(R_PREDECESSOR));
  ASM2(cmp64_reg_i8, R_PREDECESSOR, -1);
  ASM2(cmovne64_reg_reg, R_SCRATCH, R_SCRATCH_2);
  ASM2(mov64_mem_reg, M_INDIRECT_REG(R_SCRATCH), R_STATE);

  // We jmp unconditionally to the loop head, because even if we've reached the end of the list we
  // may need to push the initial state. This skips the keep case
  ASM1(jmp_label, LABEL_STATE_LOOP_HEAD);

  ASM1(define_label, LABEL_KEEP_STATE);

  // R_SCRATCH contains the address at which to resume next time
  ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 8), R_SCRATCH);

  // Proceed to the next state
  ASM2(mov64_reg_reg, R_PREDECESSOR, R_STATE);
  ASM2(mov64_reg_mem, R_STATE, M_DEREF_HANDLE(R_STATE));

  ASM1(jmp_label, LABEL_STATE_LOOP_HEAD);

  ASM1(define_label, LABEL_POST_STATE_LOOP);

  // FIXME: short-circuit if list is empty and a match was found?

  return 1;
}

static int compile_epilogue(assembler_t *as, const allocator_t *allocator) {
  ASM1(define_label, LABEL_EPILOGUE);

  ASM2(add64_reg_i8, RSP, STACK_FRAME_SIZE - 8);

  // Pop context pointer
  ASM1(pop64_reg, R_SCRATCH_2);

  // Write buffer and capacity back into the context
  ASM2(mov64_mem_reg, M_INDIRECT_REG_DISP(R_SCRATCH_2, offsetof(context_t, buffer)), R_BUFFER);
  ASM2(mov64_mem_reg, M_INDIRECT_REG_DISP(R_SCRATCH_2, offsetof(context_t, capacity)), R_CAPACITY);

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
  // just using RBP to track the base of the stack frame, but then we lose a general-purpose
  // register
  size_t stack_offset = 8;

  // State-push allocation operations enter here
  ASM1(define_label, LABEL_ALLOC_STATE_BLOCK);

  // Compute the size of a state block
  ASM2(mov64_reg_reg, R_SCRATCH, R_N_POINTERS);
  ASM2(add64_reg_i8, R_SCRATCH, 2);
  ASM2(shl64_reg_i8, R_SCRATCH, 3);

  // General allocations (e.g. for the flag bitmap) enter here, and provide their own size
  // parameter in R_SCRATCH
  ASM1(define_label, LABEL_ALLOC_MEMORY);

  // Try bump allocation first
  ASM2(add64_reg_reg, R_SCRATCH, R_BUMP_POINTER);

  ASM2(cmp64_reg_reg, R_SCRATCH, R_CAPACITY);
  BRANCH(ja_i8, check_freelist);

  // Fastest path: bump allocation succeeded. Increment R_BUMP_POINTER and yield the old value
  ASM2(xchg64_reg_reg, R_SCRATCH, R_BUMP_POINTER);
  ASM0(ret);

  BRANCH_TARGET(check_freelist);

  // Check the freelist next
  ASM2(cmp64_reg_i8, R_FREELIST, -1);
  BRANCH(je_i8, reallocate_buffer);

  // Remove and return the freelist head
  ASM2(mov64_reg_reg, R_SCRATCH, R_FREELIST);
  ASM2(mov64_reg_mem, R_FREELIST, M_DEREF_HANDLE(R_FREELIST));
  ASM0(ret);

  BRANCH_TARGET(reallocate_buffer);

  // Slowest path: need to allocate a new, larger buffer, copy the data from the old buffer,
  // and free the old buffer

  // By ensuring that R_{BUFFER,CAPACITY,BUMP_POINTER} are preserved across function
  // calls we can save some pushes and pops
  assert(R_IS_CALLEE_SAVED(R_BUFFER));
  assert(R_IS_CALLEE_SAVED(R_CAPACITY));
  assert(R_IS_CALLEE_SAVED(R_BUMP_POINTER));

  // Push all the callee-saved registers except R_SCRATCH (which contains garbage).
  // We can hackily save a few cycles elsewhere if we preserve R_SCRATCH_2 across the entire
  // function call, so make sure we push it too

  assert(!R_IS_CALLEE_SAVED(R_SCRATCH_2));

  for (reg_t reg = RAX; reg <= R15; reg++) {
    if (R_IS_CALLEE_SAVED(reg) || reg == R_SCRATCH || reg == RSP) {
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
  ASM1(call_mem, M_DISPLACED(M_ALLOC, stack_offset));

  // If the allocation failed, set the return value to HANDLE_NULL and jump to the helper function's
  // epilogue

  ASM2(mov64_reg_i32, R_SCRATCH_2, -1);

  assert((size_t)NULL == 0);
  ASM2(cmp64_reg_i8, R_SCRATCH, 0);

  ASM2(cmove64_reg_reg, R_SCRATCH, R_SCRATCH_2);
  BRANCH(je_i8, epilogue);

  // Stash the new buffer in R_CAPACITY, because R_CAPACITY is stale and R_SCRATCH gets
  // clobbered by the call to memcpy
  ASM2(mov64_reg_reg, R_CAPACITY, R_SCRATCH);

  // Call memcpy, with the first parameter being the new buffer, the second being the old
  // buffer, and the third being the total space allocated in the old buffer (i.e.
  // R_BUMP_POINTER)

  ASM2(mov64_reg_reg, RDI, R_SCRATCH);
  ASM2(mov64_reg_reg, RSI, R_BUFFER);
  ASM2(mov64_reg_reg, RDX, R_BUMP_POINTER);

  // Save a few bytes by choosing the smallest usable mov

  const uint64_t memcpy_value = (uint64_t)memcpy;

  if (memcpy_value <= 0xffffffffLU) {
    ASM2(mov32_reg_i32, R_SCRATCH, memcpy_value);
  } else {
    ASM2(mov64_reg_u64, R_SCRATCH, memcpy_value);
  }

  ASM1(call_reg, R_SCRATCH);

  // Call the allocator's free function, with the first parameter being the allocator
  // context and the second being the the old buffer
  ASM2(mov64_reg_mem, RDI, M_DISPLACED(M_ALLOCATOR_CONTEXT, stack_offset));
  ASM2(mov64_reg_reg, RSI, R_BUFFER);
  ASM1(call_mem, M_DISPLACED(M_FREE, stack_offset));

  // Copy the new buffer out of R_CAPACITY
  ASM2(mov64_reg_reg, R_BUFFER, R_CAPACITY);

  // The result is the old value of R_BUMP_POINTER
  ASM2(mov64_reg_reg, R_SCRATCH, R_BUMP_POINTER);

  // Update R_BUMP_POINTER and R_CAPACITY with the information we previously spilled. We use
  // mov rather than pop because it simplifies the control flow
  ASM2(mov64_reg_mem, R_BUMP_POINTER, M_INDIRECT_REG(RSP));
  ASM2(mov64_reg_reg, R_CAPACITY, R_BUMP_POINTER);
  ASM1(shl164_reg, R_CAPACITY);

  BRANCH_TARGET(epilogue);

  // Discard spilled information
  ASM2(add64_reg_i8, RSP, 8);
  stack_offset -= 8;

  // Restore saved registers. Careful of underflow
  for (reg_t reg = R15;; reg--) {
    if (!R_IS_CALLEE_SAVED(reg) && reg != R_SCRATCH && reg != RSP) {
      ASM1(pop64_reg, reg);
      stack_offset -= 8;
    }

    if (reg == RAX) {
      break;
    }
  }

  // Return normally if R_SCRATCH != HANDLE_NULL, i.e. if the allocation succeeded

  ASM2(cmp64_reg_i8, R_SCRATCH, -1);
  BRANCH(je_i8, allocation_failed);

  ASM0(ret);

  BRANCH_TARGET(allocation_failed);

  // Discard the return address and jump directly to the function epilogue on allocation failure
  ASM2(add64_reg_i8, RSP, 8);
  ASM2(mov32_reg_i32, R_SCRATCH, CREX_E_NOMEM);
  ASM1(jmp_label, LABEL_EPILOGUE);

  stack_offset -= 8;
  assert(stack_offset == 0);

  return 1;
}

WUR static int
compile_push_state_copy(assembler_t *as, size_t n_capturing_groups, const allocator_t *allocator) {
  ASM1(define_label, LABEL_PUSH_STATE_COPY);

  // We assume that:
  // - R_SCRATCH contains a handle corresponding to a newly-allocated state block
  // - R_SCRATCH_2 contains the instruction pointer for the new state
  // i.e. we foist the problem of calling LABEL_ALLOC_STATE_BLOCK onto the caller. This makes
  // managing the stack simpler

  // Populate the instruction pointer
  ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_SCRATCH), 8), R_SCRATCH_2);

  // Insert the newly-allocated state after the current state
  ASM2(mov64_reg_mem, R_SCRATCH_2, M_DEREF_HANDLE(R_STATE));
  ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_SCRATCH), R_SCRATCH_2);
  ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_STATE), R_SCRATCH);

  // Copy pointer buffer. Recall that R_N_POINTERS is either 0, 2, or 2 times the number of
  // capturing groups; we can use this observation to unroll the copy loop very efficiently

  ASM2(cmp64_reg_i8, R_N_POINTERS, 0);
  BRANCH(jne_i8, n_pointers_nonzero);

  ASM0(ret);

  BRANCH_TARGET(n_pointers_nonzero);

  // R_N_POINTERS is at least 2, so copy 2 poitners

  ASM2(mov64_reg_mem, R_SCRATCH_2, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 16));
  ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_SCRATCH), 16), R_SCRATCH_2);

  ASM2(mov64_reg_mem, R_SCRATCH_2, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 24));
  ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_SCRATCH), 16 + 8), R_SCRATCH_2);

  ASM2(cmp64_reg_i8, R_N_POINTERS, 2);
  BRANCH(jne_i8, n_pointers_not_two);

  ASM0(ret);

  BRANCH_TARGET(n_pointers_not_two);

  // R_N_POINTERS is exactly 2 * n_capturing groups.

  for (size_t i = 2; i < 2 * n_capturing_groups; i++) {
    ASM2(mov64_reg_mem, R_SCRATCH_2, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 16 + 8 * i));
    ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_SCRATCH), 16 + 8 * i), R_SCRATCH_2);
  }

  ASM0(ret);

  return 1;
}

static int compile_bytecode_instruction(assembler_t *as,
                                        regex_t *regex,
                                        size_t *index,
                                        const allocator_t *allocator) {
  ASM1(define_label, INSTR_LABEL(*index));

  const unsigned char *bytecode = regex->bytecode.code;

  const unsigned char byte = bytecode[(*index)++];

  const unsigned char opcode = VM_OPCODE(byte);
  const size_t operand_size = VM_OPERAND_SIZE(byte);

  const size_t operand = deserialize_operand(bytecode + *index, operand_size);
  *index += operand_size;

  switch (opcode) {
  case VM_CHARACTER: {
    assert(operand <= 0xffu);

    if ((operand & (1u << 7u)) == 0) {
      ASM2(cmp32_reg_i8, R_CHARACTER, operand);
    } else {
      ASM2(cmp32_reg_i32, R_CHARACTER, operand);
    }

    ASM2(jcc_label, JCC_JNE, LABEL_DESTROY_STATE);

    ASM2(lea64_reg_label, R_SCRATCH, INSTR_LABEL(*index));
    ASM1(jmp_label, LABEL_KEEP_STATE);

    break;
  }

  case VM_CHAR_CLASS:
  case VM_BUILTIN_CHAR_CLASS: {
    assert(opcode != VM_CHAR_CLASS || operand < regex->n_classes);
    assert(opcode != VM_BUILTIN_CHAR_CLASS || operand < N_BUILTIN_CLASSES);

    // Short-circuit for EOF
    ASM2(cmp32_reg_i8, R_CHARACTER, -1);
    ASM2(jcc_label, JCC_JE, LABEL_DESTROY_STATE);

    const reg_t base = (opcode == VM_CHAR_CLASS) ? R_CHAR_CLASSES : R_BUILTIN_CHAR_CLASSES;

    // The kth character class begins at offset 32 * k from the base of the array
    ASM2(bt32_mem_reg, M_INDIRECT_REG_DISP(base, 32 * operand), R_CHARACTER);

    ASM2(jcc_label, JCC_JNC, LABEL_DESTROY_STATE);

    ASM2(lea64_reg_label, R_SCRATCH, INSTR_LABEL(*index));
    ASM1(jmp_label, LABEL_KEEP_STATE);

    break;
  }

  case VM_ANCHOR_BOF: {
    ASM2(cmp32_reg_i8, R_PREV_CHARACTER, -1);
    ASM2(jcc_label, JCC_JNE, LABEL_DESTROY_STATE);
    break;
  }

  case VM_ANCHOR_BOL: {
    // We need to use a dynamic label here (and analogously in the VM_ANCHOR_EOL case), because the
    // jcc_label to LABEL_DESTROY_STATE might change size during branch optimization. This would
    // break the branch if we were to use the BRANCH macro

    const label_t passed = create_label(as);

    ASM2(cmp32_reg_i8, R_PREV_CHARACTER, -1);
    ASM2(jcc_label, JCC_JE, passed);

    ASM2(cmp32_reg_i8, R_PREV_CHARACTER, '\n');
    ASM2(jcc_label, JCC_JNE, LABEL_DESTROY_STATE);

    ASM1(define_label, passed);

    break;
  }

  case VM_ANCHOR_EOF: {
    ASM2(cmp32_reg_i8, R_CHARACTER, -1);
    ASM2(jcc_label, JCC_JNE, LABEL_DESTROY_STATE);
    break;
  }

  case VM_ANCHOR_EOL: {
    const label_t passed = create_label(as);

    ASM2(cmp32_reg_i8, R_CHARACTER, -1);
    ASM2(jcc_label, JCC_JE, passed);

    ASM2(cmp32_reg_i8, R_CHARACTER, '\n');
    ASM2(jcc_label, JCC_JNE, LABEL_DESTROY_STATE);

    ASM1(define_label, passed);

    break;
  }

  case VM_ANCHOR_WORD_BOUNDARY:
  case VM_ANCHOR_NOT_WORD_BOUNDARY: {
    const memory_t word_class = M_INDIRECT_REG_DISP(R_BUILTIN_CHAR_CLASSES, 32 * BCC_WORD);

    ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);

    ASM2(cmp32_reg_i8, R_PREV_CHARACTER, -1);
    BRANCH(je_i8, bof);

    ASM2(bt32_mem_reg, word_class, R_PREV_CHARACTER);
    ASM1(setc8_reg, R_SCRATCH);

    BRANCH_TARGET(bof);

    ASM2(xor32_reg_reg, R_SCRATCH_2, R_SCRATCH_2);

    ASM2(cmp32_reg_i8, R_CHARACTER, -1);
    BRANCH(je_i8, eof);

    ASM2(bt32_mem_reg, word_class, R_CHARACTER);
    ASM1(setc8_reg, R_SCRATCH_2);

    BRANCH_TARGET(eof);

    // R_SCRATCH (resp. R_SCRATCH_2) is equal to 1 if R_PREV_CHARACTER (resp. R_CHARACTER) is a word
    // character, 0 otherwise

    ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH_2);

    if (opcode == VM_ANCHOR_WORD_BOUNDARY) {
      // Fail if neither or both character(s) are word characters
      ASM2(jcc_label, JCC_JZ, LABEL_DESTROY_STATE);
    } else {
      // Fail if exactly one character is a word character
      ASM2(jcc_label, JCC_JNZ, LABEL_DESTROY_STATE);
    }

    break;
  }

  case VM_JUMP: {
    ASM1(jmp_label, INSTR_LABEL(*index + operand));
    break;
  }

  case VM_SPLIT_PASSIVE:
  case VM_SPLIT_EAGER:
  case VM_SPLIT_BACKWARDS_PASSIVE:
  case VM_SPLIT_BACKWARDS_EAGER: {
    // Conceptually, a given split has an "active" (higher priority) and a "passive" (lower
    // priority) branch, where the active branch is taken immediately in the current thread of
    // execution, and the passive branch is enqueued onto the list of states. Eager splits
    // have their target as the active branch and the next instruction as the passive
    // branch, and vice versa for passive splits

    label_t passive;

    switch (opcode) {
    case VM_SPLIT_PASSIVE: {
      passive = INSTR_LABEL(*index + operand);
      break;
    }

    case VM_SPLIT_BACKWARDS_PASSIVE: {
      passive = INSTR_LABEL(*index - operand);
      break;
    }

    case VM_SPLIT_EAGER:
    case VM_SPLIT_BACKWARDS_EAGER: {
      passive = INSTR_LABEL(*index);
      break;
    }

    default:
      assert(0);
    }

    // LABEL_PUSH_STATE_COPY accepts a state handle in R_SCRATCH and an instruction pointer in
    // R_SCRATCH_2
    ASM1(call_label, LABEL_ALLOC_STATE_BLOCK);
    ASM2(lea64_reg_label, R_SCRATCH_2, passive);
    ASM1(call_label, LABEL_PUSH_STATE_COPY);

    // Eager splits need to explicitly jump to their target; passive splits just fall through to the
    // next instruction
    if (opcode == VM_SPLIT_EAGER || opcode == VM_SPLIT_BACKWARDS_EAGER) {
      const label_t eager =
          INSTR_LABEL((opcode == VM_SPLIT_EAGER) ? (*index + operand) : (*index - operand));
      ASM1(jmp_label, eager);
    }

    break;
  }

  case VM_WRITE_POINTER: {
    if (operand < 128) {
      ASM2(cmp64_reg_i8, R_N_POINTERS, operand);
    } else {
      ASM2(cmp64_reg_i32, R_N_POINTERS, operand);
    }

    BRANCH(jbe_i8, out_of_range);

    ASM2(mov64_mem_reg, M_DISPLACED(M_DEREF_HANDLE(R_STATE), 8 * (2 + operand)), R_STR);

    BRANCH_TARGET(out_of_range);

    break;
  }

  case VM_TEST_AND_SET_FLAG: {
    assert(operand < regex->n_flags);

    if (operand < 32) {
      ASM2(bts32_reg_u8, R_FLAGS, operand);
    } else if (operand < 64) {
      ASM2(bts64_reg_u8, R_FLAGS, operand);
    } else {
      const size_t word_index = (operand - 64) / 32;
      ASM2(bts32_mem_u8, M_DISPLACED(M_FLAG_BUFFER, 4 * word_index), operand % 32);
    }

    ASM2(jcc_label, JCC_JC, LABEL_DESTROY_STATE);

    break;
  }

  default:
    assert(0);
  }

  return 1;
}

WUR static int compile_match(assembler_t *as, const allocator_t *allocator) {
  // FIXME: use 32-bit operations on R_N_POINTERS if applicable
  ASM2(cmp64_reg_i8, R_N_POINTERS, 0);
  BRANCH(jne_i8, n_pointers_nonzero);

  // R_N_POINTERS == 0, i.e. we're performing a boolean search. M_RESULT is a pointer to an int
  ASM2(mov64_reg_mem, R_SCRATCH, M_RESULT);

  assert(sizeof(int) == 4 || sizeof(int) == 8);

  if (sizeof(int) == 4) {
    ASM2(mov32_mem_i32, M_INDIRECT_REG(R_SCRATCH), 1);
  } else {
    ASM2(mov64_mem_i32, M_INDIRECT_REG(R_SCRATCH), 1);
  }

  // Short-circuit by jumping directly to the function epilogue
  ASM2(xor32_reg_reg, R_SCRATCH, R_SCRATCH);
  ASM1(jmp_label, LABEL_EPILOGUE);

  BRANCH_TARGET(n_pointers_nonzero);

  // R_N_POINTERS != 0, i.e. we're doing a find or group search

  // Deallocate the current M_MATCHED_STATE, if any

  ASM2(mov64_reg_mem, R_SCRATCH, M_MATCHED_STATE);

  ASM2(cmp64_reg_i8, R_SCRATCH, -1);
  BRANCH(je_i8, no_previous_match);

  // Chain the previous M_MATCHED_STATE onto the front of the freelist
  ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_SCRATCH), R_FREELIST);
  ASM2(mov64_reg_reg, R_FREELIST, R_SCRATCH);

  BRANCH_TARGET(no_previous_match);

  ASM2(mov64_mem_reg, M_MATCHED_STATE, R_STATE);

  // Deallocate any successors to R_STATE (because any matches therein would be of lower
  // priority)

  // FIXME: this loop looks extremely wrong

  ASM2(mov64_reg_mem, R_SCRATCH, M_DEREF_HANDLE(R_STATE));

  // Loop until the end of the list
  ASM2(cmp64_reg_i8, R_SCRATCH, -1);
  BRANCH(je_i8, post_loop);

  BACKWARDS_BRANCH_TARGET(loop_head);

  // Cache the successor's successor
  ASM2(mov64_reg_mem, R_SCRATCH_2, M_DEREF_HANDLE(R_SCRATCH));

  // Chain the successor onto the front of the freelist
  ASM2(mov64_mem_reg, M_DEREF_HANDLE(R_SCRATCH), R_FREELIST);
  ASM2(mov64_reg_reg, R_FREELIST, R_SCRATCH);

  // Proceed to the following state
  ASM2(mov64_reg_reg, R_SCRATCH, R_SCRATCH_2);

  ASM2(cmp64_reg_i8, R_SCRATCH, -1);
  BACKWARDS_BRANCH(jne_i8, loop_head);

  BRANCH_TARGET(post_loop);

  // Prevent the initial state from being pushed in the future
  ASM2(mov32_mem_i32, M_INITIAL_STATE_PUSHED, 1);

  // Remove the state from the list, but don't destroy it. R_SCRATCH holds the successor
  ASM2(mov64_reg_i32, R_SCRATCH, -1);
  ASM1(jmp_label, LABEL_REMOVE_STATE);

  return 1;
}

WUR static int compile_debugging_boundary(assembler_t *as, const allocator_t *allocator) {
#ifdef NDEBUG
  (void)as;
  (void)allocator;
#else
  for (size_t i = 0; i < 4; i++) {
    ASM0(nop);
  }
#endif

  return 1;
}

#endif
