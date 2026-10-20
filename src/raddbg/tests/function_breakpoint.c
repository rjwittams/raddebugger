volatile int result;

__attribute__((noinline)) int dead_function(int input)
{
  return input * 3;
}

__attribute__((noinline)) int leaf(int input)
{
  volatile int doubled = input * 2;
  return doubled + 1;
}

static inline __attribute__((always_inline)) int wrapper(int input)
{
  return leaf(input) + 1;
}

int main(void)
{
  volatile int seed = 20;
  result = wrapper(seed);
  return result == 42 ? 0 : 1;
}
