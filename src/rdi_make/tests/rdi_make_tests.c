// Licensed under the MIT license (https://opensource.org/license/mit/)

Test(rdim_location_ranges)
{
  Arena *bake_arena = arena_alloc();
  RDIM_BakeParams params = {0};
  params.subset_flags = RDIM_SubsetFlag_All;
  params.top_level_info.arch = RDI_Arch_X64;
  RDIM_Unit *unit = rdim_unit_chunk_list_push(bake_arena, &params.units, 1);
  RDIM_Scope *scope = rdim_scope_chunk_list_push(bake_arena, &unit->scopes, 1);
  rdim_scope_push_voff_range(bake_arena, &unit->scopes, scope, (RDIM_Rng1U64){0, 100});
  RDIM_Type *type = rdim_type_chunk_list_push(bake_arena, &params.types, 1);
  type->kind = RDI_TypeKind_S32;
  type->byte_size = 4;

  struct
  {
    String8 name;
    U64 count;
    RDIM_Rng1U64 ranges[2];
  }
  cases[] =
  {
    {str8_lit("absent"),    0},
    {str8_lit("unbounded"), 1, {{0, max_U64}}},
    {str8_lit("bounded"),   1, {{10, 20}}},
    {str8_lit("adjacent"),  2, {{10, 20}, {20, 30}}},
    {str8_lit("gap"),       2, {{10, 20}, {30, 40}}},
    {str8_lit("empty"),     1, {{20, 20}}},
  };
  RDI_RegCode regs[] = {RDI_RegCodeX64_rax, RDI_RegCodeX64_rdx};
  for EachElement(case_idx, cases)
  {
    RDIM_Symbol *local = rdim_symbol_chunk_list_push(bake_arena, &scope->locals, 8);
    local->name = cases[case_idx].name;
    local->type = type;
    local->container_scope = scope;
    for EachIndex(loc_idx, cases[case_idx].count)
    {
      RDIM_Location location = {.kind = RDI_LocationKind_ValReg, .reg_code = regs[loc_idx]};
      rdim_location_case_list_push(bake_arena, &local->location_cases, location, cases[case_idx].ranges[loc_idx]);
    }
  }

  U64 broadcast = 0;
  LaneCtx restore = lane_ctx(((LaneCtx){.lane_count = 1, .broadcast_memory = &broadcast}));
  RDIM_BakeResults baked = rdim_bake(bake_arena, &params);
  lane_ctx(restore);
  String8List blobs = rdim_file_blobs_from_section_bundle(bake_arena, &baked.section_bundle);
  String8 data = str8_list_join(arena, &blobs, 0);
  arena_release(bake_arena);
  RDI_Parsed rdi = {0};
  TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
  U64 locals_count = 0;
  RDI_Symbol *locals = rdi_table_from_name(&rdi, LocalVariables, &locals_count);
  TestCheck(locals_count == ArrayCount(cases)+1);

  U64 voffs[] = {0, 9, 10, 19, 20, 29, 30, 39, 40, max_U64};
  for EachElement(case_idx, cases)
  {
    RDI_Location encoded = locals[case_idx+1].location;
    if(case_idx == 1)
    {
      TestCheck(rdi_kind_from_location(encoded) == RDI_LocationKind_ValReg);
    }
    for EachElement(voff_idx, voffs)
    {
      U64 voff = voffs[voff_idx];
      RDI_Location actual = rdi_location_from_location_voff(&rdi, encoded, voff);
      RDI_RegCode expected = 0;
      for EachIndex(loc_idx, cases[case_idx].count)
      {
        RDIM_Rng1U64 range = cases[case_idx].ranges[loc_idx];
        if(case_idx == 1 || (range.min <= voff && voff < range.max))
        {
          expected = regs[loc_idx];
          break;
        }
      }
      if((expected == 0 && actual != 0) ||
         (expected != 0 && (rdi_kind_from_location(actual) != RDI_LocationKind_ValReg ||
                            rdi_regcode_from_location(actual) != expected)))
      {
        test_outf("%S at %I64u: expected register %u, got location %#I64x\n", cases[case_idx].name, voff, expected, actual);
        TestCheck(0);
      }
    }
  }
}
