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
  const size_t max_blocks = regex->size;

#ifdef MATCH_LOCATION
  const size_t block_size = 2;
#else
  const size_t block_size = 2 * regex->n_groups;
#endif

  size_t pointer_block_freelist;
  pointer_block_allocator_initialize(context, &pointer_block_freelist, max_blocks, block_size);

  size_t *pointer_block_offsets;

  if (context->pointer_block_offsets_size < regex->size) {
    pointer_block_offsets = malloc(sizeof(size_t) * regex->size);

    if (pointer_block_offsets == NULL) {
      return CREX_E_NOMEM;
    }

    free(context->pointer_block_offsets);
    context->pointer_block_offsets = pointer_block_offsets;

    context->pointer_block_offsets_size = regex->size;
  } else {
    pointer_block_offsets = context->pointer_block_offsets;
  }

  for (size_t i = 0; i < regex->size; i++) {
    pointer_block_offsets[i] = IP_LIST_END;
  }

  int match_found = 0;

#endif

#ifdef MATCH_BOOLEAN
  (*is_match) = 0;
#elif defined(MATCH_LOCATION)
  match->size = 0;
  match->start = NULL;
#elif defined(MATCH_GROUPS)
  for (size_t i = 0; i < N_GROUPS; i++) {
    matches[i].size = 0;
    matches[i].start = NULL;
  }
#endif

  // FIXME: justify this bound
  const size_t max_list_size = 2 * regex->size;

  size_t head, freelist;
  ip_list_initialize(context, &head, &freelist, max_list_size);

  const char *eof = buffer + buffer_size;
  int prev_character = -1;

  for (;;) {
    const int character = (buffer == eof) ? -1 : (unsigned char)(*buffer);

    memset(visited, 0, min_visited_size);

    size_t iter = head;
    size_t *pred_pointer = &head;

    int initial_state_pushed = 0;

    for (;;) {
      if (iter == IP_LIST_END) {
        if (initial_state_pushed) {
          break;
        }

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
        if (match_found) {
          break;
        }
#endif

        initial_state_pushed = 1;

        if (!ip_list_push(context, pred_pointer, &freelist, max_list_size, 0)) {
          return CREX_E_NOMEM;
        }

        iter = *pred_pointer;

        assert(context->list_buffer[iter].instruction_pointer == 0);

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
        assert(context->pointer_block_offsets[0] == IP_LIST_END);
        context->pointer_block_offsets[0] =
            pointer_block_allocator_alloc(context, &pointer_block_freelist, max_blocks, block_size);

        if (context->pointer_block_offsets[0] == IP_LIST_END) {
          return CREX_E_NOMEM;
        }

        for (size_t i = 0; i < block_size; i++) {
          context->pointer_block_buffer[context->pointer_block_offsets[0] + i].pointer = NULL;
        }
#endif
      }

      size_t instruction_pointer = context->list_buffer[iter].instruction_pointer;
      size_t *next = &context->list_buffer[iter].next;

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
      const size_t block = pointer_block_offsets[instruction_pointer];
      assert(block != IP_LIST_END);
      pointer_block_offsets[instruction_pointer] = IP_LIST_END;
#endif

      int keep = 1;

      for (;;) {
        if (instruction_pointer == regex->size) {
#ifdef MATCH_BOOLEAN
          (*is_match) = 1;
          return CREX_OK;
#else

#ifdef MATCH_LOCATION
          crex_slice_t *matches = match;
#endif
          const pointer_block_t *pointer_block_buffer = context->pointer_block_buffer;

          for (size_t i = 0; i < N_GROUPS; i++) {
            if (pointer_block_buffer[block + 2 * i].pointer == NULL ||
                pointer_block_buffer[block + 2 * i + 1].pointer == NULL) {
              matches[i].size = 0;
              matches[i].start = NULL;
            } else {
              matches[i].size = pointer_block_buffer[block + 2 * i + 1].pointer -
                                pointer_block_buffer[block + 2 * i].pointer;
              matches[i].start = pointer_block_buffer[block + 2 * i].pointer;
            }
          }

          match_found = 1;
          break;
#endif
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

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
            if (context->pointer_block_offsets[split_pointer] == IP_LIST_END) {
              context->pointer_block_offsets[split_pointer] = pointer_block_allocator_alloc(
                  context, &pointer_block_freelist, max_blocks, block_size);
            }

            assert(context->pointer_block_offsets[split_pointer] != IP_LIST_END);

            const size_t split_block = context->pointer_block_offsets[split_pointer];

            for (size_t i = 0; i < block_size; i++) {
              context->pointer_block_buffer[split_block + i] =
                  context->pointer_block_buffer[block + i];
            }
#endif
          }

          break;
        }

        case VM_WRITE_POINTER: {
#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
          // FIXME: signedness
          size_t index = (size_t)deserialize_long(regex->bytecode + instruction_pointer, 4);

          // FIXME: discarding constness
#if defined(MATCH_LOCATION)
          if (index == 0 || index == 1) {
            context->pointer_block_buffer[block + index].pointer = buffer;
          }
#else
          context->pointer_block_buffer[block + index].pointer = buffer;
#endif

#endif

          instruction_pointer += 4;
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
#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
        context->pointer_block_offsets[instruction_pointer] = block;
#endif
        pred_pointer = next;
        iter = *next;
      } else {
        ip_list_pop(context, pred_pointer, &freelist, iter);
#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
        pointer_block_allocator_free(context, &pointer_block_freelist, block_size, block);
#endif
        iter = *pred_pointer;
      }
    }

#if defined(MATCH_LOCATION) || defined(MATCH_GROUPS)
    if (iter != IP_LIST_END) {
      assert(match_found);

      pred_pointer = &context->list_buffer[iter].next;
      iter = *pred_pointer;

      while (iter != IP_LIST_END) {
        size_t block =
            context->pointer_block_offsets[context->list_buffer[iter].instruction_pointer];
        ip_list_pop(context, pred_pointer, &freelist, iter);
        pointer_block_allocator_free(context, &pointer_block_freelist, block_size, block);
      }

      pred_pointer = &context->list_buffer[iter].next;
      iter = *pred_pointer;
    }
#endif

    if (character == -1) {
      break;
    }

    buffer++;
  }

  return CREX_OK;
}

#undef MATCH_BOOLEAN
#undef MATCH_LOCATION
#undef MATCH_GROUPS
#undef NAME
#undef RESULT_DECLARATION
#undef N_GROUPS
