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
INLINE int wrapper(int input)
{
  int value = leaf(input);
  return value + 1;
}
int main(void)
{
  volatile int seed = 20;
  result = wrapper(seed);
  return result == 42 ? 0 : 1;
}
