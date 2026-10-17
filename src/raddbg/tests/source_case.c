int case_upper(int);
int case_lower(int);

int main(void)
{
  volatile int seed = 20;
  volatile int upper = case_upper(seed);
  volatile int lower = case_lower(seed+10);
  return upper+lower == 82 ? 0 : 1;
}
