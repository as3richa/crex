#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void serialize_long32(unsigned char *destination, long value) {
  assert((long)INT32_MIN <= value && value <= (long)INT32_MAX);
  assert(sizeof(int32_t) == 4);

  const int32_t i32_value = (int32_t)value;
  memcpy(destination, &i32_value, 4);
}

/*

ULONG_MAX = 2**32 - 1
LONG_MIN = -2**31 - 1
LONG_MAX = 2**31 - 1
ULONG_MAX = 2**32 - 1
value = -300
unsigned_value = -300 + (2**32 - 1) + 1 = 4294966996
destination = { 212, 254, 255, 255 };

unsigned_result = 0
unsigned_result = 212
unsigned_result = 65236
unsigned_result = 16776916
unsigned_result = 4294966996



unsigned_result - ((unsigned long)LONG_MAX + 1) = 2147483347
... + LONG_MIN = */

static inline long deserialize_long(const unsigned char *source, size_t size) {
  switch (size) {
  case 1:
    return *(const signed char *)source;

  case 2: {
    unsigned long unsigned_value = (unsigned long)source[0] | (((unsigned long)source[1]) << 8);

    if (unsigned_value & 1LU << 15) {
      return -32768L + (long)(unsigned_value ^ (1LU << 15));
    }

    return (long)unsigned_value;
  }

  case 4: {
    const long low_order_bytes =
        (long)((unsigned long)source[0] | (((unsigned long)source[1]) << 8) |
               (((unsigned long)source[2]) << 16));
    const long high_order_byte = ((long)source[3] - 2 * (long)(source[3] & (1 << 7))) * 16777216L;
    return low_order_bytes + high_order_byte;

    const unsigned long unsigned_value =
        (unsigned long)source[0] | (((unsigned long)source[1]) << 8) |
        (((unsigned long)source[2]) << 16) | (((unsigned long)source[3]) << 24);

    return (long)(unsigned_value ^ (unsigned_value & 1LU << 31)) -
           (long)

               if (unsigned_value & 1LU << 31) {
      return -2147483648L + (long)(unsigned_value - (1LU << 31));
    }

    return (long)unsigned_value;

    /*
    int32_t value;
    memcpy(&value, source, 4);
    return value; */
  }

  default:
    assert(0);
    return 0;
  }
}

int main(void) {
  long value;
  scanf("%ld", &value);

  unsigned char buffer[1024];

  for (size_t i = 0; i < 1024; i++) {
    buffer[i] = rand() & 0xff;
  }

  for (size_t i = 0; i < 1024; i++) {
    printf("%d\n", buffer[i]);
  }

  serialize_long32(buffer, value);

  for (size_t i = 0; i < 1024; i++) {
    printf("%d\n", buffer[i]);
  }
  const long result = deserialize_long(buffer, 4);
  printf("%ld\n", result);

  return 0;
}
