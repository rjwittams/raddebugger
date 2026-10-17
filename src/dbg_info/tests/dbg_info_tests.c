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
    {str8_lit(""), str8_lit(".rdi")},
    {str8_lit("x"), str8_lit("x.rdi")},
    {str8_lit(".dSYM/foo"), str8_lit(".rdi")},
    {str8_lit("/outer.dSYM/inner.dSYM/Contents/Resources/DWARF/inner"), str8_lit("/outer.dSYM/inner.rdi")},
    {str8_lit("C:\\build\\Foo.DsYm\\Contents\\Resources\\DWARF\\Foo"), str8_lit("C:\\build\\Foo.rdi")},
    {str8_lit("/build/foo.dSYM"), str8_lit("/build/foo.dSYM.rdi")},
  };

  for EachElement(idx, tests)
  {
    String8 rdi_path = di_rdi_path_from_original_path(arena, tests[idx].original_path);
    TestCheck(str8_match(rdi_path, tests[idx].expected_rdi_path, 0));
  }
}
