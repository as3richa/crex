#include "bytecode-compiler.h"

#define VM_OP(opcode, operand_size) ((opcode) | ((operand_size) << 5u))

WARN_UNUSED_RESULT static int compile_parsetree(bytecode_t *bytecode,
                                                size_t *n_flags,
                                                parsetree_t *tree,
                                                const allocator_t *allocator) {
  switch (tree->type) {
  case PT_EMPTY: {
    bytecode->size = 0;
    bytecode->code = NULL;
    return 1;
  }

  case PT_CHARACTER: {
    bytecode->size = 2;
    bytecode->code = ALLOC(allocator, 2);

    if (bytecode->code == NULL) {
      return 0;
    }

    bytecode->code[0] = VM_OP(VM_CHARACTER, 1);
    bytecode->code[1] = tree->data.character;

    return 1;
  }

  case PT_CHAR_CLASS:
  case PT_BUILTIN_CHAR_CLASS: {
    const size_t index = tree->data.char_class_index;
    const size_t operand_size = size_for_operand(index);

    bytecode->size = 1 + operand_size;
    bytecode->code = ALLOC(allocator, bytecode->size);

    if (bytecode->code == NULL) {
      return 0;
    }

    const unsigned char opcode =
        (tree->type == PT_CHAR_CLASS) ? VM_CHAR_CLASS : VM_BUILTIN_CHAR_CLASS;

    *bytecode->code = VM_OP(opcode, operand_size);
    serialize_operand(bytecode->code + 1, index, operand_size);

    return 1;
  }

  case PT_ANCHOR: {
    bytecode->size = 1;
    bytecode->code = ALLOC(allocator, 1);

    if (bytecode->code == NULL) {
      return 0;
    }

    *bytecode->code = VM_ANCHOR_BOF + tree->data.anchor_type;

    return 1;
  }

  case PT_CONCATENATION: {
    bytecode_t left;

    if (!compile_parsetree(&left, n_flags, tree->data.children[0], allocator)) {
      return 0;
    }

    bytecode_t right;

    if (!compile_parsetree(&right, n_flags, tree->data.children[1], allocator)) {
      FREE(allocator, left.code);
      return 0;
    }

    bytecode->size = left.size + right.size;
    bytecode->code = ALLOC(allocator, bytecode->size);

    if (bytecode->code == NULL) {
      FREE(allocator, left.code);
      FREE(allocator, right.code);
      return 0;
    }

    safe_memcpy(bytecode->code, left.code, left.size);
    safe_memcpy(bytecode->code + left.size, right.code, right.size);

    FREE(allocator, left.code);
    FREE(allocator, right.code);

    return 1;
  }

  case PT_ALTERNATION: {
    bytecode_t left;

    if (!compile_parsetree(&left, n_flags, tree->data.children[0], allocator)) {
      return 0;
    }

    bytecode_t right;

    if (!compile_parsetree(&right, n_flags, tree->data.children[1], allocator)) {
      FREE(allocator, left.code);
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

    unsigned char *code = ALLOC(allocator, max_size);

    if (code == NULL) {
      FREE(allocator, left.code);
      FREE(allocator, right.code);
      return 0;
    }

    bytecode->code = code;

    const size_t jump_delta = right.size;
    const size_t jump_delta_size = size_for_operand(jump_delta);

    const size_t split_delta = left.size + 1 + jump_delta_size;
    const size_t split_delta_size = size_for_operand(split_delta);

    const size_t flag = (*n_flags)++;
    const size_t flag_size = size_for_operand(flag);

    *(code++) = VM_OP(VM_SPLIT_PASSIVE, split_delta_size);
    serialize_operand(code, split_delta, split_delta_size);
    code += split_delta_size;

    safe_memcpy(code, left.code, left.size);
    code += left.size;

    *(code++) = VM_OP(VM_JUMP, jump_delta_size);
    serialize_operand(code, jump_delta, jump_delta_size);
    code += jump_delta_size;

    safe_memcpy(code, right.code, right.size);
    code += right.size;

    *(code++) = VM_OP(VM_TEST_AND_SET_FLAG, flag_size);
    serialize_operand(code, flag, flag_size);
    code += flag_size;

    bytecode->size = code - bytecode->code;

    FREE(allocator, left.code);
    FREE(allocator, right.code);

    return 1;
  }

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION: {
    bytecode_t child;

    if (!compile_parsetree(&child, n_flags, tree->data.repetition.child, allocator)) {
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

    unsigned char *code = ALLOC(allocator, max_size);

    if (code == NULL) {
      FREE(allocator, child.code);
      return 0;
    }

    bytecode->code = code;

    // <child.code>
    // <child.code>
    // ...
    // <child.code> [lower_bound times]
    // ...

    for (size_t i = 0; i < lower_bound; i++) {
      safe_memcpy(code, child.code, child.size);
      code += child.size;
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

      *(code++) = VM_OP(leading_split_opcode, leading_split_delta_size);
      serialize_operand(code, leading_split_delta, leading_split_delta_size);
      code += leading_split_delta_size;

      *(code++) = VM_OP(VM_TEST_AND_SET_FLAG, child_flag_size);
      serialize_operand(code, child_flag, child_flag_size);
      code += child_flag_size;

      safe_memcpy(code, child.code, child.size);
      code += child.size;

      *(code++) = VM_OP(trailing_split_opcode, trailing_split_delta_size);
      serialize_operand(code, trailing_split_delta, trailing_split_delta_size);
      code += trailing_split_delta_size;

      *(code++) = VM_OP(VM_TEST_AND_SET_FLAG, end_flag_size);
      serialize_operand(code, end_flag, end_flag_size);
      code += end_flag_size;

      bytecode->size = code - bytecode->code;
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

      unsigned char *end = bytecode->code + max_size;
      unsigned char *begin = end;

      const size_t flag = (*n_flags)++;
      const size_t flag_size = size_for_operand(flag);

      begin -= flag_size;
      serialize_operand(begin, flag, flag_size);
      *(--begin) = VM_OP(VM_TEST_AND_SET_FLAG, flag_size);

      for (size_t i = 0; i < upper_bound - lower_bound; i++) {
        begin -= child.size;
        safe_memcpy(begin, child.code, child.size);

        const size_t delta = (end - (1 + flag_size)) - begin;
        const size_t delta_size = size_for_operand(delta);

        begin -= delta_size;
        serialize_operand(begin, delta, delta_size);
        *(--begin) = VM_OP(split_opcode, delta_size);
      }

      assert(code <= begin);

      if (code < begin) {
        bytecode->size = max_size - (begin - code);
        memmove(code, begin, end - begin);
      } else {
        bytecode->size = max_size;
      }
    }

    FREE(allocator, child.code);

    return 1;
  }

  case PT_GROUP: {
    if (tree->data.group.index == NON_CAPTURING_GROUP) {
      return compile_parsetree(bytecode, n_flags, tree->data.group.child, allocator);
    }

    bytecode_t child;

    if (!compile_parsetree(&child, n_flags, tree->data.group.child, allocator)) {
      return 0;
    }

    const size_t leading_index = 2 * tree->data.group.index;
    const size_t trailing_index = leading_index + 1;

    const size_t leading_size = size_for_operand(leading_index);
    const size_t trailing_size = size_for_operand(trailing_index);

    bytecode->size = 1 + leading_size + child.size + 1 + trailing_size;

    unsigned char *code = ALLOC(allocator, bytecode->size);

    if (code == NULL) {
      FREE(allocator, child.code);
      return 0;
    }

    bytecode->code = code;

    *(code++) = VM_OP(VM_WRITE_POINTER, leading_size);

    serialize_operand(code, leading_index, leading_size);
    code += leading_size;

    safe_memcpy(code, child.code, child.size);
    code += child.size;

    *(code++) = VM_OP(VM_WRITE_POINTER, trailing_size);
    serialize_operand(code, trailing_index, trailing_size);
    code += trailing_size;

    assert(bytecode->size == (size_t)(code - bytecode->code));

    FREE(allocator, child.code);

    return 1;
  }

  default:
    UNREACHABLE();
    return 0;
  }
}

WARN_UNUSED_RESULT static int compile_to_bytecode(bytecode_t *bytecode,
                                                  size_t *n_flags,
                                                  parsetree_t *tree,
                                                  const allocator_t *allocator) {
  *n_flags = 0;
  return compile_parsetree(bytecode, n_flags, tree, allocator);
}
