// Licensed under the MIT license (https://opensource.org/license/mit/)

__attribute__((noinline)) unsigned long
trap_test_call(void)
{
  return 42;
}

int
main(void)
{
  // Pass the callable address to the harness without relying on debug info.
  __asm__ volatile("mov x20, %0\n\tbrk #0x123" :: "r"(trap_test_call) : "x20");
  return 23;
}
