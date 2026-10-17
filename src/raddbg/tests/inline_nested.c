#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#define INLINE static __forceinline
#else
#define NOINLINE __attribute__((noinline))
#define INLINE static inline __attribute__((always_inline))
#endif
volatile int result;
NOINLINE int leaf(int input)
{
  volatile int doubled = input * 2; // SNAPSHOT_STOP
  return doubled + 1;
}
#include "inline_nested.h"
INLINE int outer(int input)
{
  int value = wrapper(input); // OUTER_CALL
  return value + 1;
}
int main(void)
{
  volatile int seed = 20;
  int first = outer(seed); // MAIN_CALL_1
  int second = outer(seed + 1); // MAIN_CALL_2
  result = first + second;
  return result == 88 ? 0 : 1;
}
