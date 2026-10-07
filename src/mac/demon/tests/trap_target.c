// Licensed under the MIT license (https://opensource.org/license/mit/)

#include <stdio.h>
#include <string.h>
#include <signal.h>

__attribute__((noinline)) unsigned long
trap_test_call(void)
{
  return 42;
}

int
main(int argc, char **argv)
{
  const char *completion = "completed\n";
  if(argc >= 2 && strcmp(argv[1], "healthy") == 0)
  {
    raise(SIGSTOP);
  }
  else if(argc < 2 || strcmp(argv[1], "init") != 0)
  {
    // Pass the callable and alternate resume addresses without debug info.
    __asm__ goto volatile("mov x20, %0\n\tmov x21, %1\n\tbrk #0x123"
                          :: "r"(trap_test_call), "r"(&&moved_pc) : "x20", "x21" : moved_pc);
  }
  goto complete;

  moved_pc:
  completion = "moved\n";
  complete:
  if(argc == 3)
  {
    FILE *marker = fopen(argv[2], "wb");
    if(marker == 0)
    {
      return 2;
    }
    fputs(completion, marker);
    if(fclose(marker) != 0)
    {
      return 2;
    }
  }
  return 23;
}
