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

status_t NAME(RESULT_DECLARATION,
              context_t *context,
              const regex_t *regex,
              const char *buffer,
              size_t buffer_size) {
  unsigned char *visited;

  const size_t min_visited_size = (regex->size + CHAR_BIT - 1) / CHAR_BIT;

  if (context->visited_size < min_visited_size) {
    visited = malloc(min_visited_size);

    if (visited == NULL) {
      return CREX_E_NOMEM;
    }

    free(context->visited);
    context->visited = visited;

    context->visited_size = min_visited_size;
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
  state_list_create(
      &list, context->list_buffer_size, context->list_buffer, n_pointers, max_list_size);

#define CLEANUP()                                                                                  \
  do {                                                                                             \
    context->list_buffer_size = list.capacity;                                                     \
    context->list_buffer = list.buffer;                                                            \
  } while (0)

  const char *eof = buffer + buffer_size;
  int prev_character = -1;

  for (;;) {
    const int character = (buffer == eof) ? -1 : (unsigned char)(*buffer);

    memset(visited, 0, min_visited_size);

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
    if (list.head == STATE_LIST_EMPTY && match_found) {
      CLEANUP();
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
          CLEANUP();
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
          CLEANUP();
          return CREX_OK;
#elif defined(MATCH_LOCATION)
          match_found = 1;

          char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);

          if (pointer_buffer[0] == NULL || pointer_buffer[1] == NULL) {
            match->size = 0;
            match->start = NULL;
          } else {
            match->size = pointer_buffer[1] - pointer_buffer[0];
            match->start = pointer_buffer[0];
          }
#elif defined(MATCH_GROUPS)
          match_found = 1;

          char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);

          for (size_t i = 0; i < regex->n_groups; i++) {
            if (pointer_buffer[2 * i] == NULL || pointer_buffer[2 * i + 1] == NULL) {
              matches[i].size = 0;
              matches[i].start = NULL;
            } else {
              matches[i].size = pointer_buffer[2 * i + 1] - pointer_buffer[2 * i];
              matches[i].start = pointer_buffer[2 * i];
            }
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

        const size_t byte_index = instr_pointer / CHAR_BIT;
        const size_t bit_index = instr_pointer % CHAR_BIT;

        if (visited[byte_index] & (1u << bit_index)) {
          keep = 0;
          break;
        }

        visited[byte_index] |= 1u << bit_index;

        const unsigned char code = regex->bytecode[instr_pointer++];

        if (code == VM_CHARACTER) {
          assert(instr_pointer <= regex->size - 1);

          const unsigned char expected_character = regex->bytecode[instr_pointer++];

          if (character != expected_character) {
            keep = 0;
          }

          break;
        }

        switch (code) {
        case VM_ANCHOR_BOF:
          if (prev_character != -1) {
            keep = 0;
          }
          break;

        case VM_ANCHOR_BOL:
          if (prev_character != -1 && prev_character != '\n') {
            keep = 0;
          }
          break;

        case VM_ANCHOR_EOF:
          if (character != -1) {
            keep = 0;
          }
          break;

        case VM_ANCHOR_EOL:
          if (character != -1 && character != '\n') {
            keep = 0;
          }
          break;

        case VM_ANCHOR_WORD_BOUNDARY:
          assert(0 && "FIXME");
          break;

        case VM_ANCHOR_NOT_WORD_BOUNDARY:
          assert(0 && "FIXME");
          break;

        case VM_JUMP: {
          assert(instr_pointer <= regex->size - 4);

          const long delta = deserialize_long(regex->bytecode + instr_pointer, 4);
          instr_pointer += 4;

          instr_pointer += delta;

          break;
        }

        case VM_SPLIT_PASSIVE:
        case VM_SPLIT_EAGER: {
          assert(instr_pointer <= regex->size - 4);

          const long delta = deserialize_long(regex->bytecode + instr_pointer, 4);
          instr_pointer += 4;

          size_t split_pointer;

          if (code == VM_SPLIT_PASSIVE) {
            split_pointer = instr_pointer + delta;
          } else {
            split_pointer = instr_pointer;
            instr_pointer += delta;
          }

          if (!((visited[split_pointer / CHAR_BIT] >> (split_pointer % CHAR_BIT)) & 1u)) {
            if (!state_list_push_copy(&list, state, split_pointer)) {
              CLEANUP();
              return CREX_E_NOMEM;
            }
          }

          break;
        }

        case VM_WRITE_POINTER: {
          // FIXME: signedness
          const size_t index = (size_t)deserialize_long(regex->bytecode + instr_pointer, 4);
          instr_pointer += 4;

#ifdef MATCH_BOOLEAN
          (void)index;
#elif defined(MATCH_LOCATION)
          char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);

          if (index <= 1) {
            pointer_buffer[index] = buffer;
          }
#elif defined(MATCH_GROUPS)
          char **pointer_buffer = LIST_POINTER_BUFFER(&list, state);

          assert(index < n_pointers);
          pointer_buffer[index] = buffer;
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

    buffer++;
  }

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)

  if(!match_found) {
#if defined(MATCH_LOCATION)
    match->size = 0;
    match->start = NULL;
#else
    for (size_t i = 0; i < regex->n_groups; i++) {
      matches[i].size = 0;
      matches[i].start = NULL;
    }
#endif
  }

#endif

  CLEANUP();
  return CREX_OK;
}

#undef MATCH_BOOLEAN
#undef MATCH_LOCATION
#undef MATCH_GROUPS
#undef NAME
#undef RESULT_DECLARATION
#undef N_GROUPS
