#line 1 "CaseLookup/shared.h"
__attribute__((noinline)) int case_lower(int seed)
{
  volatile int lower_marker = seed+20;
  lower_marker += 1;
  return lower_marker;
}
