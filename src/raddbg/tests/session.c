#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
volatile int result;
NOINLINE int leaf(int input)
{
  volatile int doubled = input * 2;
  return doubled + 1;
}
int main(void)
{
  volatile int seed = 20;
  result = leaf(seed);
  result += 1;
#ifndef EXIT_CODE
#define EXIT_CODE 0
#endif
  return result == 42 ? EXIT_CODE : 1;
}
