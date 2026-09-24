// Caller locals must remain readable at every instruction in the assembly leaf.
extern void unwind_leaf(int);
int main(void)
{
  volatile int seed = 20;
  unwind_leaf(seed);
  return seed == 20 ? 0 : 1;
}
