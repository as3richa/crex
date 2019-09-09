#include <time.h>

#include "src/crex.c"
#include "src/vm.h"

WARN_UNUSED_RESULT static size_t my_rand(void) {
  // Assume rand is absolutely awful. Performance is not a major concern here

  size_t result = 0;

  for (size_t i = 0; i < sizeof(size_t); i++) {
    result = (result << 8u) ^ (size_t)rand();
  }

  return result;
}

WARN_UNUSED_RESULT static size_t select_from_distribution(size_t *distribution, size_t size) {
  size_t total_weight = 0;

  for (size_t i = 0; i < size; i++) {
    total_weight += distribution[i];
  }

  size_t goal = my_rand() % total_weight;

  for (size_t i = 0; i < size; i++) {
    if (goal < distribution[i]) {
      return i;
    }

    goal -= distribution[i];
  }

  UNREACHABLE();
  return 0;
}

typedef struct {
  size_t lifespan;

  unsigned int is_matching : 1;
  unsigned int eof_acceptable : 1;

  char_class_t char_class;
} thread_state_t;

static void reset_thread_state(thread_state_t *state) {
  state->is_matching = 0;
  state->eof_acceptable = 1;
  memset(state->char_class, 0xff, 32);
}

WARN_UNUSED_RESULT static thread_status_t interesting_step_thread(vm_t *vm,
                                                                  vm_handle_t thread,
                                                                  size_t *instr_pointer,
                                                                  const char *str,
                                                                  int character,
                                                                  int prev_character) {
  thread_state_t *state = EXTRA_DATA(*vm, thread);

  // The VM initially memsets the per-thread data to all zeroes on spawn; this guarantees that
  // lifespan is zero immediately after spawn (and never again)
  if (state->lifespan == 0) {
    reset_thread_state(state);
  }

  state->lifespan++;

  fprintf(stderr, "!! %zu %zu\n", *instr_pointer, state->lifespan);

  const unsigned char byte = vm->code[*instr_pointer];

  const unsigned char opcode = VM_OPCODE(byte);
  const size_t operand_size = VM_OPERAND_SIZE(byte);

  const size_t operand = deserialize_operand(vm->code + *instr_pointer + 1, operand_size);

  switch (opcode) {
  case VM_CHARACTER:
  case VM_CHAR_CLASS:
  case VM_BUILTIN_CHAR_CLASS:
  case VM_ANCHOR_EOF:
  case VM_ANCHOR_EOL:
  case VM_ANCHOR_WORD_BOUNDARY:
  case VM_ANCHOR_NOT_WORD_BOUNDARY: {
    *instr_pointer += operand_size + 1;

    switch (opcode) {
    case VM_CHARACTER: {
      state->eof_acceptable = 0;

      const int char_acceptable = bitmap_test(state->char_class, operand);

      bitmap_clear(state->char_class, 32);

      if (char_acceptable) {
        bitmap_set(state->char_class, operand);
      }

      return TS_DONE;
    }

    case VM_CHAR_CLASS:
    case VM_BUILTIN_CHAR_CLASS: {
      state->eof_acceptable = 0;

      unsigned char *char_class =
          (opcode == VM_CHAR_CLASS) ? vm->classes[operand] : builtin_classes[operand];

      for (size_t i = 0; i < 32; i++) {
        state->char_class[i] &= char_class[i];
      }

      return TS_DONE;
    }

    case VM_ANCHOR_EOF: {
      bitmap_clear(state->char_class, 32);
      return TS_CONTINUE;
    }

    case VM_ANCHOR_EOL: {
      const int newline_acceptable = bitmap_test(state->char_class, '\n');

      bitmap_clear(state->char_class, 32);

      if (newline_acceptable) {
        bitmap_set(state->char_class, '\n');
      }

      return TS_CONTINUE;
    }

    case VM_ANCHOR_WORD_BOUNDARY:
    case VM_ANCHOR_NOT_WORD_BOUNDARY: {
      const int prev_char_is_word =
          prev_character != -1 && bitmap_test(builtin_classes[BCC_WORD], prev_character);

      const int word_expected = prev_char_is_word ^ (opcode == VM_ANCHOR_WORD_BOUNDARY);

      unsigned char *char_class = builtin_classes[word_expected ? BCC_WORD : BCC_NOT_WORD];

      for (size_t i = 0; i < 32; i++) {
        state->char_class[i] &= char_class[i];
      }

      if (word_expected) {
        state->eof_acceptable = 0;
      }

      return TS_CONTINUE;
    }

    default:
      UNREACHABLE();
    }
  }

  default: {
    const thread_status_t status =
        step_thread(vm, thread, instr_pointer, str, character, prev_character);

    // VM_WRITE_POINTER 1 is always and solely the final instruction of a compiled regex, and
    // moreover the structure of the program guarantees that this instruction can't be jumped over.
    // We use this invariant to intercept a match before it happens (to avoid destroying
    // potentially-valid threads). If ever the invariant no longer holds, an alternative would be to
    // implement matching as an opcode

    if (opcode == VM_WRITE_POINTER && operand == 1) {
      return TS_DONE;
    }

    return status;
  }
  }
}

