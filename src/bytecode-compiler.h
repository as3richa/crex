#ifndef BYTECODE_COMPILER_H
#define BYTECODE_COMPILER_H

#include "parser.h"

typedef struct {
  size_t size;
  unsigned char *code;
} bytecode_t;

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

#define VM_OPCODE(byte) ((byte)&31u)
#define VM_OPERAND_SIZE(byte) ((byte) >> 5u)

WARN_UNUSED_RESULT static int compile_to_bytecode(bytecode_t *bytecode,
                                                  size_t *n_flags,
                                                  parsetree_t *tree,
                                                  const allocator_t *allocator);

#endif
