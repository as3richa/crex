#include "vm.h"

#define FLAGS(vm) (unsigned char *)(vm).buffer

#define THREAD_SIZE(vm) (BLOCK_SIZE * (2 + (vm).n_pointers) + (vm).extra_size)

WUR static vm_handle_t vm_alloc(vm_t *vm, size_t size);

WUR static int create_vm(
    vm_t *vm, context_t *context, const regex_t *regex, size_t n_pointers, size_t extra_size) {
  assert(n_pointers == 0 || n_pointers == 2 || n_pointers == 2 * regex->n_capturing_groups);

  vm->context = context;
  vm->n_pointers = n_pointers;

  // FIXME: consider making this conditional
  vm->extra_size = extra_size;

  vm->buffer = context->buffer;
  vm->capacity = context->capacity;
  vm->bump_pointer = 0;
  vm->freelist = NULL_HANDLE;

  vm->head = NULL_HANDLE;

  vm->matched_thread = NULL_HANDLE;

  vm->size = regex->bytecode.size;
  vm->code = regex->bytecode.code;

  vm->classes = regex->classes;

  vm->flags_size = bitmap_size_for_bits(regex->n_flags);

#ifndef NDEBUG
  vm->n_capturing_groups = regex->n_capturing_groups;
  vm->n_classes = regex->n_classes;
  vm->n_flags = regex->n_flags;
#endif

  const vm_handle_t flags = vm_alloc(vm, vm->flags_size);

  if (flags == NULL_HANDLE) {
    return 0;
  }

  assert(flags == 0);

  return 1;
}