WARN_UNUSED_RESULT static status_t
generate_interesting_string(void *result, context_t *context, const regex_t *regex) {
  (void)result;

  vm_t vm;

  if (!create_vm(&vm, context, regex, 2 * regex->n_capturing_groups, sizeof(thread_state_t))) {
    return CREX_E_NOMEM;
  }

  const char *str_base = (const char *)NULL + 1;
  const char *str = str_base;

  int prev_character = -1;
  size_t character;

  for (;;) {
    vm_status_t status = run_threads(&vm, interesting_step_thread, str, -1, prev_character);

    if (status == VM_STATUS_DONE) {
      break;
    }

    if (status == VM_STATUS_E_NOMEM) {
      return CREX_E_NOMEM;
    }

    assert(status == VM_STATUS_CONTINUE);

    size_t distribution[257];

    // Every character and EOF has a non-zero probability, but the base probability of printable
    // characters is higher
    for (size_t i = 0; i <= 256; i++) {
      distribution[i] = (i != 256 && isprint(i)) ? 10 : 0;
    }

    for (vm_handle_t thread = vm.head; thread != NULL_HANDLE; thread = NEXT(vm, thread)) {
      thread_state_t *state = EXTRA_DATA(vm, thread);

      // Skip over any plausibly-matching threads, because we'd prefer a later, more interesting
      // match
      if (state->is_matching) {
        continue;
      }

      // Increase the probability of any character or EOF acceptable to this thread, proportional to
      // the thread's lifespan

      for (size_t i = 0; i < 256; i++) {
        if (!bitmap_test(state->char_class, i)) {
          continue;
        }

        distribution[i] += 100 * state->lifespan;
      }

      if (state->eof_acceptable) {
        distribution[256] += 20 * state->lifespan;
      }
    }

    for (size_t i = 0; i <= 256; i++) {
      if (distribution[i] <= 1)
        continue;
      fprintf(stderr, "%zu: %zu\n", i, distribution[i]);
    }
    fprintf(stderr, "\n\n");

    character = select_from_distribution(distribution, 257);

    fprintf(stderr, "%zu\n", character);

    vm_handle_t prev_thread = NULL_HANDLE;

    for (vm_handle_t thread = vm.head; thread != NULL_HANDLE;) {
      thread_state_t *state = EXTRA_DATA(vm, thread);

      fprintf(stderr, "=> %zu ", INSTR_POINTER(vm, thread));
      print_char_class(state->char_class, stderr);
      fprintf(stderr, " %c ", (int)character);

      const int okay =
          (character == 256) ? state->eof_acceptable : bitmap_test(state->char_class, character);

      fprintf(stderr, "%d\n", okay);

      reset_thread_state(state);

      // Discard threads that would not accept the chosen character
      if (!okay) {
        thread = destroy_thread(&vm, thread, prev_thread);
        continue;
      }

      // No special handling is needed for non-matching states
      if (!state->is_matching) {
        prev_thread = thread;
        thread = NEXT(vm, thread);
        continue;
      }

      // Mark the first truly-matching thread we find as such. This destroys any successors
      const vm_status_t status = on_match(&vm, thread, prev_thread);
      assert(status == VM_STATUS_CONTINUE);
      (void)status;

      break;
    }

    if (character == 256) {
      break;
    }

    // FIXME
    putchar(character);

    prev_character = (character == 256) ? -1 : (int)character;
    str++;
  }

  // If we haven't yet explicitly recorded EOF, we can output a suffix of random characters without
  // affecting the result

  if (character != 256) {
    const size_t suffix_size = my_rand() % 32;

    for (size_t i = 0; i < suffix_size; i++) {
      const int unprintable = my_rand() % 10 == 11;

      if (unprintable) {
        character = my_rand() % 256;
      } else {
        character = ' ' + my_rand() % ('~' - ' ' + 1);
      }

      putchar(character);
    }
  }

  return CREX_OK;
}

int main(void) {
  srand(time(NULL));

  const char *pattern = "\\([0-9]{3}\\)-[0-9]{3}-[0-9]{4}";

  crex_status_t status;

  crex_regex_t *regex = crex_compile_str(&status, pattern);

  if (regex == NULL) {
    return 1;
  }

  crex_context_t *context = crex_create_context(&status);

  if (context == NULL) {
    crex_destroy_regex(regex);
    return 1;
  }

  if (generate_interesting_string(NULL, context, regex) != CREX_OK) {
    crex_destroy_regex(regex);
    crex_destroy_context(context);
    return 1;
  }

  crex_destroy_regex(regex);
  crex_destroy_context(context);

  return 0;
}
