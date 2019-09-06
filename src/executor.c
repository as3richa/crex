WARN_UNUSED_RESULT static status_t execute_regex(void *result,
                                                 context_t *context,
                                                 const regex_t *regex,
                                                 const char *str,
                                                 size_t size,
                                                 size_t n_pointers) {
  assert(n_pointers == 0 || n_pointers == 2 || n_pointers == 2 * regex->n_capturing_groups);

  handle_t matched_state = HANDLE_NULL;

  state_list_t list;
  state_list_init(&list, context, n_pointers);

  internal_allocator_t *allocator = &list.allocator;

  const size_t flags_size = bitmap_size_for_bits(regex->n_flags);
  const handle_t flags = internal_allocator_alloc(allocator, flags_size);

  if (flags == HANDLE_NULL) {
    return CREX_E_NOMEM;
  }

#define FLAGS (context->buffer + flags)

#ifndef NDEBUG
  // The compiler statically guarantees that no instruction can ever be executed twice, via
  // VM_TEST_AND_SET_FLAG. Moreover, it does so much more efficiently than the naive solution
  // (tracking every instruction in a bitmap). In development, we can check the compiler's
  // correctness by also tracking execution naively

  const size_t visited_size = bitmap_size_for_bits(regex->bytecode.size);
  const handle_t visited = internal_allocator_alloc(allocator, visited_size);

  if (visited == HANDLE_NULL) {
    return CREX_E_NOMEM;
  }

#define VISITED (context->buffer + visited)

#endif

  const char *eof = str + size;
  int prev_character = -1;

  for (;;) {
    const int character = (str == eof) ? -1 : (unsigned char)(*str);

    bitmap_clear(FLAGS, flags_size);

#ifndef NDEBUG
    bitmap_clear(VISITED, visited_size);
#endif

    if (list.head == HANDLE_NULL && matched_state != HANDLE_NULL) {
      break;
    }

    int initial_state_pushed = 0;

    handle_t state = list.head;
    handle_t predecessor = HANDLE_NULL;

    for (;;) {
      assert(predecessor == HANDLE_NULL || LIST_NEXT(&list, predecessor) == state);

      if (state == HANDLE_NULL) {
        if (initial_state_pushed) {
          break;
        }

        // Don't push the initial state if we've already found a match
        if (matched_state != HANDLE_NULL) {
          break;
        }

        state = state_list_push_initial_state(&list, predecessor);

        if (state == HANDLE_NULL) {
          return CREX_E_NOMEM;
        }

        initial_state_pushed = 1;
      }

      size_t instr_pointer = LIST_INSTR_POINTER(&list, state);

      int keep = 1;

      for (;;) {
        if (instr_pointer == regex->bytecode.size) {
          if (n_pointers == 0) {
            int *is_match = result;
            *is_match = 1;
            return CREX_OK;
          }

          // Deallocate the previously-matched state, if any
          if (matched_state != HANDLE_NULL) {
            internal_allocator_free(allocator, matched_state);
          }

          matched_state = state;

          // We can safely discard any successor of state, because any match coming from a successor
          // would be of lower priority. We can't deallocate state itself, however, because we'll
          // need to copy its pointers later

          handle_t tail = LIST_NEXT(&list, state);

          while (tail != HANDLE_NULL) {
            tail = state_list_pop(&list, state);
          }

          if (predecessor == HANDLE_NULL) {
            list.head = HANDLE_NULL;
          } else {
            LIST_NEXT(&list, predecessor) = HANDLE_NULL;
          }

          break;
        }

        const unsigned char byte = regex->bytecode.code[instr_pointer++];

        const unsigned char opcode = VM_OPCODE(byte);
        const size_t operand_size = VM_OPERAND_SIZE(byte);

        // FIMXE: think harder about this
        assert(opcode == VM_TEST_AND_SET_FLAG || !bitmap_test_and_set(VISITED, instr_pointer - 1));

        assert(instr_pointer <= regex->bytecode.size - operand_size);
        const size_t operand =
            deserialize_operand(regex->bytecode.code + instr_pointer, operand_size);
        instr_pointer += operand_size;

        if (opcode == VM_CHARACTER) {
          assert(operand <= 0xffu);

          // FIXME: make character/prev_character size_t?
          if ((size_t)character != operand) {
            keep = 0;
          }

          break;
        }

        if (opcode == VM_CHAR_CLASS) {
          keep = character != -1 && bitmap_test(regex->classes[operand], character);
          break;
        }

        if (opcode == VM_BUILTIN_CHAR_CLASS) {
          assert(operand < N_BUILTIN_CLASSES);
          keep = character != -1 && bitmap_test(builtin_classes[operand], character);
          break;
        }

        switch (opcode) {
        case VM_ANCHOR_BOF: {
          keep = prev_character == -1;
          break;
        }

        case VM_ANCHOR_BOL: {
          keep = prev_character == -1 || prev_character == '\n';
          break;
        }

        case VM_ANCHOR_EOF: {
          keep = character == -1;
          break;
        }

        case VM_ANCHOR_EOL: {
          keep = character == -1 || character == '\n';
          break;
        }

        case VM_ANCHOR_WORD_BOUNDARY:
        case VM_ANCHOR_NOT_WORD_BOUNDARY: {
          const int prev_char_is_word =
              prev_character != -1 && bitmap_test(builtin_classes[BCC_WORD], prev_character);

          const int char_is_word =
              character != -1 && bitmap_test(builtin_classes[BCC_WORD], character);

          keep = prev_char_is_word ^ char_is_word ^ (opcode == VM_ANCHOR_NOT_WORD_BOUNDARY);

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

          if (!state_list_push_copy(&list, state, split_pointer)) {
            return CREX_E_NOMEM;
          }

          break;
        }

        case VM_WRITE_POINTER: {
          assert(operand < 2 * regex->n_capturing_groups);

          if (operand < n_pointers) {
            const char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);
            pointer_buffer[operand] = str;
          }

          break;
        }

        case VM_TEST_AND_SET_FLAG: {
          assert(operand < regex->n_flags);

          if (bitmap_test_and_set(FLAGS, operand)) {
            keep = 0;
            break;
          }

          break;
        }

        default:
          UNREACHABLE();
        }

        if (!keep) {
          break;
        }
      }

      if (keep) {
        LIST_INSTR_POINTER(&list, state) = instr_pointer;
        predecessor = state;
        state = LIST_NEXT(&list, state);
      } else {
        state = state_list_pop(&list, predecessor);
      }
    }

    if (character == -1) {
      break;
    }

    prev_character = character;
    str++;
  }

#undef FLAGS
#undef VISITED

  if (n_pointers == 0) {
    int *is_match = result;
    *is_match = 0;
  } else {
    match_t *matches = result;

    if (matched_state == HANDLE_NULL) {
      for (size_t i = 0; i < n_pointers / 2; i++) {
        matches[i].begin = NULL;
        matches[i].end = NULL;
      }
    } else {
      const char **pointer_buffer = LIST_POINTER_BUFFER(&list, matched_state);

      for (size_t i = 0; i < n_pointers / 2; i++) {
        matches[i].begin = pointer_buffer[2 * i];
        matches[i].end = pointer_buffer[2 * i + 1];

        assert(matches[i].begin <= matches[i].end);
        assert((matches[i].begin == NULL) == (matches[i].end == NULL));
      }
    }
  }

  return CREX_OK;
}
