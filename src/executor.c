#include "vm.h"

WARN_UNUSED_RESULT static status_t execute_regex(void *result,
                                                 context_t *context,
                                                 const regex_t *regex,
                                                 const char *str,
                                                 size_t size,
                                                 size_t n_pointers) {
  vm_t vm;

  if (!create_vm(&vm, context, regex, n_pointers, 0)) {
    return CREX_E_NOMEM;
  }

  const char *eof = str + size;
  int prev_character = -1;

  for (;;) {
    const int character = (str == eof) ? -1 : (unsigned char)(*str);

    vm_status_t status = run_threads(&vm, step_thread, str, character, prev_character);

    if (status == VM_STATUS_DONE) {
      break;
    }

    if (status == VM_STATUS_E_NOMEM) {
      return CREX_E_NOMEM;
    }

    assert(status == VM_STATUS_CONTINUE);

    if (character == -1) {
      break;
    }

    prev_character = character;
    str++;
  }

  if (n_pointers == 0) {
    int *is_match = result;
    *is_match = vm.matched_thread != NULL_HANDLE;
    return CREX_OK;
  }

  match_t *matches = result;

  if (vm.matched_thread == NULL_HANDLE) {
    for (size_t i = 0; i < n_pointers / 2; i++) {
      matches[i].begin = NULL;
      matches[i].end = NULL;
    }

    return CREX_OK;
  }

  memcpy(matches, POINTER_BUFFER(vm, vm.matched_thread), sizeof(const char *) * n_pointers);

  return CREX_OK;
}
