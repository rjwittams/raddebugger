#define assert(expr) !(expr) ? __builtin_trap() : (void)0

__attribute__((noinline)) int check_value(int input)
{
  volatile int local = input + 2;
  assert(local == 23);
  return local;
}

__attribute__((noinline)) int caller(void)
{
  volatile int seed = 20;
  int result = check_value(seed);
  return result + 1;
}

int main(void)
{
  volatile int sentinel = 7;
  return caller() + sentinel;
}
