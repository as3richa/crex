#ifndef NDEBUG

#include <stdio.h>

static const char *anchor_type_to_str(anchor_type_t type) {
  switch (type) {
  case AT_BOF:
    return "\\A";

  case AT_BOL:
    return "^";

  case AT_EOF:
    return "\\z";

  case AT_EOL:
    return "$";

  case AT_WORD_BOUNDARY:
    return "\\w";

  case AT_NOT_WORD_BOUNDARY:
    return "\\W";

  default:
    assert(0);
    return NULL;
  }
}

const char *opcode_to_str(unsigned char opcode) {
  switch (opcode) {
  case VM_CHARACTER:
    return "VM_CHARACTER";

  case VM_CHAR_CLASS:
    return "VM_CHAR_CLASS";

  case VM_BUILTIN_CHAR_CLASS:
    return "VM_BUILTIN_CHAR_CLASS";

  case VM_ANCHOR_BOF:
    return "VM_ANCHOR_BOF";

  case VM_ANCHOR_BOL:
    return "VM_ANCHOR_BOL";

  case VM_ANCHOR_EOF:
    return "VM_ANCHOR_EOF";

  case VM_ANCHOR_EOL:
    return "VM_ANCHOR_EOL";

  case VM_ANCHOR_WORD_BOUNDARY:
    return "VM_ANCHOR_WORD_BOUNDARY";

  case VM_ANCHOR_NOT_WORD_BOUNDARY:
    return "VM_ANCHOR_NOT_WORD_BOUNDARY";

  case VM_JUMP:
    return "VM_JUMP";

  case VM_SPLIT_PASSIVE:
    return "VM_SPLIT_PASSIVE";

  case VM_SPLIT_EAGER:
    return "VM_SPLIT_EAGER";

  case VM_SPLIT_BACKWARDS_PASSIVE:
    return "VM_SPLIT_BACKWARDS_PASSIVE";

  case VM_SPLIT_BACKWARDS_EAGER:
    return "VM_SPLIT_BACKWARDS_EAGER";

  case VM_WRITE_POINTER:
    return "VM_WRITE_POINTER";

  case VM_TEST_AND_SET_FLAG:
    return "VM_TEST_AND_SET_FLAG";

  default:
    assert(0);
    return NULL;
  }
}

static void print_char_class(const char_class_t char_class, FILE *file) {
  fputc('[', file);

  for (int c = 0; c <= 255;) {
    if (!bitmap_test(char_class, c)) {
      c++;
      continue;
    }

    int d;

    for (d = c; (d + 1) <= 255 && bitmap_test(char_class, d + 1); d++) {
    }

    if (c == '-' || c == '[' || c == ']' || c == '^') {
      fprintf(file, "\\%c", c);
    } else if (isprint(c) || c == ' ') {
      fputc(c, file);
    } else {
      fprintf(file, "\\x%02x", c);
    }

    if (c != d) {
      fputc('-', file);

      if (d == '-' || d == '[' || d == ']' || d == '^') {
        fprintf(file, "\\%c", d);
      } else if (isprint(d) || d == ' ') {
        fputc(d, file);
      } else {
        fprintf(file, "\\x%02x", d);
      }
    }

    c = d + 1;
  }

  fputc(']', file);
}

