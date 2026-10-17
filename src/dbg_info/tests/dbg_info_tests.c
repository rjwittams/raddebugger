// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

Test(di_rdi_path_from_original_path)
{
  struct
  {
    String8 original_path;
    String8 expected_rdi_path;
  }
  tests[] =
  {
    {str8_lit("/build/foo"), str8_lit("/build/foo.rdi")},
    {str8_lit("/build/foo.dSYM/Contents/Resources/DWARF/foo"), str8_lit("/build/foo.rdi")},
    {str8_lit("/build/Foo.app.dSYM/Contents/Resources/DWARF/Foo"), str8_lit("/build/Foo.app.rdi")},
  };

  for EachElement(idx, tests)
  {
    String8 rdi_path = di_rdi_path_from_original_path(arena, tests[idx].original_path);
    TestCheck(str8_match(rdi_path, tests[idx].expected_rdi_path, 0));
  }
}
