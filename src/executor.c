typedef size_t handle_t;

#define NULL_HANDLE SIZE_MAX

typedef struct {
  context_t *context;
  size_t n_pointers;

  size_t capacity;
  unsigned char *buffer;

  size_t bump_pointer;
  handle_t freelist;

  size_t flags_size;

  size_t head;
} executor_t;

static const size_t block_size = sizeof(union {
  size_t size;
  void *pointer;
});

typedef enum { TS_REJECTED, TS_MATCHED, TS_OKAY } thread_status_t;

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
  ex->flags_size = bitmap_size_for_bits(regex->n_flags);
  ex->head = NULL_HANDLE;

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

  const unsigned char *code = BYTECODE_CODE(regex->bytecode);

  handle_t matched_thread = NULL_HANDLE;

  const char *eof = str + size;
  int prev_character = -1;

  for (;;) {
    const int character = (str == eof) ? -1 : (unsigned char)(*str);

    bitmap_clear(FLAGS(ex), ex.flags_size);

    if (ex.head == NULL_HANDLE && matched_thread != NULL_HANDLE) {
      break;
    }

    int thread_spawned = 0;

    handle_t thread = ex.head;
    handle_t prev_thread = NULL_HANDLE;

    for (;;) {
      assert(prev_thread == NULL_HANDLE || NEXT(ex, prev_thread) == thread);

      if (thread == NULL_HANDLE) {
        // Don't spawn a new thread if we have already done so at this character position, or if
        // we've already found a match
        if (thread_spawned || matched_thread != NULL_HANDLE) {
          break;
        }

        thread = spawn_thread(&ex, prev_thread);

        if (thread == NULL_HANDLE) {
          return CREX_E_NOMEM;
        }

        thread_spawned = 1;
      }

      size_t instr_pointer = INSTR_POINTER(ex, thread);

      thread_status_t thread_status = TS_OKAY;

      for (;;) {
        if (instr_pointer == regex->bytecode.size) {
          if (n_pointers == 0) {
            int *is_match = result;
            *is_match = 1;
            return CREX_OK;
          }

          thread_status = TS_MATCHED;
          break;
        }

        const unsigned char byte = code[instr_pointer++];

        const unsigned char opcode = VM_OPCODE(byte);
        const size_t operand_size = VM_OPERAND_SIZE(byte);

        assert(instr_pointer <= regex->bytecode.size - operand_size);

        const size_t operand = deserialize_operand(code + instr_pointer, operand_size);
        instr_pointer += operand_size;

        if (opcode == VM_CHARACTER) {
          assert(operand <= 0xffu);

          if ((size_t)character != operand) {
            thread_status = TS_REJECTED;
          }

          break;
        }

        if (opcode == VM_CHAR_CLASS) {
          if (character == -1 || !bitmap_test(regex->classes[operand], character)) {
            thread_status = TS_REJECTED;
          }
          break;
        }

        if (opcode == VM_BUILTIN_CHAR_CLASS) {
          if (character == -1 || !bitmap_test(builtin_classes[operand], character)) {
            thread_status = TS_REJECTED;
          }
          break;
        }

        switch (opcode) {
        case VM_ANCHOR_BOF: {
          if (prev_character != -1) {
            thread_status = TS_REJECTED;
          }
          break;
        }

        case VM_ANCHOR_BOL: {
          if (prev_character != -1 && prev_character != '\n') {
            thread_status = TS_REJECTED;
          }
          break;
        }

        case VM_ANCHOR_EOF: {
          if (character != -1) {
            thread_status = TS_REJECTED;
          }
          break;
        }

        case VM_ANCHOR_EOL: {
          if (character != -1 && character != '\n') {
            thread_status = TS_REJECTED;
          }
          break;
        }

        case VM_ANCHOR_WORD_BOUNDARY:
        case VM_ANCHOR_NOT_WORD_BOUNDARY: {
          const int prev_char_is_word =
              prev_character != -1 && bitmap_test(builtin_classes[BCC_WORD], prev_character);

          const int char_is_word =
              character != -1 && bitmap_test(builtin_classes[BCC_WORD], character);

          if (prev_char_is_word ^ char_is_word ^ (opcode == VM_ANCHOR_WORD_BOUNDARY)) {
            thread_status = TS_REJECTED;
          }

          break;
        }

        case VM_JUMP: {
          instr_pointer += operand;
          break;
        }

        case VM_SPLIT_PASSIVE:
        case VM_SPLIT_EAGER:
        case VM_SPLIT_BACKWARDS_PASSIVE:
        case VM_SPLIT_BACKWARDS_EAGER: {
          size_t split_pointer;

          switch (opcode) {
          case VM_SPLIT_PASSIVE:
            split_pointer = instr_pointer + operand;
            break;

          case VM_SPLIT_EAGER:
            split_pointer = instr_pointer;
            instr_pointer += operand;
            break;

          case VM_SPLIT_BACKWARDS_PASSIVE:
            split_pointer = instr_pointer - operand;
            break;

          case VM_SPLIT_BACKWARDS_EAGER:
            split_pointer = instr_pointer;
            instr_pointer -= operand;
            break;
          }

          assert(instr_pointer <= regex->bytecode.size);
          assert(split_pointer <= regex->bytecode.size);

          if (!split_thread(&ex, thread, split_pointer)) {
            return CREX_E_NOMEM;
          }

          break;
        }

        case VM_WRITE_POINTER: {
          assert(operand < 2 * regex->n_capturing_groups);

          if (operand < n_pointers) {
            POINTER_BUFFER(ex, thread)[operand] = str;
          }

          break;
        }

        case VM_TEST_AND_SET_FLAG: {
          assert(operand < regex->n_flags);

          if (bitmap_test_and_set(FLAGS(ex), operand)) {
            thread_status = TS_REJECTED;
          }

          break;
        }

        default:
          UNREACHABLE();
        }

        if (thread_status != TS_OKAY) {
          break;
        }
      }

      switch (thread_status) {
      case TS_MATCHED: {
        // Deallocate the previously-matched thread, if any
        if (matched_thread != NULL_HANDLE) {
          executor_free(&ex, matched_thread);
        }

        matched_thread = thread;

        // We can discard any successor of thread, because any match coming from a
        // successor would be of lower priority

        handle_t tail = NEXT(ex, thread);

        while (tail != NULL_HANDLE) {
          const handle_t next = NEXT(ex, tail);
          executor_free(&ex, tail);
          tail = next;
        }

        if (prev_thread == NULL_HANDLE) {
          ex.head = NULL_HANDLE;
        } else {
          NEXT(ex, prev_thread) = NULL_HANDLE;
        }

        break;
      }

      case TS_REJECTED: {
        thread = NEXT(ex, thread);

        if (prev_thread == NULL_HANDLE) {
          ex.head = thread;
        } else {
          NEXT(ex, prev_thread) = thread;
        }

        break;
      }

      case TS_OKAY: {
        INSTR_POINTER(ex, thread) = instr_pointer;

        prev_thread = thread;
        thread = NEXT(ex, thread);

        break;
      }

      default:
        UNREACHABLE();
      }

      if (thread_status == TS_MATCHED) {
        break;
      }
    }

    if (character == -1) {
      break;
    }

    prev_character = character;
    str++;
  }

#undef FLAGS

  if (n_pointers == 0) {
    int *is_match = result;
    *is_match = 0;
    return CREX_OK;
  }

  match_t *matches = result;

  if (matched_thread == NULL_HANDLE) {
    for (size_t i = 0; i < n_pointers / 2; i++) {
      matches[i].begin = NULL;
      matches[i].end = NULL;
    }

    return CREX_OK;
  }

  for (size_t i = 0; i < n_pointers / 2; i++) {
    matches[i].begin = POINTER_BUFFER(ex, matched_thread)[2 * i];
    matches[i].end = POINTER_BUFFER(ex, matched_thread)[2 * i + 1];

    assert(matches[i].begin <= matches[i].end);
    assert((matches[i].begin == NULL) == (matches[i].end == NULL));
  }

  return CREX_OK;
}
