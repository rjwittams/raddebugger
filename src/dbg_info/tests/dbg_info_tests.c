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

Test(di_match_scope)
{
  Arena *bake_arena = arena_alloc();
  RDIM_BakeParams params = {0};
  params.subset_flags = RDIM_SubsetFlag_All;
  params.top_level_info.arch = RDI_Arch_X64;
  RDIM_Unit *unit = rdim_unit_chunk_list_push(bake_arena, &params.units, 1);
  RDIM_Type *type = rdim_type_chunk_list_push(bake_arena, &params.types, 1);
  type->kind = RDI_TypeKind_S32;
  type->byte_size = 4;
  RDIM_Symbol *symbol = rdim_symbol_chunk_list_push(bake_arena, &unit->global_variables, 1);
  symbol->name = str8_lit("scope_global");
  symbol->type = type;
  RDIM_Location location = {.kind = RDI_LocationKind_ModuleOff, .offset = 0x40};
  rdim_location_case_list_push(bake_arena, &symbol->location_cases, location, (RDIM_Rng1U64){0, max_U64});
  U64 broadcast = 0;
  LaneCtx restore = lane_ctx(((LaneCtx){.lane_count = 1, .broadcast_memory = &broadcast}));
  RDIM_BakeResults baked = rdim_bake(bake_arena, &params);
  lane_ctx(restore);
  String8List blobs = rdim_file_blobs_from_section_bundle(bake_arena, &baked.section_bundle);
  TestCheck(t_write_file_list(str8_lit("scope.rdi"), blobs));
  arena_release(bake_arena);

  // Both timestamp identities remain loaded, as with a saved entry and a rebuilt target.
  String8 path = t_make_file_path(arena, str8_lit("scope.rdi"));
  DI_Key keys[2] = {0};
  Access *access = access_open();
  for EachElement(idx, keys)
  {
    keys[idx] = di_key_from_path_timestamp(path, idx+1);
    di_open(keys[idx]);
    RDI_Parsed *rdi = di_rdi_from_key(access, keys[idx], 1, now_time_us()+5000000);
    TestCheck(rdi->raw_data_size != 0);
  }
  TestCheck(!di_key_match(keys[0], keys[1]));
  // Alternating scopes must not reuse the other scope's cached match.
  for(U64 idx = 0; idx < 4; idx += 1)
  {
    DI_KeyArray scope = {.v = &keys[idx%2], .count = 1};
    DI_Match match = di_match_from_string(str8_lit("scope_global"), 0, 0, 1, di_key_zero(), now_time_us()+5000000, &scope);
    TestCheck(!match.stale && match.idx != 0);
    TestCheck(di_key_match(match.key, keys[idx%2]));
  }
  DI_KeyArray empty_scope = {0};
  DI_Match empty = di_match_from_string(str8_lit("scope_global"), 0, 0, 1, keys[0], now_time_us()+5000000, &empty_scope);
  TestCheck(!empty.stale && empty.idx == 0);
  DI_Match global_match = di_match_from_string(str8_lit("scope_global"), 0, 0, 1, keys[0], now_time_us()+5000000, 0);
  TestCheck(!global_match.stale && global_match.idx != 0);
  DI_KeyArray other_scope = {.v = &keys[1], .count = 1};
  DI_Match qualified = di_match_from_string(str8_lit("scope_global"), 0, 0, 0, keys[0], now_time_us()+5000000, &other_scope);
  TestCheck(!qualified.stale && qualified.idx == 0);
  access_close(access);
  for EachElement(idx, keys) {di_close(keys[idx], 0);}
}
