#ifndef BYTECODE_COMPILER_H
#define BYTECODE_COMPILER_H

#include "parser.h"

WUR static unsigned char *
compile_to_bytecode(size_t *size, size_t *n_flags, parsetree_t *tree, const allocator_t *allocator);

#endif
