typedef size_t handle_t;

#define NULL_HANDLE SIZE_MAX

typedef struct {
  context_t *context;
  size_t n_pointers;

  size_t capacity;
  unsigned char *buffer;

  size_t bump_pointer;
  handle_t freelist;

  size_t head;

  handle_t matched_thread;

  size_t size;
  const unsigned char *code;

  char_class_t *classes;

  size_t flags_size;

#ifndef NDEBUG
  size_t n_capturing_groups;
  size_t n_classes;
  size_t n_flags;
#endif
} executor_t;

typedef enum { TS_CONTINUE, TS_REJECTED, TS_MATCHED, TS_DONE, TS_E_NOMEM } thread_status_t;

typedef enum { EX_CONTINUE, EX_DONE, EX_E_NOMEM } executor_status_t;

static const size_t block_size = sizeof(union {
  size_t size;
  void *pointer;
});

#define NEXT(ex, handle) (*(size_t *)((ex).buffer + (handle)))
#define INSTR_POINTER(ex, handle) (*(size_t *)((ex).buffer + (handle) + block_size))
#define POINTER_BUFFER(ex, handle) ((const char **)((ex).buffer + (handle) + 2 * block_size))

#define THREAD_SIZE(ex) (block_size * (2 + (ex).n_pointers))

#define FLAGS(ex) (unsigned char *)(ex).buffer

WARN_UNUSED_RESULT static handle_t executor_allocate(executor_t *ex, size_t size) {
  // Round size up to the nearest multiple of the required alignment
  size = (size + block_size - 1) / block_size * block_size;

  if (ex->bump_pointer + size <= ex->capacity) {
    const handle_t result = ex->bump_pointer;
    ex->bump_pointer += size;

    return result;
  }

  if (ex->freelist != NULL_HANDLE) {
    const handle_t result = ex->freelist;
    ex->freelist = NEXT(*ex, ex->freelist);

    return result;
  }

  const allocator_t *allocator = &ex->context->allocator;

  const size_t capacity = 2 * ex->capacity + size;
  unsigned char *buffer = ALLOC(allocator, capacity);

  if (buffer == NULL) {
    return NULL_HANDLE;
  }

  safe_memcpy(buffer, ex->buffer, ex->bump_pointer);
  FREE(allocator, ex->buffer);

  ex->capacity = capacity;
  ex->buffer = buffer;

  ex->context->capacity = capacity;
  ex->context->buffer = buffer;

  const handle_t result = ex->bump_pointer;
  ex->bump_pointer += size;

  return result;
}

static void executor_free(executor_t *ex, handle_t handle) {
  NEXT(*ex, handle) = ex->freelist;
  ex->freelist = handle;
}

WARN_UNUSED_RESULT static int
create_executor(executor_t *ex, context_t *context, const regex_t *regex, size_t n_pointers) {
  assert(n_pointers == 0 || n_pointers == 2 || n_pointers == 2 * regex->n_capturing_groups);

  ex->context = context;
  ex->n_pointers = n_pointers;

  ex->buffer = context->buffer;
  ex->capacity = context->capacity;
  ex->bump_pointer = 0;
  ex->freelist = NULL_HANDLE;

  ex->head = NULL_HANDLE;

  ex->matched_thread = NULL_HANDLE;

  ex->size = regex->bytecode.size;
  ex->code = BYTECODE_CODE(regex->bytecode);

  ex->classes = regex->classes;

  ex->flags_size = bitmap_size_for_bits(regex->n_flags);

#ifndef NDEBUG
  ex->n_capturing_groups = regex->n_capturing_groups;
  ex->n_classes = regex->n_classes;
  ex->n_flags = regex->n_flags;
#endif

  const handle_t flags = executor_allocate(ex, ex->flags_size);

  if (flags == NULL_HANDLE) {
    return 0;
  }

  assert(flags == 0);

  return 1;
}

