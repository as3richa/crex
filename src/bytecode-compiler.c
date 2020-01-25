#include "bytecode-compiler.h"
#include "vm.h"

#define NAME bytecode
#define CONTAINED_TYPE unsigned char
#define STACK_CAPACITY 64
#include "vector.c"

WUR static int compile_parsetree(bytecode_t *bytecode,
                                 size_t *n_flags,
                                 parsetree_t *tree,
                                 const allocator_t *allocator);

WUR static unsigned char *
emit_bytecode(unsigned char *code, unsigned char opcode, size_t operand, size_t operand_size);

WUR static unsigned char *emit_bytecode_copy(unsigned char *code, bytecode_t *source);

WUR static unsigned char *compile_to_bytecode(size_t *size,
                                              size_t *n_flags,
                                              parsetree_t *tree,
                                              const allocator_t *allocator) {
  *n_flags = 0;

  bytecode_t bytecode;
  create_bytecode(&bytecode);

  if (!compile_parsetree(&bytecode, n_flags, tree, allocator)) {
    return NULL;
  }

  unsigned char *code = unpack_bytecode(&bytecode, size, allocator);

  if (code == NULL) {
    destroy_bytecode(&bytecode, allocator);
    return NULL;
  }

  return code;
}

WUR static int compile_parsetree(bytecode_t *bytecode,
                                 size_t *n_flags,
                                 parsetree_t *tree,
                                 const allocator_t *allocator) {
  for(; tree != NULL; tree = tree->next) {
    switch (tree->type) {
    case PT_EMPTY: {
      break;
    }

    case PT_CHARACTER: {
      unsigned char *code = bytecode_reserve(bytecode, 2, allocator);

      if (code == NULL) {
        return 0;
      }

      code = emit_bytecode(code, VM_CHARACTER, tree->data.character, 1);

      bytecode_extend(bytecode, code);

      break;
    }

    case PT_CHAR_CLASS:
    case PT_BUILTIN_CHAR_CLASS: {
      const size_t index = tree->data.char_class_index;
      const size_t index_size = size_for_operand(index);

      unsigned char *code = bytecode_reserve(bytecode, 1 + index_size, allocator);

      if (code == NULL) {
        return 0;
      }

      const unsigned char opcode =
          (tree->type == PT_CHAR_CLASS) ? VM_CHAR_CLASS : VM_BUILTIN_CHAR_CLASS;

      code = emit_bytecode(code, opcode, index, index_size);

      bytecode_extend(bytecode, code);

      break;
    }

    case PT_ANCHOR: {
      unsigned char *code = bytecode_reserve(bytecode, 1, allocator);

      if (code == NULL) {
        return 0;
      }

      const unsigned char opcode = VM_ANCHOR_BOF + tree->data.anchor_type;

      code = emit_bytecode(code, opcode, 0, 0);

      bytecode_extend(bytecode, code);

      break;
    }

    case PT_ALTERNATION: {
      bytecode_t left;
      create_bytecode(&left);

      if (!compile_parsetree(&left, n_flags, tree->data.alternation.left, allocator)) {
        destroy_bytecode(&left, allocator);
        return 0;
      }

      bytecode_t right;
      create_bytecode(&right);

      if (!compile_parsetree(&right, n_flags, tree->data.alternation.right, allocator)) {
        destroy_bytecode(&left, allocator);
        destroy_bytecode(&right, allocator);
        return 0;
      }

      //   VM_SPLIT_PASSIVE right
      //   <left.code>
      //   VM_JUMP end
      // right:
      //   <right.code>
      // end:
      //   VM_TEST_AND_SET_FLAG <flag>

      const size_t max_size = (1 + 4) + left.size + (1 + 4) + right.size + (1 + 4);

      unsigned char *code = bytecode_reserve(bytecode, max_size, allocator);

      if (code == NULL) {
        destroy_bytecode(&left, allocator);
        destroy_bytecode(&right, allocator);
        return 0;
      }

      const size_t jump_delta = right.size;
      const size_t jump_delta_size = size_for_operand(jump_delta);

      const size_t split_delta = left.size + 1 + jump_delta_size;
      const size_t split_delta_size = size_for_operand(split_delta);

      const size_t flag = (*n_flags)++;
      const size_t flag_size = size_for_operand(flag);

      code = emit_bytecode(code, VM_SPLIT_PASSIVE, split_delta, split_delta_size);
      code = emit_bytecode_copy(code, &left);
      code = emit_bytecode(code, VM_JUMP, jump_delta, jump_delta_size);
      code = emit_bytecode_copy(code, &right);
      code = emit_bytecode(code, VM_TEST_AND_SET_FLAG, flag, flag_size);

      destroy_bytecode(&left, allocator);
      destroy_bytecode(&right, allocator);

      bytecode_extend(bytecode, code);

      break;
    }

    case PT_GREEDY_REPETITION:
    case PT_LAZY_REPETITION: {
      bytecode_t child;
      create_bytecode(&child);

      if (!compile_parsetree(&child, n_flags, tree->data.repetition.child, allocator)) {
        destroy_bytecode(&child, allocator);
        return 0;
      }

      const size_t lower_bound = tree->data.repetition.lower_bound;
      const size_t upper_bound = tree->data.repetition.upper_bound;

      assert(lower_bound <= upper_bound && lower_bound != REPETITION_INFINITY);

      size_t max_size = lower_bound * child.size;

      if (upper_bound == REPETITION_INFINITY) {
        max_size += (1 + 4) + (1 + 4) + child.size + (1 + 4) + (1 + 4);
      } else {
        max_size += (upper_bound - lower_bound) * ((1 + 4) + child.size) + (1 + 4);
      }

      unsigned char *code = bytecode_reserve(bytecode, max_size, allocator);

      // FIXME: this is vile
      unsigned char *end = code + max_size;

      if (code == NULL) {
        destroy_bytecode(&child, allocator);
        return 0;
      }

      // <child.code>
      // <child.code>
      // ...
      // <child.code> [lower_bound times]
      // ...

      for (size_t i = 0; i < lower_bound; i++) {
        code = emit_bytecode_copy(code, &child);
      }

      if (upper_bound == REPETITION_INFINITY) {
        //   ...
        //   VM_SPLIT_{PASSIVE,EAGER} end
        // child:
        //   VM_TEST_AND_SET_FLAG child_flag
        //   <child.code>
        //   VM_SPLIT_{EAGER,PASSIVE} child
        // end:
        //   VM_TEST_AND_SET_FLAG end_flag

        unsigned char leading_split_opcode;
        unsigned char trailing_split_opcode;

        if (tree->type == PT_GREEDY_REPETITION) {
          leading_split_opcode = VM_SPLIT_PASSIVE;
          trailing_split_opcode = VM_SPLIT_BACKWARDS_EAGER;
        } else {
          leading_split_opcode = VM_SPLIT_EAGER;
          trailing_split_opcode = VM_SPLIT_BACKWARDS_PASSIVE;
        }

        const size_t child_flag = (*n_flags)++;
        const size_t child_flag_size = size_for_operand(child_flag);

        const size_t end_flag = (*n_flags)++;
        const size_t end_flag_size = size_for_operand(end_flag);

        size_t trailing_split_delta;
        size_t trailing_split_delta_size;

        // Because the source address for a jump or split is the address immediately after the
        // operand, in the case of a backwards split, the magnitude of the delta changes with the
        // operand size. In particular, a smaller operand implies a smaller magnitude. In order to
        // select the smallest possible operand in all cases, we have to compute the required delta
        // for a given operand size, then check if the delta does in fact fit. I would factor this out
        // into its own function, but this is the only instance of a backwards jump in the compiler

        {
          size_t delta;
          size_t delta_size;

          for (delta_size = 1; delta_size <= 4; delta_size *= 2) {
            delta = (1 + child_flag_size) + child.size + (1 + delta_size);

            if (size_for_operand(delta) <= delta_size) {
              break;
            }
          }

          trailing_split_delta = delta;
          trailing_split_delta_size = delta_size;
        }

        const size_t leading_split_delta =
            (1 + child_flag_size) + child.size + (1 + trailing_split_delta_size);

        const size_t leading_split_delta_size = size_for_operand(leading_split_delta);

        code =
            emit_bytecode(code, leading_split_opcode, leading_split_delta, leading_split_delta_size);
        code = emit_bytecode(code, VM_TEST_AND_SET_FLAG, child_flag, child_flag_size);
        code = emit_bytecode_copy(code, &child);
        code = emit_bytecode(
            code, trailing_split_opcode, trailing_split_delta, trailing_split_delta_size);
        code = emit_bytecode(code, VM_TEST_AND_SET_FLAG, end_flag, end_flag_size);
      } else {
        //   ...
        //   VM_SPLIT_{PASSIVE,EAGER} end
        //   <child.code>
        //   VM_SPLIT_{PASSIVE,EAGER} end
        //   <child.code>
        //   ... [(uppper_bound - lower_bound) times]
        // end:
        //   VM_TEST_AND_SET_FLAG flag

        const unsigned char split_opcode =
            (tree->type == PT_GREEDY_REPETITION) ? VM_SPLIT_PASSIVE : VM_SPLIT_EAGER;

        // In general, we don't know the minimum size of the operands of the earlier splits until
        // we've determined the minimum size of the operands of the later splits. If we
        // compile the later splits first (i.e. by filling the buffer from right to left),
        // we can pick the correct size a priori, and then perform a single copy to account for any
        // saved space

        // In general, this optimization leaves a linear amount of memory allocated but unused at
        // the end of the buffer; however, because the top level parsetree is necessarily a PT_GROUP,
        // we always free it before yielding the final compiled regex

        unsigned char *begin = end;

        const size_t flag = (*n_flags)++;
        const size_t flag_size = size_for_operand(flag);

        // Silence unused result warning
        unsigned char *unused;

        begin -= flag_size + 1;
        unused = emit_bytecode(begin, VM_TEST_AND_SET_FLAG, flag, flag_size);

        for (size_t i = 0; i < upper_bound - lower_bound; i++) {
          begin -= child.size;
          unused = emit_bytecode_copy(begin, &child);

          const size_t delta = (end - (1 + flag_size)) - begin;
          const size_t delta_size = size_for_operand(delta);

          begin -= 1 + delta_size;
          unused = emit_bytecode(begin, split_opcode, delta, delta_size);
        }

        (void)unused;

        assert(code <= begin);

        if (code < begin) {
          memmove(code, begin, end - begin);
        }

        code += end - begin;
      }

      destroy_bytecode(&child, allocator);

      bytecode_extend(bytecode, code);

      break;
    }

    case PT_GROUP: {
      if (tree->data.group.index == NON_CAPTURING_GROUP) {
        return compile_parsetree(bytecode, n_flags, tree->data.group.child, allocator);
      }

      bytecode_t child;
      create_bytecode(&child);

      if (!compile_parsetree(&child, n_flags, tree->data.group.child, allocator)) {
        destroy_bytecode(&child, allocator);
        return 0;
      }

      const size_t leading_index = 2 * tree->data.group.index;
      const size_t trailing_index = leading_index + 1;

      const size_t leading_size = size_for_operand(leading_index);
      const size_t trailing_size = size_for_operand(trailing_index);

      const size_t max_size = (1 + leading_size) + child.size + (1 + trailing_size);

      unsigned char *code = bytecode_reserve(bytecode, max_size, allocator);

      if (code == NULL) {
        destroy_bytecode(&child, allocator);
        return 0;
      }

      code = emit_bytecode(code, VM_WRITE_POINTER, leading_index, leading_size);
      code = emit_bytecode_copy(code, &child);
      code = emit_bytecode(code, VM_WRITE_POINTER, trailing_index, trailing_size);

      destroy_bytecode(&child, allocator);

      bytecode_extend(bytecode, code);

      break;
    }

    default:
      UNREACHABLE();
    }
  }

  return 1;
}

WUR static unsigned char *
emit_bytecode(unsigned char *code, unsigned char opcode, size_t operand, size_t operand_size) {
  *(code++) = opcode | (operand_size << 5u);

  serialize_operand(code, operand, operand_size);
  code += operand_size;

  return code;
}

WUR static unsigned char *emit_bytecode_copy(unsigned char *code, bytecode_t *source) {
  safe_memcpy(code, bytecode_buffer(source), source->size);
  return code + source->size;
}
