#include <time.h>

#include "src/crex.c"
#include "src/vm.h"

typedef struct {
  size_t capacity;
  size_t size;
  char *str;
} string_builder_t;

void create_string_builder(string_builder_t *sb) {
  sb->capacity = 0;
  sb->size = 0;
  sb->str = NULL;
}

int string_builder_push(string_builder_t *sb, char c) {
  assert(sb->size <= sb->capacity);

  if (sb->size == sb->capacity) {
    const size_t capacity = 2 * sb->capacity + 1;
    char *str = realloc(sb->str, capacity);

    if (str == NULL) {
      return 0;
    }

    sb->capacity = capacity;
    sb->str = str;
  }

  sb->str[sb->size++] = c;

  return 1;
}

void destroy_string_builder(string_builder_t *sb) {
  free(sb->str);
}

WARN_UNUSED_RESULT static size_t my_rand(void) {
  // Assume rand is absolutely awful. Performance is not a major concern here

  size_t result = 0;

  for (size_t i = 0; i < sizeof(size_t); i++) {
    result = (result << 8u) ^ (size_t)rand();
  }

  return result;
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
  // character contains garbage
  (void)character;

  thread_state_t *state = EXTRA_DATA(*vm, thread);

  // The VM initially memsets the per-thread data to all-zeroes on spawn, in which case
  // state->lifespan == 0
  if (state->lifespan == 0) {
    reset_thread_state(state);
  }

  state->lifespan++;

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
    const thread_status_t status = step_thread(vm, thread, instr_pointer, str, -1, prev_character);

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
generate_interesting_string(string_builder_t *result, context_t *context, const regex_t *regex) {
  vm_t vm;

  if (!create_vm(&vm, context, regex, 2 * regex->n_capturing_groups, sizeof(thread_state_t))) {
    return CREX_E_NOMEM;
  }

  const char *str_base = (const char *)NULL + 1;
  const char *str = str_base;

  int prev_character = -1;
  int character;

  for (;;) {
    vm_status_t status = run_threads(&vm, interesting_step_thread, str, -1, prev_character);

    if (status == VM_STATUS_DONE) {
      break;
    }

    if (status == VM_STATUS_E_NOMEM) {
      return CREX_E_NOMEM;
    }

    assert(status == VM_STATUS_CONTINUE);

    // Select a random thread, weighted by thread lifespan
    vm_handle_t special_thread = NULL_HANDLE;
    size_t total_lifespan = 0;

    // In parallel, track the set of characters that would cause every thread to reject
    char_class_t rejects_all;
    memset(rejects_all, 0xff, 32);
    int eof_rejects_all = 1;

    for (vm_handle_t thread = vm.head; thread != NULL_HANDLE; thread = NEXT(vm, thread)) {
      thread_state_t *state = EXTRA_DATA(vm, thread);

      total_lifespan += state->lifespan;

      if (my_rand() % total_lifespan < state->lifespan) {
        special_thread = thread;
      }

      if (state->eof_acceptable) {
        eof_rejects_all = 0;
      }

      for (size_t i = 0; i < 32; i++) {
        rejects_all[i] &= ~state->char_class[i];
      }
    }

    assert((special_thread == NULL_HANDLE) == (total_lifespan == 0));

    // It can be shown by induction that special_thread is uniformly distributed over the set of
    // active threads, assuming that there exists at least one active thread. With high probability
    // attempt to select a character that will allow special_thread to progress; with
    // low probability, attempt to select a character that will cause every thread to reject. If no
    // active threads exist, this degrades to picking a character or EOF at random

    if (total_lifespan != 0 && my_rand() % total_lifespan == 0) {
      special_thread = NULL_HANDLE;
    }

    int eof_acceptable;
    unsigned char *char_class;

    if (special_thread == NULL_HANDLE) {
      eof_acceptable = eof_rejects_all;
      char_class = rejects_all;
    } else {
      thread_state_t *state = EXTRA_DATA(vm, special_thread);
      eof_acceptable = state->eof_acceptable;
      char_class = state->char_class;
    }

    // If no character satisfies our goal, pick EOF with low probability, or a random character with
    // high probability
    character = (my_rand() % 100 == 0) ? -1 : (int)(my_rand() % 256);

    size_t n_options = 0;

    if (eof_acceptable) {
      n_options++;
      character = -1;
    }

    for (size_t i = 0; i <= 255; i++) {
      if (!bitmap_test(char_class, i)) {
        continue;
      }

      if (my_rand() % (++n_options) == 0) {
        character = i;
      }
    }

    for (vm_handle_t thread = vm.head, prev_thread = NULL_HANDLE; thread != NULL_HANDLE;) {
      thread_state_t *state = EXTRA_DATA(vm, thread);

      const int okay =
          (character == -1) ? state->eof_acceptable : bitmap_test(state->char_class, character);

      // Discard threads that would not accept the chosen character
      if (!okay) {
        thread = destroy_thread(&vm, thread, prev_thread);
        continue;
      }

      reset_thread_state(state);

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

    if (character == -1) {
      break;
    }

    if (!string_builder_push(result, character)) {
      return CREX_E_NOMEM;
    }

    prev_character = character;
    str++;
  }

  // If we haven't reached EOF, maybe append another interesting string
  if (character != -1 && my_rand() % 4 < 3) {
    return generate_interesting_string(result, context, regex);
  }

  return CREX_OK;
}

static int print_string_literal(const char *str, size_t size, FILE *file) {
  if (fputc('"', file) == EOF) {
    return EOF;
  }

  for (size_t i = 0; i < size; i++) {
    const int c = str[i];

    switch (c) {
    case '"': {
      if (fputs("\\\"", file) == EOF) {
        return EOF;
      }
      break;
    }

    case '\n': {
      if (fputs("\\n", file) == EOF) {
        return EOF;
      }
      break;
    }

    case '\t': {
      if (fputs("\\t", file) == EOF) {
        return EOF;
      }
      break;
    }

    case '\\': {
      if (fputs("\\\\", file) == EOF) {
        return EOF;
      }
      break;
    }

    default: {
      if (' ' <= c && c <= '~') {
        if (fputc(c, file) == EOF) {
          return EOF;
        }

        break;
      }

      if (fprintf(file, "\\%03o", (unsigned char)c) < 0) {
        return 0;
      }
    }
    }
  }

  if (fputc('"', file) == EOF) {
    return EOF;
  }

  return 0;
}

int main(void) {
  srand(time(NULL));

  const char *pattern =
      "\\b(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\b";
  // const char *pattern = "0x([0-9a-fA-F]{4}){1,2}";
  // const char* pattern =
  // "{(?:\\s*([1-9][0-9]*)(?:\\.([0-9]+))?(?:[eE](-?[1-9][0-9]*))?,)*\\s*([1-9][0-9]*)(?:\\.([0-9]+))?(?:[eE](-?[1-9][0-9]*))?\\s*}";

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

  string_builder_t sb;
  create_string_builder(&sb);

  for (size_t i = 0; i < 24; i++) {
    sb.size = 0;

    if (generate_interesting_string(&sb, context, regex) != CREX_OK) {
      crex_destroy_regex(regex);
      crex_destroy_context(context);
      destroy_string_builder(&sb);
      return 1;
    }

    print_string_literal(sb.str, sb.size, stdout);
    putchar('\n');
  }

  crex_destroy_regex(regex);
  crex_destroy_context(context);
  destroy_string_builder(&sb);

  return 0;
}