WARN_UNUSED_RESULT static handle_t spawn_thread(executor_t *ex, handle_t prev_thread) {
  const handle_t thread = executor_allocate(ex, THREAD_SIZE(*ex));

  if (thread == NULL_HANDLE) {
    return NULL_HANDLE;
  }

  NEXT(*ex, thread) = NULL_HANDLE;
  INSTR_POINTER(*ex, thread) = 0;

  for (size_t i = 0; i < ex->n_pointers; i++) {
    POINTER_BUFFER(*ex, thread)[i] = NULL;
  }

  if (prev_thread == NULL_HANDLE) {
    ex->head = thread;
  } else {
    NEXT(*ex, prev_thread) = thread;
  }

  return thread;
}

WARN_UNUSED_RESULT static int
split_thread(executor_t *ex, handle_t prev_thread, size_t instr_pointer) {
  const handle_t thread = executor_allocate(ex, THREAD_SIZE(*ex));

  if (thread == NULL_HANDLE) {
    return 0;
  }

  assert(prev_thread != NULL_HANDLE);

  NEXT(*ex, thread) = NEXT(*ex, prev_thread);
  INSTR_POINTER(*ex, thread) = instr_pointer;

  const size_t size = block_size * ex->n_pointers;
  memcpy(POINTER_BUFFER(*ex, thread), POINTER_BUFFER(*ex, prev_thread), size);

  NEXT(*ex, prev_thread) = thread;

  return 1;
}

static handle_t remove_thread(executor_t *ex, handle_t thread, handle_t prev_thread) {
  handle_t next_thread = NEXT(*ex, thread);

  if (prev_thread == NULL_HANDLE) {
    ex->head = next_thread;
  } else {
    NEXT(*ex, prev_thread) = next_thread;
  }

  return next_thread;
}

static handle_t destroy_thread(executor_t *ex, handle_t thread, handle_t prev_thread) {
  handle_t next_thread = remove_thread(ex, thread, prev_thread);

  executor_free(ex, thread);

  return next_thread;
}

typedef thread_status_t (*step_function_t)(executor_t *, size_t, size_t *, const char *, int, int);

WARN_UNUSED_RESULT static executor_status_t execute_threads(
    executor_t *ex, step_function_t step, const char *str, int character, int prev_character) {
  bitmap_clear(FLAGS(*ex), ex->flags_size);

  if (ex->head == NULL_HANDLE && ex->matched_thread != NULL_HANDLE) {
    return EX_DONE;
  }

  int thread_spawned = 0;

  handle_t thread = ex->head;
  handle_t prev_thread = NULL_HANDLE;

  for (;;) {
    assert(prev_thread == NULL_HANDLE || NEXT(*ex, prev_thread) == thread);

    if (thread == NULL_HANDLE) {
      // Don't spawn a new thread if we have already done so at this character position, or if
      // we've already found a match
      if (thread_spawned || ex->matched_thread != NULL_HANDLE) {
        break;
      }

      thread = spawn_thread(ex, prev_thread);

      if (thread == NULL_HANDLE) {
        return EX_E_NOMEM;
      }

      thread_spawned = 1;
    }

    size_t instr_pointer = INSTR_POINTER(*ex, thread);

    thread_status_t thread_status;

    do {
      assert(instr_pointer <= ex->size);

      if (instr_pointer == ex->size) {
        thread_status = TS_MATCHED;
        break;
      }

      thread_status = step(ex, thread, &instr_pointer, str, character, prev_character);
    } while (thread_status == TS_CONTINUE);

    switch (thread_status) {
    case TS_REJECTED: {
      thread = destroy_thread(ex, thread, prev_thread);
      break;
    }

    case TS_MATCHED: {
      // Short-circuit for boolean searches
      if (ex->n_pointers == 0) {
        ex->matched_thread = thread;
        return EX_DONE;
      }

      // Deallocate the previously-matched thread, if any
      if (ex->matched_thread != NULL_HANDLE) {
        executor_free(ex, ex->matched_thread);
      }

      ex->matched_thread = thread;

      // We can discard any successor of thread, because any match coming from a
      // successor would be of lower priority

      // FIXME: free rather than destroy to avoid touch thread's next pointer

      handle_t tail = NEXT(*ex, thread);

      while (tail != NULL_HANDLE) {
        tail = destroy_thread(ex, tail, thread);
      }

      // Remove the matching thread from the list of active threads (but don't destroy it)
      thread = remove_thread(ex, thread, prev_thread);

      break;
    }

    case TS_DONE: {
      INSTR_POINTER(*ex, thread) = instr_pointer;
      prev_thread = thread;
      thread = NEXT(*ex, thread);
      break;
    }

    case TS_E_NOMEM: {
      return EX_E_NOMEM;
    }

    default:
      UNREACHABLE();
    }
  }

  return EX_CONTINUE;
}