status_t crex_print_tokenization(const char *pattern, size_t size, FILE *file) {
  const char *eof = pattern + size;

  char_classes_t classes = {0, 0, NULL};

  token_t token;

  while (pattern != eof) {
    const status_t status = lex(&classes, &token, &pattern, eof, &default_allocator);

    if (status != CREX_OK) {
      free(classes.buffer);
      return status;
    }

    switch (token.type) {
    case TT_CHARACTER:
      fputs("TT_CHARACTER ", file);

      if (isprint(token.data.character)) {
        fputc(token.data.character, file);
      } else {
        fprintf(file, "'\\x%02x'", (unsigned int)token.data.character);
      }

      fputc('\n', file);

      break;

    case TT_CHAR_CLASS:
      fputs("TT_CHAR_CLASS ", file);
      print_char_class(classes.buffer[token.data.char_class_index], file);
      fputc('\n', file);
      break;

    case TT_BUILTIN_CHAR_CLASS: {
      fputs("TT_BUILTIN_CHAR_CLASS ", file);
      print_char_class(builtin_classes[token.data.char_class_index], file);
      fputc('\n', file);
      break;
    }

    case TT_ANCHOR:
      fprintf(file, "TT_ANCHOR %s\n", anchor_type_to_str(token.data.anchor_type));
      break;

    case TT_PIPE:
      fputs("TT_PIPE\n", file);
      break;

    case TT_GREEDY_REPETITION:
    case TT_LAZY_REPETITION: {
      const char *str =
          (token.type == TT_GREEDY_REPETITION) ? "TT_GREEDY_REPETITION" : "TT_LAZY_REPETITION";

      fprintf(file, "%s %zu .. ", str, token.data.repetition.lower_bound);

      if (token.data.repetition.upper_bound == REPETITION_INFINITY) {
        fputs("inf\n", file);
      } else {
        fprintf(file, "%zu\n", token.data.repetition.upper_bound);
      }

      break;
    }

    case TT_OPEN_PAREN:
      fputs("TT_OPEN_PAREN\n", file);
      break;

    case TT_NON_CAPTURING_OPEN_PAREN:
      fputs("TT_NON_CAPTURING_OPEN_PAREN\n", file);
      break;

    case TT_CLOSE_PAREN:
      fputs("TT_CLOSE_PAREN\n", file);
      break;

    default:
      assert(0);
    }
  }

  free(classes.buffer);

  return CREX_OK;
}

static void
print_parsetree(const parsetree_t *tree, size_t depth, const char_classes_t *classes, FILE *file);

status_t crex_print_parsetree(const char *pattern, size_t size, FILE *file) {
  status_t status;

  size_t n_capturing_groups;

  char_classes_t classes = {0, 0, NULL};

  parsetree_t *tree =
      parse(&status, &n_capturing_groups, &classes, pattern, size, &default_allocator);

  if (tree == NULL) {
    free(classes.buffer);
    return status;
  }

  print_parsetree(tree, 0, &classes, file);
  fputc('\n', file);

  free(classes.buffer);
  destroy_parsetree(tree, &default_allocator);

  return CREX_OK;
}

static void
print_parsetree(const parsetree_t *tree, size_t depth, const char_classes_t *classes, FILE *file) {
  for (size_t i = 0; i < depth; i++) {
    fputc(' ', file);
  }

  switch (tree->type) {
  case PT_EMPTY:
    fputs("(PT_EMPTY)", file);
    break;

  case PT_CHARACTER:
    fputs("(PT_CHARACTER ", file);

    if (isprint(tree->data.character)) {
      fprintf(file, "'%c')", tree->data.character);
    } else {
      fprintf(file, "'\\x%02x')", (unsigned int)tree->data.character);
    }

    break;

  case PT_CHAR_CLASS:
    fputs("(PT_CHAR_CLASS ", file);
    print_char_class(classes->buffer[tree->data.char_class_index], file);
    fputc(')', file);
    break;

  case PT_BUILTIN_CHAR_CLASS: {
    fputs("(PT_BUILTIN_CHAR_CLASS ", file);
    print_char_class(builtin_classes[tree->data.char_class_index], file);
    fputc(')', file);
    break;
  }

  case PT_ANCHOR:
    fprintf(file, "(PT_ANCHOR %s)", anchor_type_to_str(tree->data.anchor_type));
    break;

  case PT_CONCATENATION:
    fputs("(PT_CONCATENATION\n", file);
    print_parsetree(tree->data.children[0], depth + 1, classes, file);
    fputc('\n', file);
    print_parsetree(tree->data.children[1], depth + 1, classes, file);
    fputc(')', file);
    break;

  case PT_ALTERNATION:
    fputs("(PT_ALTERNATION\n", file);
    print_parsetree(tree->data.children[0], depth + 1, classes, file);
    fputc('\n', file);
    print_parsetree(tree->data.children[1], depth + 1, classes, file);
    fputc(')', file);
    break;

  case PT_GREEDY_REPETITION:
  case PT_LAZY_REPETITION: {
    const char *str =
        (tree->type == PT_GREEDY_REPETITION) ? "PT_GREEDY_REPETITION" : "PT_LAZY_REPETITION";

    fprintf(file, "(%s %zu ", str, tree->data.repetition.lower_bound);

    if (tree->data.repetition.upper_bound == REPETITION_INFINITY) {
      fputs("inf\n", file);
    } else {
      fprintf(file, "%zu\n", tree->data.repetition.upper_bound);
    }

    print_parsetree(tree->data.repetition.child, depth + 1, classes, file);
    fputc(')', file);

    break;
  }

  case PT_GROUP:
    fprintf(file, "(PT_GROUP ");

    if (tree->data.group.index == NON_CAPTURING_GROUP) {
      fputs("<non-capturing>\n", file);
    } else {
      fprintf(file, "%zu\n", tree->data.group.index);
    }

    print_parsetree(tree->data.group.child, depth + 1, classes, file);

    fputc(')', file);

    break;

  default:
    assert(0);
  }
}

