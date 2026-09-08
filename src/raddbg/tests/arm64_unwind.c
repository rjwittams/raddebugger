// Caller locals must remain readable at every instruction in the assembly leaf.
extern void unwind_leaf(void);
int main(void)
{
  volatile int seed = 20;
  unwind_leaf();
  return seed == 20 ? 0 : 1;
}
