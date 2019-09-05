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

#define VM_OP(opcode, operand_size) ((opcode) | ((operand_size) << 5))

#define VM_OPCODE(byte) ((byte)&31u)

#define VM_OPERAND_SIZE(byte) ((byte) >> 5u)

typedef struct {
  size_t size;
  unsigned char *bytecode;
} bytecode_t;

static size_t size_for_operand(size_t operand) {
  assert(operand <= 0xffffffffLU);

  if (operand == 0) {
    return 0;
  }

  if (operand <= 0xffLU) {
    return 1;
  }

  if (operand <= 0xffffLU) {
    return 2;
  }

  return 4;
}

static void serialize_operand(void *destination, size_t operand, size_t size) {
  switch (size) {
  case 0: {
    assert(operand == 0);
    break;
  }

  case 1: {
    assert(operand <= 0xffLU);
    uint8_t u8_operand = operand;
    safe_memcpy(destination, &u8_operand, 1);
    break;
  }

  case 2: {
    assert(operand <= 0xffffLU);
    uint16_t u16_operand = operand;
    safe_memcpy(destination, &u16_operand, 2);
    break;
  }

  case 4: {
    assert(operand <= 0xffffffffLU);
    uint32_t u32_operand = operand;
    safe_memcpy(destination, &u32_operand, 4);
    break;
  }

  default:
    assert(0);
  }
}

static size_t deserialize_operand(void *source, size_t size) {
  switch (size) {
  case 0:
    return 0;

  case 1: {
    uint8_t u8_value;
    safe_memcpy(&u8_value, source, 1);
    return u8_value;
  }

  case 2: {
    uint16_t u16_value;
    safe_memcpy(&u16_value, source, 2);
    return u16_value;
  }

  case 4: {
    uint32_t u32_value;
    safe_memcpy(&u32_value, source, 4);
    return u32_value;
  }
  }

  assert(0);
  return 0;
}

static void serialize_operand_le(void *destination, size_t operand, size_t size) {
  unsigned char *bytes = destination;

  switch (size) {
  case 0: {
    assert(operand == 0);
    break;
  }

  case 1: {
    assert(operand <= 0xffLU);
    bytes[0] = operand;
    break;
  }

  case 2: {
    assert(operand <= 0xffffLU);
    bytes[0] = operand & 0xffu;
    bytes[1] = operand >> 8u;
    break;
  }

  case 4: {
    assert(operand <= 0xffffffffLU);
    bytes[0] = operand & 0xffu;
    bytes[1] = (operand >> 8u) & 0xffu;
    bytes[2] = (operand >> 16u) & 0xffu;
    bytes[3] = (operand >> 24u) & 0xffu;
    break;
  }

  case 8: {
    assert(operand <= UINT64_MAX);
    bytes[0] = operand & 0xffu;
    bytes[1] = (operand >> 8u) & 0xffu;
    bytes[2] = (operand >> 16u) & 0xffu;
    bytes[3] = (operand >> 24u) & 0xffu;
    bytes[4] = (operand >> 32u) & 0xffu;
    bytes[5] = (operand >> 40u) & 0xffu;
    bytes[6] = (operand >> 48u) & 0xffu;
    bytes[7] = (operand >> 56u) & 0xffu;
    break;
  }

  default:
    assert(0);
  }
}

static size_t deserialize_operand_le(void *source, size_t size) {
  unsigned char *bytes = source;

  switch (size) {
  case 0:
    return 0;

  case 1:
    return bytes[0];

  case 2: {
    size_t operand = bytes[0];
    operand |= ((size_t)bytes[1]) << 8u;
    return operand;
  }

  case 4: {
    size_t operand = bytes[0];
    operand |= ((size_t)bytes[1]) << 8u;
    operand |= ((size_t)bytes[2]) << 16u;
    operand |= ((size_t)bytes[3]) << 24u;
    return operand;
  }

  default:
    assert(0);
    return 0;
  }
}

