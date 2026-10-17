// Do not initialize TLS before the stop: only the cold resolver uses the stack.
typedef unsigned long long U64;
_Thread_local volatile U64 red_tls = 123;
extern U64 red_zone_leaf(void);
int main(void) {
  volatile U64 result = red_zone_leaf();
  return result != 1234;
}
