// FIXME: every name here is awful

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

static size_t deserialize_operand(const void *source, size_t size) {
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

  default:
    UNREACHABLE();
    return 0;
  }
}

static void serialize_operand_le(void *destination, size_t operand, size_t size) {
  unsigned char *bytes = destination;

  switch (size) {
  case 0: {
    assert(operand == 0);
    return;
  }

  case 1: {
    assert(operand <= 0xffLU);
    bytes[0] = operand;
    return;
  }

  case 2: {
    assert(operand <= 0xffffLU);
    bytes[0] = operand & 0xffu;
    bytes[1] = operand >> 8u;
    return;
  }

  case 4: {
    assert(operand <= 0xffffffffLU);
    bytes[0] = operand & 0xffu;
    bytes[1] = (operand >> 8u) & 0xffu;
    bytes[2] = (operand >> 16u) & 0xffu;
    bytes[3] = (operand >> 24u) & 0xffu;
    return;
  }

#ifdef NATIVE_COMPILER
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
    return;
  }
#endif

  default:
    UNREACHABLE();
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
