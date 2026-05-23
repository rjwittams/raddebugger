#include <stdint.h>

volatile uint64_t watch_value = 0;

__attribute__((noinline)) void
smoke_vector_ready(void)
{
  __asm__ volatile("" ::: "memory");
}

__attribute__((noinline)) void
smoke_vector_stop(void)
{
  __asm__ volatile(
    "movi v0.16b, #0x11\n"
    "movi v8.16b, #0x88\n"
    "movi v31.16b, #0x31\n"
    :
    :
    : "v0", "v8", "v31");
  smoke_vector_ready();
}

__attribute__((noinline)) void
smoke_watch_write(void)
{
  watch_value = 0x1122334455667788ull;
}

__attribute__((noinline)) uint64_t
smoke_unwind_c(uint64_t x)
{
  volatile uint64_t y = x + 3;
  return y;
}

__attribute__((noinline)) uint64_t
smoke_unwind_b(uint64_t x)
{
  volatile uint64_t y = x + 2;
  return smoke_unwind_c(y) + y;
}

__attribute__((noinline)) uint64_t
smoke_unwind_a(uint64_t x)
{
  volatile uint64_t y = x + 1;
  return smoke_unwind_b(y) + y;
}

int
main(void)
{
  smoke_vector_stop();
  smoke_watch_write();
  return (int)smoke_unwind_a(watch_value);
}