void crex_print_bytecode(const regex_t *regex, FILE *file) {
  const unsigned char *base = BYTECODE_CODE(regex->bytecode);
  const unsigned char *code = base;

  for (;;) {
    size_t i = code - base;
    assert(i <= regex->bytecode.size);

    fprintf(file, "%05zd", i);

    if (i == regex->bytecode.size) {
      fputc('\n', file);
      break;
    }

    const unsigned char byte = *(code++);

    const unsigned char opcode = VM_OPCODE(byte);
    const size_t operand_size = VM_OPERAND_SIZE(byte);

    const size_t operand = deserialize_operand(code, operand_size);
    code += operand_size;

    i += (1 + operand_size);

    fprintf(file, " %s", opcode_to_str(opcode));

    switch (opcode) {
    case VM_CHARACTER: {
      assert(operand <= 0xffu);

      if (isprint(operand) || operand == ' ') {
        fprintf(file, " '%c'\n", (char)operand);
      } else {
        fprintf(file, " \\x%02zx\n", operand);
      }

      break;
    }

    case VM_CHAR_CLASS:
    case VM_BUILTIN_CHAR_CLASS: {
      fputc(' ', file);

      char_class_t *char_class =
          (opcode == VM_CHAR_CLASS) ? &regex->classes[operand] : &builtin_classes[operand];
      print_char_class(*char_class, file);

      fputc('\n', file);

      break;
    }

    case VM_JUMP:
    case VM_SPLIT_PASSIVE:
    case VM_SPLIT_EAGER:
    case VM_SPLIT_BACKWARDS_PASSIVE:
    case VM_SPLIT_BACKWARDS_EAGER: {
      size_t destination;

      if (opcode == VM_SPLIT_BACKWARDS_PASSIVE || opcode == VM_SPLIT_BACKWARDS_EAGER) {
        destination = i - operand;
      } else {
        destination = i + operand;
      }

      assert(destination < regex->bytecode.size);

      fprintf(file, " %zu (=> %zu)\n", operand, destination);

      break;
    }

    case VM_WRITE_POINTER:
    case VM_TEST_AND_SET_FLAG: {
      fprintf(file, " %zu\n", operand);
      break;
    }

    default:
      assert(operand_size == 0);
      fputc('\n', file);
    }
  }
}

#ifdef NATIVE_COMPILER

void crex_dump_native_code(const regex_t *regex, FILE *file) {
  // FIXME: check IO errors in this and other debug functions?
  fwrite(regex->native_code.code, 1, regex->native_code.size, file);
}

#endif

#endif
