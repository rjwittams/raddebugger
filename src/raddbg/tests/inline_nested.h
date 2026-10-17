INLINE int wrapper(int input)
{
  int value;
  {
    volatile int adjusted = input;
    value = leaf(adjusted); // INLINE_BODY
    result = value;
  }
  return value + 1;
}
