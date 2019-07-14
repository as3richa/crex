#ifdef MATCH_BOOLEAN

#define NAME crex_is_match
#define RESULT_DECLARATION int *is_match

#elif defined(MATCH_LOCATION)

#define NAME crex_find
#define RESULT_DECLARATION crex_slice_t *match
#define N_GROUPS 1

#elif defined(MATCH_GROUPS)

#define NAME crex_match_groups
#define RESULT_DECLARATION crex_slice_t *matches
#define N_GROUPS regex->n_groups

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

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)

  size_t *pointer_offsets;

  if (context->pointer_offsets_size < regex->size) {
    pointer_offsets = malloc(sizeof(size_t) * regex->size);

    if (pointer_offsets == NULL) {
      return CREX_E_NOMEM;
    }

    free(context->pointer_offsets);
    context->pointer_offsets = pointer_offsets;

    context->pointer_offsets_size = regex->size;
  } else {
    pointer_offsets = context->pointer_offsets;
  }

  for (size_t i = 0; i < regex->size; i++) {
    pointer_offsets[i] = ~(size_t)0;
  }

  size_t highest_used_offset = 0;

#endif

  // FIXME: justify this bound
  const size_t max_list_size = 2 * regex->size;

  size_t head, freelist;
  ip_list_initialize(context, &head, &freelist, max_list_size);

  const char *eof = buffer + buffer_size;
  int prev_character = -1;

  for (;;) {
    const int character = (buffer == eof) ? -1 : (unsigned char)(*(buffer++));

    memset(visited, 0, min_visited_size);

    if (!ip_list_push(context, &head, &freelist, max_list_size, 0)) {
      return CREX_E_NOMEM;
    }

    size_t iter = head;
    size_t *pred_pointer = &head;

    for (;;) {
      size_t instruction_pointer = context->list_buffer[iter].instruction_pointer;
      size_t *next = &context->list_buffer[iter].next;

      int keep = 1;

      for (;;) {
        if (instruction_pointer == regex->size) {
#ifdef MATCH_BOOLEAN
          (*is_match) = 1;
#elif defined(MATCH_LOCATION)
          // FIXME
#elif defined(MATCH_GROUPS)
          // FIXME
#endif
          return CREX_OK;
        }

        const size_t byte_index = instruction_pointer / CHAR_BIT;
        const size_t bit_index = instruction_pointer % CHAR_BIT;

        if (visited[byte_index] & (1u << bit_index)) {
          keep = 0;
          break;
        }

        visited[byte_index] |= 1u << bit_index;

        const unsigned char code = regex->bytecode[instruction_pointer++];

        if (code == VM_CHARACTER) {
          assert(instruction_pointer <= regex->size - 1);

          const unsigned char expected_character = regex->bytecode[instruction_pointer++];

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
          assert(instruction_pointer <= regex->size - 4);

          const long delta = deserialize_long(regex->bytecode + instruction_pointer, 4);
          instruction_pointer += 4;

          instruction_pointer += delta;

          break;
        }

        case VM_SPLIT_PASSIVE:
        case VM_SPLIT_EAGER: {
          assert(instruction_pointer <= regex->size - 4);

          const long delta = deserialize_long(regex->bytecode + instruction_pointer, 4);
          instruction_pointer += 4;

          size_t split_pointer;
          // FIXME: copy state

          if (code == VM_SPLIT_PASSIVE) {
            split_pointer = instruction_pointer + delta;
          } else {
            split_pointer = instruction_pointer;
            instruction_pointer += delta;
          }

          if (!((visited[split_pointer / CHAR_BIT] >> (split_pointer % CHAR_BIT)) & 1u)) {
            if (!ip_list_push(context, next, &freelist, max_list_size, split_pointer)) {
              return CREX_E_NOMEM;
            }
          }

          break;
        }

        case VM_WRITE_POINTER: {
          instruction_pointer += 4; // FIXME
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
        context->list_buffer[iter].instruction_pointer = instruction_pointer;
        pred_pointer = next;
        iter = *next;
      } else {
        ip_list_pop(context, pred_pointer, &freelist, iter);
        iter = *pred_pointer;
      }

      if (iter == IP_LIST_END) {
        break;
      }
    }

    if (character == -1) {
      break;
    }
  }

#ifdef MATCH_BOOLEAN
  (*is_match) = 0;
#else
  for (size_t i = 0; i < N_GROUPS; i++) {
    matches[i].size = 0;
    matches[i].start = NULL;
  }
#endif

  return CREX_OK;
}

#undef MATCH_BOOLEAN
#undef MATCH_LOCATION
#undef MATCH_GROUPS
