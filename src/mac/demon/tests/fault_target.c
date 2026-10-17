// Licensed under the MIT license (https://opensource.org/license/mit/)

#include <stdint.h>

int
main(void)
{
  return *(volatile unsigned char *)(uintptr_t)0x12345;
}
