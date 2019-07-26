#ifdef MATCH_BOOLEAN

#define NAME crex_is_match
#define RESULT_DECLARATION int *is_match

#elif defined(MATCH_LOCATION)

#define NAME crex_find
#define RESULT_DECLARATION crex_slice_t *match

#elif defined(MATCH_GROUPS)

#define NAME crex_match_groups
#define RESULT_DECLARATION crex_slice_t *matches

#endif

status_t
NAME(RESULT_DECLARATION, context_t *context, const regex_t *regex, const char *str, size_t size) {
  unsigned char *visited;

  const size_t visited_size = bitmap_size_for_bits(regex->size);

  if (context->visited_size < visited_size) {
    visited = ALLOC(&context->allocator, visited_size);

    if (visited == NULL) {
      return CREX_E_NOMEM;
    }

    FREE(&context->allocator, context->visited);
    context->visited = visited;
    context->visited_size = visited_size;
  } else {
    visited = context->visited;
  }

#ifdef MATCH_BOOLEAN
  const size_t n_pointers = 0;
#elif defined(MATCH_LOCATION)
  const size_t n_pointers = 2;
#elif defined(MATCH_GROUPS)
  const size_t n_pointers = 2 * regex->n_groups;
#endif

#ifdef MATCH_BOOLEAN
  *is_match = 0;
#endif

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
  int match_found = 0;
#endif

  // FIXME: tighten and justify this bound
  const size_t max_list_size = 2 * regex->size;

  state_list_t list;
  state_list_create(&list, context, n_pointers, max_list_size);

  const char *eof = str + size;
  int prev_character = -1;

  for (;;) {
    const int character = (str == eof) ? -1 : (unsigned char)(*str);

    bitmap_clear(visited, visited_size);

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
    if (list.head == STATE_LIST_EMPTY && match_found) {
      return CREX_OK;
    }
#endif

    int initial_state_pushed = 0;

    state_list_handle_t state = list.head;
    state_list_handle_t predecessor = STATE_LIST_EMPTY;

    for (;;) {
      if (state == STATE_LIST_EMPTY) {
        if (initial_state_pushed) {
          break;
        }

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
        if (match_found) {
          break;
        }
#endif

        if (!state_list_push_initial_state(&list, predecessor)) {
          return CREX_E_NOMEM;
        }

        // FIXME: this is nonsense
        if (predecessor == STATE_LIST_EMPTY) {
          state = list.head;
        } else {
          state = LIST_NEXT(&list, predecessor);
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

          for (size_t i = 0; i < regex->n_groups; i++) {
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

          state_list_handle_t tail = LIST_NEXT(&list, state);

          while (tail != STATE_LIST_EMPTY) {
            tail = state_list_pop(&list, state);
          }

          break;
#endif
        }

        if (bitmap_test(visited, instr_pointer)) {
          keep = 0;
        }

        bitmap_set(visited, instr_pointer);

        const unsigned char byte = regex->bytecode[instr_pointer++];
        const unsigned char opcode = VM_OPCODE(byte);
        const size_t operand_size = VM_OPERAND_SIZE(byte);

        if (opcode == VM_CHARACTER) {
          assert(operand_size == 1);
          assert(instr_pointer <= regex->size - 1);

          const unsigned char expected_character = regex->bytecode[instr_pointer++];

          if (character != expected_character) {
            keep = 0;
          }

          break;
        }

        if (opcode == VM_CHAR_CLASS) {
          assert(instr_pointer <= regex->size - operand_size);

          const size_t index =
              deserialize_unsigned_operand(regex->bytecode + instr_pointer, operand_size);
          instr_pointer += operand_size;

          keep = character != -1 && bitmap_test(regex->classes[index].bitmap, character);

          break;
        }

        if (opcode == VM_BUILTIN_CHAR_CLASS) {
          // <= 255 distinct builtin character classes
          assert(operand_size == 1);

          assert(instr_pointer <= regex->size - 1);

          const size_t index = regex->bytecode[instr_pointer++];
          keep = character != -1 && BCC_TEST(index, character);

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
          assert(operand_size != 0);
          assert(instr_pointer <= regex->size - operand_size);

          const long delta =
              deserialize_signed_operand(regex->bytecode + instr_pointer, operand_size);
          instr_pointer += operand_size;

          instr_pointer += delta;

          break;
        }

        case VM_SPLIT_PASSIVE:
        case VM_SPLIT_EAGER: {
          assert(operand_size != 0);
          assert(instr_pointer <= regex->size - operand_size);

          const long delta =
              deserialize_signed_operand(regex->bytecode + instr_pointer, operand_size);
          instr_pointer += operand_size;

          size_t split_pointer;

          if (opcode == VM_SPLIT_PASSIVE) {
            split_pointer = instr_pointer + delta;
          } else {
            split_pointer = instr_pointer;
            instr_pointer += delta;
          }

          if (!bitmap_test(visited, split_pointer)) {
            if (!state_list_push_copy(&list, state, split_pointer)) {
              return CREX_E_NOMEM;
            }
          }

          break;
        }

        case VM_WRITE_POINTER: {
          assert(operand_size != 0);

          const size_t index =
              deserialize_unsigned_operand(regex->bytecode + instr_pointer, operand_size);
          instr_pointer += operand_size;

#ifdef MATCH_BOOLEAN
          (void)index;
#elif defined(MATCH_LOCATION)
          const char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);

          if (index <= 1) {
            pointer_buffer[index] = str;
          }
#elif defined(MATCH_GROUPS)
          const char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);

          assert(index < n_pointers);
          pointer_buffer[index] = str;
#endif

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
    for (size_t i = 0; i < regex->n_groups; i++) {
      matches[i].begin = NULL;
      matches[i].end = NULL;
    }
#endif
  }

#endif

  return CREX_OK;
}

#undef MATCH_BOOLEAN
#undef MATCH_LOCATION
#undef MATCH_GROUPS
#undef NAME
#undef RESULT_DECLARATION