WARN_UNUSED_RESULT static thread_status_t step_thread(executor_t *ex,
                                                      handle_t thread,
                                                      size_t *instr_pointer,
                                                      const char *str,
                                                      int character,
                                                      int prev_character) {
  assert(*instr_pointer < ex->size);

  const unsigned char byte = ex->code[(*instr_pointer)++];

  const unsigned char opcode = VM_OPCODE(byte);
  const size_t operand_size = VM_OPERAND_SIZE(byte);

  assert(*instr_pointer + operand_size <= ex->size);

  const size_t operand = deserialize_operand(ex->code + *instr_pointer, operand_size);
  *instr_pointer += operand_size;

  switch (opcode) {
  case VM_CHARACTER: {
    assert(operand <= 0xffu);
    return ((size_t)character == operand) ? TS_DONE : TS_REJECTED;
  }

  case VM_CHAR_CLASS: {
    assert(operand < ex->n_classes);
    const int okay = character != -1 && bitmap_test(ex->classes[operand], character);
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
    assert(*instr_pointer <= ex->size);
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

    assert(*instr_pointer <= ex->size);
    assert(split_pointer <= ex->size);

    if (!split_thread(ex, thread, split_pointer)) {
      return TS_E_NOMEM;
    }

    return TS_CONTINUE;
  }

  case VM_WRITE_POINTER: {
    assert(operand < 2 * ex->n_capturing_groups);

    if (operand < ex->n_pointers) {
      POINTER_BUFFER(*ex, thread)[operand] = str;
    }

    return TS_CONTINUE;
  }

  case VM_TEST_AND_SET_FLAG: {
    assert(operand < ex->n_flags);
    return bitmap_test_and_set(FLAGS(*ex), operand) ? TS_REJECTED : TS_CONTINUE;
  }

  default:
    UNREACHABLE();
  }
}

WARN_UNUSED_RESULT static status_t execute_regex(void *result,
                                                 context_t *context,
                                                 const regex_t *regex,
                                                 const char *str,
                                                 size_t size,
                                                 size_t n_pointers) {
  executor_t ex;

  if (!create_executor(&ex, context, regex, n_pointers)) {
    return CREX_E_NOMEM;
  }

  const char *eof = str + size;
  int prev_character = -1;

  for (;;) {
    const int character = (str == eof) ? -1 : (unsigned char)(*str);

    executor_status_t ex_status = execute_threads(&ex, step_thread, str, character, prev_character);

    if (ex_status == EX_DONE) {
      break;
    }

    if (ex_status == EX_E_NOMEM) {
      return CREX_E_NOMEM;
    }

    assert(ex_status == EX_CONTINUE);

    if (character == -1) {
      break;
    }

    prev_character = character;
    str++;
  }

  if (n_pointers == 0) {
    int *is_match = result;
    *is_match = ex.matched_thread != NULL_HANDLE;
    return CREX_OK;
  }

  match_t *matches = result;

  if (ex.matched_thread == NULL_HANDLE) {
    for (size_t i = 0; i < n_pointers / 2; i++) {
      matches[i].begin = NULL;
      matches[i].end = NULL;
    }

    return CREX_OK;
  }

  memcpy(matches, POINTER_BUFFER(ex, ex.matched_thread), sizeof(const char *) * n_pointers);

  return CREX_OK;
}