WARN_UNUSED_RESULT static status_t compile_to_bytecode(bytecode_t *result,
                                                       size_t *n_flags,
                                                       parsetree_t *tree,
                                                       const allocator_t *allocator) {
  switch (tree->type) {
  case PT_EMPTY:
    result->size = 0;
    result->bytecode = NULL;
    break;

  case PT_CHARACTER:
    result->size = 2;
    result->bytecode = ALLOC(allocator, 2);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    result->bytecode[0] = VM_OP(VM_CHARACTER, 1);
    result->bytecode[1] = tree->data.character;

    break;

  case PT_CHAR_CLASS:
  case PT_BUILTIN_CHAR_CLASS: {
    const size_t index = tree->data.char_class_index;
    const size_t operand_size = size_for_operand(index);

    result->size = 1 + operand_size;
    result->bytecode = ALLOC(allocator, result->size);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    const unsigned char opcode =
        (tree->type == PT_CHAR_CLASS) ? VM_CHAR_CLASS : VM_BUILTIN_CHAR_CLASS;

    *result->bytecode = VM_OP(opcode, operand_size);
    serialize_operand(result->bytecode + 1, index, operand_size);

    break;
  }

  case PT_ANCHOR:
    result->size = 1;
    result->bytecode = ALLOC(allocator, 1);

    if (result->bytecode == NULL) {
      return CREX_E_NOMEM;
    }

    *result->bytecode = VM_ANCHOR_BOF + tree->data.anchor_type;

    break;

  case PT_CONCATENATION: {
    bytecode_t left, right;

    status_t status = compile_to_bytecode(&left, n_flags, tree->data.children[0], allocator);

    if (status != CREX_OK) {
      return status;
    }

    status = compile_to_bytecode(&right, n_flags, tree->data.children[1], allocator);

    if (status != CREX_OK) {
      FREE(allocator, left.bytecode);
      return status;
    }

    result->size = left.size + right.size;
    result->bytecode = ALLOC(allocator, result->size);

    if (result->bytecode == NULL) {
      FREE(allocator, left.bytecode);
      FREE(allocator, right.bytecode);
      return CREX_E_NOMEM;
    }

    safe_memcpy(result->bytecode, left.bytecode, left.size);
    safe_memcpy(result->bytecode + left.size, right.bytecode, right.size);

    FREE(allocator, left.bytecode);
    FREE(allocator, right.bytecode);

    break;
  }

  case PT_ALTERNATION: {
    bytecode_t left, right;

    status_t status = compile_to_bytecode(&left, n_flags, tree->data.children[0], allocator);

    if (status != CREX_OK) {
      return status;
    }

    status = compile_to_bytecode(&right, n_flags, tree->data.children[1], allocator);

    if (status != CREX_OK) {
      FREE(allocator, left.bytecode);
      return status;
    }

    //   VM_SPLIT_PASSIVE right
    //   <left.bytecode>
    //   VM_JUMP end
    // right:
    //   <right.bytecode>
    // end:
    //   VM_TEST_AND_SET_FLAG <flag>

    const size_t max_size = (1 + 4) + left.size + (1 + 4) + right.size + (1 + 4);

    unsigned char *bytecode = ALLOC(allocator, max_size);

    if (bytecode == NULL) {
      FREE(allocator, left.bytecode);
      FREE(allocator, right.bytecode);
      return CREX_E_NOMEM;
    }

    result->bytecode = bytecode;

    const size_t jump_delta = right.size;
    const size_t jump_delta_size = size_for_operand(jump_delta);

    const size_t split_delta = left.size + 1 + jump_delta_size;
    const size_t split_delta_size = size_for_operand(split_delta);

    const size_t flag = (*n_flags)++;
    const size_t flag_size = size_for_operand(flag);

    *(bytecode++) = VM_OP(VM_SPLIT_PASSIVE, split_delta_size);
    serialize_operand(bytecode, split_delta, split_delta_size);
    bytecode += split_delta_size;

    safe_memcpy(bytecode, left.bytecode, left.size);
    bytecode += left.size;

    *(bytecode++) = VM_OP(VM_JUMP, jump_delta_size);
    serialize_operand(bytecode, jump_delta, jump_delta_size);
    bytecode += jump_delta_size;

    safe_memcpy(bytecode, right.bytecode, right.size);
    bytecode += right.size;

    *(bytecode++) = VM_OP(VM_TEST_AND_SET_FLAG, flag_size);
    serialize_operand(bytecode, flag, flag_size);
    bytecode += flag_size;

    result->size = bytecode - result->bytecode;

    FREE(allocator, left.bytecode);
    FREE(allocator, right.bytecode);

    break;
  }

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION: {
    bytecode_t child;

    status_t status = compile_to_bytecode(&child, n_flags, tree->data.repetition.child, allocator);

    if (status != CREX_OK) {
      return status;
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

    result->bytecode = ALLOC(allocator, max_size);

    if (result->bytecode == NULL) {
      FREE(allocator, child.bytecode);
      return CREX_E_NOMEM;
    }

    unsigned char *bytecode = result->bytecode;

    // <child.bytecode>
    // <child.bytecode>
    // ...
    // <child.bytecode> [lower_bound times]
    // ...

    for (size_t i = 0; i < lower_bound; i++) {
      safe_memcpy(bytecode, child.bytecode, child.size);
      bytecode += child.size;
    }

    if (upper_bound == REPETITION_INFINITY) {
      //   ...
      //   VM_SPLIT_{PASSIVE,EAGER} end
      // child:
      //   VM_TEST_AND_SET_FLAG child_flag
      //   <child.bytecode>
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

      *(bytecode++) = VM_OP(leading_split_opcode, leading_split_delta_size);
      serialize_operand(bytecode, leading_split_delta, leading_split_delta_size);
      bytecode += leading_split_delta_size;

      *(bytecode++) = VM_OP(VM_TEST_AND_SET_FLAG, child_flag_size);
      serialize_operand(bytecode, child_flag, child_flag_size);
      bytecode += child_flag_size;

      safe_memcpy(bytecode, child.bytecode, child.size);
      bytecode += child.size;

      *(bytecode++) = VM_OP(trailing_split_opcode, trailing_split_delta_size);
      serialize_operand(bytecode, trailing_split_delta, trailing_split_delta_size);
      bytecode += trailing_split_delta_size;

      *(bytecode++) = VM_OP(VM_TEST_AND_SET_FLAG, end_flag_size);
      serialize_operand(bytecode, end_flag, end_flag_size);
      bytecode += end_flag_size;

      result->size = bytecode - result->bytecode;
    } else {
      //   ...
      //   VM_SPLIT_{PASSIVE,EAGER} end
      //   <child.bytecode>
      //   VM_SPLIT_{PASSIVE,EAGER} end
      //   <child.bytecode>
      //   ... [(uppper_bound - lower_bound) times]
      // end:
      //   VM_TEST_AND_SET_FLAG flag

      const unsigned char split_opcode =
          (tree->type == PT_GREEDY_REPETITION) ? VM_SPLIT_PASSIVE : VM_SPLIT_EAGER;

      // In general, we don't know the minimum size of the operands of the earlier splits until
      // we've determined the minimum size of the operands of the later splits. If we
      // compile_to_bytecode the later splits first (i.e. by filling the buffer from right to left),
      // we can pick the correct size a priori, and then perform a single copy to account for any
      // saved space

      // In general, this optimization leaves a linear amount of memory allocated but unused at
      // the end of the buffer; however, because the top level parsetree is necessarily a PT_GROUP,
      // we always free it before yielding the final compiled regex

      unsigned char *end = result->bytecode + max_size;
      unsigned char *begin = end;

      const size_t flag = (*n_flags)++;
      const size_t flag_size = size_for_operand(flag);

      begin -= flag_size;
      serialize_operand(begin, flag, flag_size);
      *(--begin) = VM_OP(VM_TEST_AND_SET_FLAG, flag_size);

      for (size_t i = 0; i < upper_bound - lower_bound; i++) {
        begin -= child.size;
        safe_memcpy(begin, child.bytecode, child.size);

        const size_t delta = (end - (1 + flag_size)) - begin;
        const size_t delta_size = size_for_operand(delta);

        begin -= delta_size;
        serialize_operand(begin, delta, delta_size);
        *(--begin) = VM_OP(split_opcode, delta_size);
      }

      assert(bytecode <= begin);

      if (bytecode < begin) {
        result->size = max_size - (begin - bytecode);
        memmove(bytecode, begin, end - begin);
      } else {
        result->size = max_size;
      }
    }

    FREE(allocator, child.bytecode);

    break;
  }

  case PT_GROUP: {
    if (tree->data.group.index == NON_CAPTURING_GROUP) {
      return compile_to_bytecode(result, n_flags, tree->data.group.child, allocator);
    }

    bytecode_t child;
    status_t status = compile_to_bytecode(&child, n_flags, tree->data.group.child, allocator);

    if (status != CREX_OK) {
      return status;
    }

    const size_t leading_index = 2 * tree->data.group.index;
    const size_t trailing_index = leading_index + 1;

    const size_t leading_size = size_for_operand(leading_index);
    const size_t trailing_size = size_for_operand(trailing_index);

    result->size = 1 + leading_size + child.size + 1 + trailing_size;
    result->bytecode = ALLOC(allocator, result->size);

    if (result->bytecode == NULL) {
      FREE(allocator, child.bytecode);
      return CREX_E_NOMEM;
    }

    unsigned char *bytecode = result->bytecode;
    *(bytecode++) = VM_OP(VM_WRITE_POINTER, leading_size);

    serialize_operand(bytecode, leading_index, leading_size);
    bytecode += leading_size;

    safe_memcpy(bytecode, child.bytecode, child.size);
    bytecode += child.size;

    *(bytecode++) = VM_OP(VM_WRITE_POINTER, trailing_size);
    serialize_operand(bytecode, trailing_index, trailing_size);
    bytecode += trailing_size;

    assert(result->size == (size_t)(bytecode - result->bytecode));

    FREE(allocator, child.bytecode);

    break;
  }

  default:
    assert(0);
  }

  return CREX_OK;
}
