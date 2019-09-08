#ifndef VM_H
#define VM_H

enum {
  VM_CHARACTER,
  VM_CHAR_CLASS,
  VM_BUILTIN_CHAR_CLASS,
  VM_ANCHOR_BOF,
  VM_ANCHOR_BOL,
  VM_ANCHOR_EOF,
  VM_ANCHOR_EOL,
  VM_ANCHOR_WORD_BOUNDARY,
  VM_ANCHOR_NOT_WORD_BOUNDARY,
  VM_JUMP,
  VM_SPLIT_PASSIVE,
  VM_SPLIT_EAGER,
  VM_SPLIT_BACKWARDS_PASSIVE,
  VM_SPLIT_BACKWARDS_EAGER,
  VM_WRITE_POINTER,
  VM_TEST_AND_SET_FLAG
};

// Bytecode instruction encoding
#define VM_OP(opcode, operand_size) ((opcode) | ((operand_size) << 5u))

// ... decoding
#define VM_OPCODE(byte) ((byte)&31u)
#define VM_OPERAND_SIZE(byte) ((byte) >> 5u)

typedef size_t vm_handle_t;

#define NULL_HANDLE SIZE_MAX

typedef struct {
  context_t *context;
  size_t n_pointers;

  size_t capacity;
  unsigned char *buffer;

  size_t bump_pointer;
  vm_handle_t freelist;

  size_t head;

  vm_handle_t matched_thread;

  size_t size;
  const unsigned char *code;

  char_class_t *classes;

  size_t flags_size;

#ifndef NDEBUG
  size_t n_capturing_groups;
  size_t n_classes;
  size_t n_flags;
#endif
} vm_t;

typedef enum { TS_CONTINUE, TS_REJECTED, TS_MATCHED, TS_DONE, TS_E_NOMEM } thread_status_t;

typedef enum { VM_STATUS_CONTINUE, VM_STATUS_DONE, VM_STATUS_E_NOMEM } vm_status_t;

typedef thread_status_t (*step_function_t)(vm_t *, size_t, size_t *, const char *, int, int);

WARN_UNUSED_RESULT static int
create_vm(vm_t *vm, context_t *context, const regex_t *regex, size_t n_pointers);

WARN_UNUSED_RESULT static vm_status_t
run_threads(vm_t *vm, step_function_t step, const char *str, int character, int prev_character);

WARN_UNUSED_RESULT static thread_status_t step_thread(vm_t *vm,
                                                      vm_handle_t thread,
                                                      size_t *instr_pointer,
                                                      const char *str,
                                                      int character,
                                                      int prev_character);

#define BLOCK_SIZE                                                                                 \
  sizeof(union {                                                                                   \
    size_t size;                                                                                   \
    void *pointer;                                                                                 \
  })

#define NEXT(vm, thread) (*(size_t *)((vm).buffer + (thread)))
#define INSTR_POINTER(vm, thread) (*(size_t *)((vm).buffer + (thread) + BLOCK_SIZE))
#define POINTER_BUFFER(vm, thread) ((const char **)((vm).buffer + (thread) + 2 * BLOCK_SIZE))

#endif
