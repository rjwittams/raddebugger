#include <dlfcn.h>

int main(void)
{
  volatile int seed = 20;
  void *library = dlopen("./libsymbols.dylib", RTLD_NOW);
  if(!library) {return 1;}
  void (*caller)(int) = (void (*)(int))dlsym(library, "symbol_library_caller");
  if(!caller) {return 2;}
  caller(seed);
  dlclose(library);
  return 0;
}