WUR static vm_handle_t vm_alloc(vm_t *vm, size_t size) {
  // Round size up to the nearest multiple of the required alignment
  size = (size + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;

  if (vm->bump_pointer + size <= vm->capacity) {
    const vm_handle_t result = vm->bump_pointer;
    vm->bump_pointer += size;

    return result;
  }

  if (vm->freelist != NULL_HANDLE) {
    const vm_handle_t result = vm->freelist;
    vm->freelist = NEXT(*vm, result);

    return result;
  }

  const allocator_t *allocator = &vm->context->allocator;

  const size_t capacity = 2 * vm->capacity + size;
  unsigned char *buffer = ALLOC(allocator, capacity);

  if (buffer == NULL) {
    return NULL_HANDLE;
  }

  safe_memcpy(buffer, vm->buffer, vm->bump_pointer);
  FREE(allocator, vm->buffer);

  vm->capacity = capacity;
  vm->buffer = buffer;

  vm->context->capacity = capacity;
  vm->context->buffer = buffer;

  const vm_handle_t result = vm->bump_pointer;
  vm->bump_pointer += size;

  return result;
}

static void vm_free(vm_t *vm, vm_handle_t handle) {
  NEXT(*vm, handle) = vm->freelist;
  vm->freelist = handle;
}

WUR static vm_handle_t spawn_thread(vm_t *vm, vm_handle_t prev_thread) {
  const vm_handle_t thread = vm_alloc(vm, THREAD_SIZE(*vm));

  if (thread == NULL_HANDLE) {
    return NULL_HANDLE;
  }

  NEXT(*vm, thread) = NULL_HANDLE;
  INSTR_POINTER(*vm, thread) = 0;

  for (size_t i = 0; i < vm->n_pointers; i++) {
    POINTER_BUFFER(*vm, thread)[i] = NULL;
  }

  memset(EXTRA_DATA(*vm, thread), 0, vm->extra_size);

  if (prev_thread == NULL_HANDLE) {
    vm->head = thread;
  } else {
    NEXT(*vm, prev_thread) = thread;
  }

  return thread;
}

WUR static int split_thread(vm_t *vm, size_t instr_pointer, vm_handle_t prev_thread) {
  assert(instr_pointer <= vm->size);

  const vm_handle_t thread = vm_alloc(vm, THREAD_SIZE(*vm));

  if (thread == NULL_HANDLE) {
    return 0;
  }

  assert(prev_thread != NULL_HANDLE);

  NEXT(*vm, thread) = NEXT(*vm, prev_thread);
  INSTR_POINTER(*vm, thread) = instr_pointer;

  const size_t pointer_buffer_size = BLOCK_SIZE * vm->n_pointers;
  memcpy(POINTER_BUFFER(*vm, thread), POINTER_BUFFER(*vm, prev_thread), pointer_buffer_size);

  memcpy(EXTRA_DATA(*vm, thread), EXTRA_DATA(*vm, prev_thread), vm->extra_size);

  NEXT(*vm, prev_thread) = thread;

  return 1;
}

static vm_handle_t destroy_thread(vm_t *vm, vm_handle_t thread, vm_handle_t prev_thread) {
  vm_handle_t next_thread = NEXT(*vm, thread);

  if (prev_thread == NULL_HANDLE) {
    vm->head = next_thread;
  } else {
    NEXT(*vm, prev_thread) = next_thread;
  }

  vm_free(vm, thread);

  return next_thread;
}

WUR static thread_status_t step_thread(vm_t *vm,
                                       vm_handle_t thread,
                                       size_t *instr_pointer,
                                       const char *str,
                                       int character,
                                       int prev_character) {
  assert(*instr_pointer < vm->size);

  const unsigned char byte = vm->code[(*instr_pointer)++];

  const unsigned char opcode = VM_OPCODE(byte);
  const size_t operand_size = VM_OPERAND_SIZE(byte);

  assert(*instr_pointer + operand_size <= vm->size);

  const size_t operand = deserialize_operand(vm->code + *instr_pointer, operand_size);
  *instr_pointer += operand_size;

  switch (opcode) {
  case VM_CHARACTER: {
    assert(operand <= 0xffu);
    return ((size_t)character == operand) ? TS_DONE : TS_REJECTED;
  }

  case VM_CHAR_CLASS: {
    assert(operand < vm->n_classes);
    const int okay = character != -1 && bitmap_test(vm->classes[operand], character);
    return okay ? TS_DONE : TS_REJECTED;
  }

  case VM_BUILTIN_CHAR_CLASS: {
    assert(operand < N_BUILTIN_CLASSES);
    const int okay = character != -1 && bitmap_test(builtin_classes[operand], character);
    return okay ? TS_DONE : TS_REJECTED;
  }

  case VM_ANCHOR_BOF: {
    assert(operand == 0);
    return (prev_character == -1) ? TS_CONTINUE : TS_REJECTED;
  }

  case VM_ANCHOR_BOL: {
    assert(operand == 0);
    return (prev_character == -1 || prev_character == '\n') ? TS_CONTINUE : TS_REJECTED;
  }

  case VM_ANCHOR_EOF: {
    assert(operand == 0);
    return (character == -1) ? TS_CONTINUE : TS_REJECTED;
  }

  case VM_ANCHOR_EOL: {
    assert(operand == 0);
    return (character == -1 || character == '\n') ? TS_CONTINUE : TS_REJECTED;
  }

  case VM_ANCHOR_WORD_BOUNDARY:
  case VM_ANCHOR_NOT_WORD_BOUNDARY: {
    assert(operand == 0);

    const int prev_char_is_word =
        prev_character != -1 && bitmap_test(builtin_classes[BCC_WORD], prev_character);

    const int char_is_word = character != -1 && bitmap_test(builtin_classes[BCC_WORD], character);

    const int okay = prev_char_is_word ^ char_is_word ^ (opcode != VM_ANCHOR_WORD_BOUNDARY);

    return okay ? TS_CONTINUE : TS_REJECTED;
  }

  case VM_JUMP: {
    *instr_pointer += operand;
    assert(*instr_pointer <= vm->size);
    return TS_CONTINUE;
  }

  case VM_SPLIT_PASSIVE:
  case VM_SPLIT_EAGER:
  case VM_SPLIT_BACKWARDS_PASSIVE:
  case VM_SPLIT_BACKWARDS_EAGER: {
    size_t split_pointer;

    switch (opcode) {
    case VM_SPLIT_PASSIVE: {
      split_pointer = *instr_pointer + operand;
      break;
    }

    case VM_SPLIT_EAGER: {
      split_pointer = *instr_pointer;
      *instr_pointer += operand;
      break;
    }

    case VM_SPLIT_BACKWARDS_PASSIVE: {
      split_pointer = *instr_pointer - operand;
      break;
    }

    case VM_SPLIT_BACKWARDS_EAGER: {
      split_pointer = *instr_pointer;
      *instr_pointer -= operand;
      break;
    }

    default:
      UNREACHABLE();
    }

    assert(*instr_pointer <= vm->size);

    if (!split_thread(vm, split_pointer, thread)) {
      return TS_E_NOMEM;
    }

    return TS_CONTINUE;
  }

  case VM_WRITE_POINTER: {
    assert(operand < 2 * vm->n_capturing_groups);

    if (operand < vm->n_pointers) {
      POINTER_BUFFER(*vm, thread)[operand] = str;
    }

    return TS_CONTINUE;
  }

  case VM_TEST_AND_SET_FLAG: {
    assert(operand < vm->n_flags);
    return bitmap_test_and_set(FLAGS(*vm), operand) ? TS_REJECTED : TS_CONTINUE;
  }

  default:
    UNREACHABLE();
  }
}

WUR static vm_status_t
run_threads(vm_t *vm, step_function_t step, const char *str, int character, int prev_character) {
  bitmap_clear(FLAGS(*vm), vm->flags_size);

  if (vm->head == NULL_HANDLE && vm->matched_thread != NULL_HANDLE) {
    return VM_STATUS_DONE;
  }

  int thread_spawned = 0;

  vm_handle_t thread = vm->head;
  vm_handle_t prev_thread = NULL_HANDLE;

  for (;;) {
    assert(prev_thread == NULL_HANDLE || NEXT(*vm, prev_thread) == thread);

    if (thread == NULL_HANDLE) {
      // Don't spawn a new thread if we have already done so at this character position, or if
      // we've already found a match
      if (thread_spawned || vm->matched_thread != NULL_HANDLE) {
        break;
      }

      thread = spawn_thread(vm, prev_thread);

      if (thread == NULL_HANDLE) {
        return VM_STATUS_E_NOMEM;
      }

      thread_spawned = 1;
    }

    size_t instr_pointer = INSTR_POINTER(*vm, thread);

    thread_status_t thread_status;

    do {
      assert(instr_pointer <= vm->size);

      if (instr_pointer == vm->size) {
        thread_status = TS_MATCHED;
        break;
      }

      thread_status = step(vm, thread, &instr_pointer, str, character, prev_character);
    } while (thread_status == TS_CONTINUE);

    switch (thread_status) {
    case TS_REJECTED: {
      thread = destroy_thread(vm, thread, prev_thread);
      break;
    }

    case TS_MATCHED: {
      return on_match(vm, thread, prev_thread);
    }

    case TS_DONE: {
      INSTR_POINTER(*vm, thread) = instr_pointer;
      prev_thread = thread;
      thread = NEXT(*vm, thread);
      break;
    }

    case TS_E_NOMEM: {
      return VM_STATUS_E_NOMEM;
    }

    default:
      UNREACHABLE();
    }
  }

  return VM_STATUS_CONTINUE;
}

static vm_status_t on_match(vm_t *vm, vm_handle_t thread, vm_handle_t prev_thread) {
  // Short-circuit for boolean searches
  if (vm->n_pointers == 0) {
    vm->matched_thread = thread;
    return VM_STATUS_DONE;
  }

  // Deallocate the previously-matched thread, if any
  if (vm->matched_thread != NULL_HANDLE) {
    vm_free(vm, vm->matched_thread);
  }

  vm->matched_thread = thread;

  // We can discard any successor of thread, because any match coming from a
  // successor would be of lower priority

  vm_handle_t tail_thread = NEXT(*vm, thread);

  while (tail_thread != NULL_HANDLE) {
    vm_handle_t next_thread = NEXT(*vm, tail_thread);

    // Use vm_free rather than destroy_thread, because we needn't worry about maintaining outgoing
    // pointers
    vm_free(vm, tail_thread);

    tail_thread = next_thread;
  }

  // Manually remove the thread from the list, but don't destroy it
  if (prev_thread == NULL_HANDLE) {
    vm->head = NULL_HANDLE;
  } else {
    NEXT(*vm, prev_thread) = NULL_HANDLE;
  }

  return VM_STATUS_CONTINUE;
}
