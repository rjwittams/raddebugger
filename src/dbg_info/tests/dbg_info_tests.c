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

Test(di_build_identity_keys)
{
  String8 path = t_make_file_path(arena, str8_lit("same-build-path"));
  Guid first_guid = {.data1 = 1};
  Guid second_guid = {.data1 = 2};
  DI_Key first = di_key_from_path_timestamp_identity(path, 123, Arch_arm64, first_guid);
  DI_Key second = di_key_from_path_timestamp_identity(path, 123, Arch_arm64, second_guid);
  DI_Key other_arch = di_key_from_path_timestamp_identity(path, 123, Arch_x64, first_guid);
  DI_Key portable = di_key_from_path_timestamp(path, 123);
  TestCheck(!di_key_match(first, second));
  TestCheck(!di_key_match(first, other_arch));
  TestCheck(!di_key_match(first, portable));
  TestCheck(di_key_match(first, di_key_from_path_timestamp_identity(path, 123, Arch_arm64, first_guid)));
  TestCheck(di_key_match(second, di_key_from_path_timestamp_identity(path, 123, Arch_arm64, second_guid)));
  TestCheck(di_key_match(portable, di_key_from_path_timestamp(path, 123)));
  TestCheck(!di_key_match(first, di_key_from_path_timestamp_identity(str8_lit("another-build-path"), 123, Arch_arm64, first_guid)));
}

Test(di_rdi_identity_paths)
{
  Guid guid = {.data1=1};
  Guid other_guid = {.data1=2};
  String8 path = str8_lit("/one/foo.dSYM/Contents/Resources/DWARF/foo");
  String8 other_path = str8_lit("/two/foo.dSYM/Contents/Resources/DWARF/foo");
  for(U64 cache = 0; cache < 2; cache += 1)
  {
    String8 folder = cache ? str8_lit("/cache") : str8_zero();
    String8 first = di_rdi_path_from_identity(arena, path, folder, Arch_arm64, guid);
    String8 other = di_rdi_path_from_identity(arena, path, folder, Arch_arm64, other_guid);
    String8 arch = di_rdi_path_from_identity(arena, path, folder, Arch_x64, guid);
    String8 file = di_rdi_path_from_identity(arena, other_path, folder, Arch_arm64, guid);
    TestCheck(!str8_match(first, other, 0));
    TestCheck(!str8_match(first, arch, 0));
    TestCheck(!str8_match(first, file, 0));
    TestCheck(str8_match(first, di_rdi_path_from_identity(arena, path, folder, Arch_arm64, guid), 0));
    // The portable fallback must distinguish same-basename inputs too.
    first = di_rdi_path_from_identity(arena, path, folder, Arch_Null, (Guid){0});
    file = di_rdi_path_from_identity(arena, other_path, folder, Arch_Null, (Guid){0});
    TestCheck(!str8_match(first, file, 0));
  }
  TestCheck(str8_match(di_rdi_path_from_identity(arena, str8_lit("/one/foo.pdb"), str8_zero(), Arch_Null, (Guid){0}),
                      str8_lit("/one/foo.pdb.rdi"), 0));
}

Test(di_rdi_identity)
{
  Guid expected = {.data1=0x12345678, .data2=0x9abc, .data3=0xdef0,
                   .data4={1,2,3,4,5,6,7,8}};
  for(U64 cpu = 0; cpu < 2; cpu += 1) for(U64 test_case = 0; test_case < 7; test_case += 1)
  {
    RDIM_BakeParams params = {0};
    params.subset_flags = RDIM_SubsetFlag_All;
    params.top_level_info.arch = (cpu != (test_case == 2)) ? RDI_Arch_ARM64 : RDI_Arch_X64;
    MemoryCopy(&params.top_level_info.guid, &expected, sizeof(expected));
    if(test_case == 1) {params.top_level_info.guid.u8[0] ^= 1;}
    if(test_case == 3) {MemoryZeroStruct(&params.top_level_info.guid);}
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count=1, .broadcast_memory=&broadcast}));
    RDIM_BakeResults baked = rdim_bake(arena, &params);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(arena, &baked.section_bundle);
    String8 data = str8_list_join(arena, &blobs, 0);
    if(test_case == 4) {data.size = sizeof(RDI_Header);}
    if(test_case == 5) {((RDI_Header *)data.str)->magic = 0;}
    String8 name = str8f(arena, "%I64u-%I64u.rdi", cpu, test_case);
    TestCheck(t_write_file(name, data));
    File file = file_open(AccessFlag_Read|AccessFlag_ShareRead, t_make_file_path(arena, name));
    B32 matches = di_rdi_file_matches_identity(file, cpu ? Arch_arm64 : Arch_x64, test_case == 6 ? (Guid){0} : expected);
    file_close(file);
    test_outf("identity case %I64u: %u\n", test_case, matches);
    TestCheck(matches == (test_case == 0));
  }
}
