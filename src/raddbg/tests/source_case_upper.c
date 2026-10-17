#line 1 "CaseLookup/Shared.h"
__attribute__((noinline)) int case_upper(int seed)
{
  volatile int upper_marker = seed+10;
  upper_marker += 1;
  return upper_marker;
}
