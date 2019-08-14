#ifdef MATCH_BOOLEAN

#define NAME crex_is_match
#define RESULT int *is_match

#elif defined(MATCH_LOCATION)

#define NAME crex_find
#define RESULT match_t *match

#elif defined(MATCH_GROUPS)

#define NAME crex_match_groups
#define RESULT match_t *matches

#endif

PUBLIC status_t
NAME(RESULT, context_t *context, const regex_t *regex, const char *str, size_t size) {
#ifdef MATCH_BOOLEAN
  const size_t n_pointers = 0;
#elif defined(MATCH_LOCATION)
  const size_t n_pointers = 2;
#elif defined(MATCH_GROUPS)
  const size_t n_pointers = 2 * regex->n_capturing_groups;
#endif

#ifdef MATCH_BOOLEAN
  *is_match = 0;
#endif

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
  int match_found = 0;
#endif

  state_list_t list;
  state_list_init(&list, context, n_pointers);

  const size_t flags_size = bitmap_size_for_bits(regex->n_flags);
  const handle_t flags = internal_allocator_alloc(&list.allocator, flags_size);

  if (flags == HANDLE_NULL) {
    return CREX_E_NOMEM;
  }

#define FLAGS (unsigned char *)(context->buffer + flags)

#ifndef NDEBUG
  // The compiler statically guarantees that no instruction can ever be executed twice, via
  // VM_TEST_AND_SET_FLAG. Moreover, it does so much more efficiently than the naive solution
  // (tracking every instruction in a bitmap). In development, we can check the compiler's
  // correctness by also tracking execution naively

  const size_t visited_size = bitmap_size_for_bits(regex->size);
  const handle_t visited = internal_allocator_alloc(&list.allocator, visited_size);

  if (visited == HANDLE_NULL) {
    return CREX_E_NOMEM;
  }

#define VISITED (unsigned char *)(context->buffer + visited)

#endif

  const char *eof = str + size;
  int prev_character = -1;

  for (;;) {
    const int character = (str == eof) ? -1 : (unsigned char)(*str);

    bitmap_clear(FLAGS, flags_size);

#ifndef NDEBUG
    bitmap_clear(VISITED, visited_size);
#endif

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
    if (list.head == HANDLE_NULL && match_found) {
      return CREX_OK;
    }
#endif

    int initial_state_pushed = 0;

    handle_t state = list.head;
    handle_t predecessor = HANDLE_NULL;

    for (;;) {
      assert(predecessor == HANDLE_NULL || LIST_NEXT(&list, predecessor) == state);

      if (state == HANDLE_NULL) {
        if (initial_state_pushed) {
          break;
        }

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
        if (match_found) {
          break;
        }
#endif

        state = state_list_push_initial_state(&list, predecessor);

        if (state == HANDLE_NULL) {
          return CREX_E_NOMEM;
        }

        initial_state_pushed = 1;
      }

      size_t instr_pointer = LIST_INSTR_POINTER(&list, state);

      int keep = 1;

      for (;;) {
        if (instr_pointer == regex->size) {

#ifdef MATCH_BOOLEAN
          *is_match = 1;
          return CREX_OK;
#elif defined(MATCH_LOCATION)
          match_found = 1;

          const char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);

          if (pointer_buffer[0] == NULL || pointer_buffer[1] == NULL) {
            match->begin = NULL;
            match->end = NULL;
          } else {
            match->begin = pointer_buffer[0];
            match->end = pointer_buffer[1];
          }
#elif defined(MATCH_GROUPS)
          match_found = 1;

          const char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);

          for (size_t i = 0; i < regex->n_capturing_groups; i++) {
            if (pointer_buffer[0] == NULL || pointer_buffer[1] == NULL) {
              matches[i].begin = NULL;
              matches[i].end = NULL;
            } else {
              matches[i].begin = pointer_buffer[0];
              matches[i].end = pointer_buffer[1];
            }

            pointer_buffer += 2;
          }
#endif

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
          keep = 0;

          handle_t tail = LIST_NEXT(&list, state);

          while (tail != HANDLE_NULL) {
            tail = state_list_pop(&list, state);
          }

          break;
#endif
        }

        const unsigned char byte = regex->bytecode[instr_pointer++];

        const unsigned char opcode = VM_OPCODE(byte);
        const size_t operand_size = VM_OPERAND_SIZE(byte);

        // FIMXE: think harder about this
        assert(opcode == VM_TEST_AND_SET_FLAG || !bitmap_test_and_set(VISITED, instr_pointer - 1));

        assert(instr_pointer <= regex->size - operand_size);
        const size_t operand = deserialize_operand(regex->bytecode + instr_pointer, operand_size);
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
          keep = character != -1 && bitmap_test(regex->classes[operand].bitmap, character);
          break;
        }

        if (opcode == VM_BUILTIN_CHAR_CLASS) {
          assert(operand <= N_BUILTIN_CLASSES);
          keep = character != -1 && BCC_TEST(operand, character);
          break;
        }

        switch (opcode) {
        case VM_ANCHOR_BOF:
          keep = prev_character == -1;
          break;

        case VM_ANCHOR_BOL:
          keep = prev_character == -1 || prev_character == '\n';
          break;

        case VM_ANCHOR_EOF:
          keep = character == -1;
          break;

        case VM_ANCHOR_EOL:
          keep = character == -1 || character == '\n';
          break;

        case VM_ANCHOR_WORD_BOUNDARY:
        case VM_ANCHOR_NOT_WORD_BOUNDARY: {
          const int prev_is_word = prev_character != -1 && BCC_TEST(BCC_WORD, prev_character);
          const int char_is_word = character != -1 && BCC_TEST(BCC_WORD, character);
          keep = prev_is_word ^ char_is_word ^ (opcode == VM_ANCHOR_NOT_WORD_BOUNDARY);
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

          assert(instr_pointer <= regex->size);
          assert(split_pointer <= regex->size);

          if (!state_list_push_copy(&list, state, split_pointer)) {
            return CREX_E_NOMEM;
          }

          break;
        }

        case VM_WRITE_POINTER: {
#ifdef MATCH_BOOLEAN
          (void)operand;
#elif defined(MATCH_LOCATION)
          const char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);

          if (operand <= 1) {
            pointer_buffer[operand] = str;
          }
#elif defined(MATCH_GROUPS)
          const char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);

          assert(operand < n_pointers);
          pointer_buffer[operand] = str;
#endif

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
          assert(0);
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

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)

  if (!match_found) {
#if defined(MATCH_LOCATION)
    match->begin = NULL;
    match->end = NULL;
#else
    for (size_t i = 0; i < regex->n_capturing_groups; i++) {
      matches[i].begin = NULL;
      matches[i].end = NULL;
    }
#endif
  }

#endif

  return CREX_OK;
}

#undef FLAGS
#undef VISITED
#undef MATCH_BOOLEAN
#undef MATCH_LOCATION
#undef MATCH_GROUPS
#undef NAME
#undef RESULT
