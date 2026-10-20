// Built without debug information; only the Mach-O symbol table names frames.
__attribute__((noinline)) void symbol_library_leaf(int input)
{
  volatile int local = input + 2;
  if(local == 22) {__builtin_trap();}
}

__attribute__((noinline)) void symbol_library_caller(int input)
{
  symbol_library_leaf(input);
}
