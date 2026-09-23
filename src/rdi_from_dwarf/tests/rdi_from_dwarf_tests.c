// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

Test(d2r_high_pc_voff)
{
  DW2_ParseCtx unit_parse_ctx = {0};
  unit_parse_ctx.version = DW_Version_5;

  U64 base_vaddr = 0x10000000;
  U64 voff_base = 0x120;

  DW2_FormVal high_pc_addr = {0};
  high_pc_addr.kind = DW_FormKind_Addr;
  high_pc_addr.addr = base_vaddr + 0x180;
  TestCheck(d2r_voff_opl_from_high_pc(&unit_parse_ctx, base_vaddr, voff_base, &high_pc_addr) == 0x180);

  DW2_FormVal high_pc_size = {0};
  high_pc_size.kind = DW_FormKind_Data4;
  high_pc_size.u128.u64[0] = 0x60;
  TestCheck(d2r_voff_opl_from_high_pc(&unit_parse_ctx, base_vaddr, voff_base, &high_pc_size) == 0x180);
}

Test(d2r_line_extended_opcode)
{
  U8 abbrev[] = {1,0x11,1, 3,8, 0x1b,8, 0x10,0x17, 0x11,1, 0x12,6, 0,0,
                 2,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0, 0};
  U8 info[] = {0,0,0,0, 4,0, 0,0,0,0, 8, 1,
               'm','a','i','n','.','c',0, '/','s','r','c',0,
               0,0,0,0, 0,0x10,0,0,0,0,0,0, 0,0x20,0,0,
               2,'i','n','t',0,5,4, 0};
  U32 info_size = sizeof(info) - 4;
  MemoryCopy(info, &info_size, 4);
  U8 info5[sizeof(info)+1] = {0,0,0,0, 5,0, 1,8, 0,0,0,0};
  MemoryCopy(info5+12, info+11, sizeof(info)-11);
  U32 info5_size = sizeof(info5) - 4;
  MemoryCopy(info5, &info5_size, 4);
  U8 header[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                 0,1,1,1,1,0,0,0,1,0,0,1, 0,
                 'm','a','i','n','.','c',0, 0,0,0, 0};
  U32 header_size = sizeof(header) - 10;
  MemoryCopy(header+6, &header_size, 4);
  U8 header5[] = {0,0,0,0, 5,0,8,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                  0,1,1,1,1,0,0,0,1,0,0,1,
                  1,1,8,1, '/','s','r','c',0,
                  2,1,8,2,0x0f,1, 'm','a','i','n','.','c',0,0};
  U32 header5_size = sizeof(header5) - 12;
  MemoryCopy(header5+8, &header5_size, 4);
  U8 first[] = {0,9,2, 0,0x10,0,0,0,0,0,0, 1, 3,1, 2,4, 1, 2,4, 0,1,1};
  U8 second[] = {0,9,2, 0,0x20,0,0,0,0,0,0, 1, 3,1, 2,4, 1, 2,4, 0,1,1};
  // An unknown extended opcode's payload must not become line instructions.
  U8 long_padding[133] = {0,0x82,1,0}; // 130-byte extent, two-byte ULEB length.
  struct {String8 name, data; U64 sequences;} cases[] =
  {
    {str8_lit("control"),             str8_zero(),                        2},
    {str8_lit("unknown-zero"),        str8_lit("\0\2\0\0"),             2},
    {str8_lit("vendor-low"),          str8_lit("\0\5\200\3\1\1\0"),   2},
    {str8_lit("vendor-high"),         str8_lit("\0\2\377\0"),           2},
    {str8_lit("no-payload"),          str8_lit("\0\1\0"),                2},
    {str8_lit("long-payload"),        str8_array_fixed(long_padding),      2},
    {str8_lit("padded-known-opcode"), str8_lit("\0\3\4\7\0"),          2},
    {str8_lit("zero-extent"),         str8_lit("\0\0"),                  1},
    {str8_lit("oversized-extent"),    str8_lit("\0\377\177\0"),         1},
  };
  for(U64 version = 4; version <= 5; version += 1) for EachElement(case_idx, cases)
  {
    String8List parts = {0};
    U8 set_file[] = {4, version == 5 ? 0 : 1};
    str8_list_push(arena, &parts, version == 5 ? str8_array_fixed(header5) : str8_array_fixed(header));
    str8_list_push(arena, &parts, str8_array_fixed(set_file));
    str8_list_push(arena, &parts, str8_array_fixed(first));
    str8_list_push(arena, &parts, cases[case_idx].data);
    str8_list_push(arena, &parts, str8_array_fixed(set_file));
    str8_list_push(arena, &parts, str8_array_fixed(second));
    String8 lines = str8_list_join(arena, &parts, 0);
    U32 unit_size = lines.size - 4;
    MemoryCopy(lines.str, &unit_size, 4);
    RDIM_BinarySectionNode section = {0};
    section.v.voff_first = 0x1000;
    section.v.voff_opl = 0x3000;
    D2R_ConvertParams params = {0};
    params.arch = Arch_x64;
    params.path_style = PathStyle_UnixAbsolute;
    params.subset_flags = RDIM_SubsetFlag_All;
    params.deterministic = 1;
    params.binary_sections.first = params.binary_sections.last = &section;
    params.binary_sections.count = 1;
    params.raw.sec[DW_SectionKind_Info].data = version == 5 ? str8_array_fixed(info5) : str8_array_fixed(info);
    params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
    params.raw.sec[DW_SectionKind_Line].data = lines;
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count = 1, .broadcast_memory = &broadcast}));
    Arena *convert_arena = arena_alloc();
    RDIM_BakeParams result = d2r_convert(convert_arena, &params);
    lane_ctx(restore);
    TestCheck(result.units.total_count == 1);
    RDIM_LineTable *table = result.units.first->v[0].line_table;
    test_outf("DWARF %I64u, %S: %I64u sequences, %I64u lines\n", version, cases[case_idx].name, table->seq_count, table->line_count);
    TestCheck(table->seq_count == cases[case_idx].sequences && table->line_count == 2*cases[case_idx].sequences);
    U64 seq_idx = 0;
    for EachNode(n, RDIM_LineSequenceNode, table->first_seq)
    {
      RDIM_LineSequence *seq = &n->v;
      TestCheck(seq->line_count == 2);
      TestCheck(str8_match(seq->src_file->path, str8_lit("/src/main.c"), 0));
      for(U64 idx = 0; idx < 2; idx += 1)
      {
        TestCheck(seq->voffs[idx] == 0x1000*(seq_idx+1) + 4*idx);
        TestCheck(seq->line_nums[idx] == 1 + idx);
      }
      TestCheck(seq->voffs[2] == 0x1000*(seq_idx+1) + 8);
      seq_idx += 1;
    }
    arena_release(convert_arena);
  }
}

SkippedTest(d2r_regressions)
{
  String8 radbin_path = test_build_exe_path(arena, s("radbin"));
  String8 bin_paths[] =
  {
    test_input_path(arena, ctx, s("raddbg_linux_dac63d6b/raddbg")),
  };
  
  // rjf: generate RDIs
  {
    ProcessList processes = {0};
    for EachElement(bin_idx, bin_paths)
    {
      String8 bin_path = bin_paths[bin_idx];
      String8 rdi_path = str8f(arena, "%S/%I64u.rdi", ctx->artifacts_path, bin_idx);
      Process process = launch_cmd_line(str8f(arena, "%S --rdi --deterministic %S --out:%S", radbin_path, bin_path, rdi_path));
      process_list_push(arena, &processes, process);
    }
    for EachNode(n, ProcessNode, processes.first)
    {
      process_join(n->v, max_U64, 0);
    }
  }
  
  // rjf: generate dumps
  {
    ProcessList processes = {0};
    for EachElement(bin_idx, bin_paths)
    {
      String8 rdi_path = str8f(arena, "%S/%I64u.rdi", ctx->artifacts_path, bin_idx);
      String8 dump_path = str8f(arena, "%S/current_%I64u", ctx->artifacts_path, bin_idx);
      Process process = launch_cmd_line(str8f(arena, "%S --dump --deterministic %S --out:%S", radbin_path, rdi_path, dump_path));
      process_list_push(arena, &processes, process);
    }
    for EachNode(n, ProcessNode, processes.first)
    {
      process_join(n->v, max_U64, 0);
    }
  }
  
  // rjf: check against exemplars
  {
    for EachElement(bin_idx, bin_paths)
    {
      Temp scratch = scratch_begin(&arena, 1);
      String8 dump_path_current = str8f(scratch.arena, "%S/current_%I64u", ctx->artifacts_path, bin_idx);
      String8 dump_data_current = data_from_file_path(scratch.arena, dump_path_current);
      String8 dump_path_exemplar = test_exemplar_path(scratch.arena, ctx, str8f(scratch.arena, "exemplar_%I64u", bin_idx));
      String8 dump_data_exemplar = data_from_file_path(scratch.arena, dump_path_exemplar);
      B32 dump_matches = str8_match(dump_data_current, dump_data_exemplar, 0);
      if(!dump_matches)
      {
        String8 diff_cmd = str8f(scratch.arena, "diff %S %S",
                                 path_normalized_from_string(scratch.arena, dump_path_current),
                                 path_normalized_from_string(scratch.arena, dump_path_exemplar));
        test_outf("Current log does not match exemplar; run `%S`\n", diff_cmd);
      }
      TestCheck(dump_matches);
      scratch_end(scratch);
    }
  }
}

Test(d2r_determinism)
{
  U64 num_repeats_per_bin = 16;
  String8 radbin_path = test_build_exe_path(arena, s("radbin"));
  String8 bin_paths[] =
  {
    test_input_path(arena, ctx, s("raddbg_linux_dac63d6b/raddbg")),
  };
  for EachElement(bin_idx, bin_paths)
  {
    // rjf: unpack paths, make output directory
    String8 bin_path = bin_paths[bin_idx];
    
    // rjf: generate all RDIs
    String8List rdi_paths = {0};
    String8List dump_paths = {0};
    {
      ProcessList processes = {0};
      for EachIndex(repeat_idx, num_repeats_per_bin)
      {
        String8 rdi_name = str8f(arena, "repeat_%I64u.rdi", repeat_idx);
        String8 rdi_path = str8f(arena, "%S/%S", ctx->artifacts_path, rdi_name);
        str8_list_push(arena, &rdi_paths, rdi_path);
        String8 cmdl = str8f(arena, "%S --rdi --deterministic %S --out:%S", radbin_path, bin_path, rdi_path);
        Process process = launch_cmd_line(cmdl);
        TestCheck(!process_match(process_zero(), process));
        process_list_push(arena, &processes, process);
      }
      for EachNode(n, ProcessNode, processes.first)
      {
        process_join(n->v, max_U64, 0);
      }
    }
    
    // rjf: generate all dumps
    {
      ProcessList processes = {0};
      for EachNode(n, String8Node, rdi_paths.first)
      {
        String8 rdi_path = n->string;
        String8 dump_path = str8f(arena, "%S.dump", rdi_path);
        str8_list_push(arena, &dump_paths, dump_path);
        Process process_handle = launch_cmd_linef("%S --dump --deterministic %S --out:%S", radbin_path, rdi_path, dump_path);
        TestCheck(!process_match(process_zero(), process_handle));
        process_list_push(arena, &processes, process_handle);
      }
      for EachNode(n, ProcessNode, processes.first)
      {
        process_join(n->v, max_U64, 0);
      }
    }
    
    // rjf: gather all hashes/paths
    U64 rdi_hashes_count = rdi_paths.node_count;
    U128 *rdi_hashes = push_array(arena, U128, rdi_hashes_count);
    String8 *rdi_paths_array = push_array(arena, String8, rdi_hashes_count);
    U64 dump_hashes_count = dump_paths.node_count;
    U128 *dump_hashes = push_array(arena, U128, dump_hashes_count);
    String8 *dump_paths_array = push_array(arena, String8, dump_hashes_count);
    {
      U64 idx = 0;
      for EachNode(n, String8Node, rdi_paths.first)
      {
        Temp scratch = scratch_begin(0, 0);
        String8 rdi_path = n->string;
        String8 path = rdi_path;
        String8 data = data_from_file_path(scratch.arena, path);
        TestCheck(data.size != 0);
        rdi_hashes[idx] = u128_hash_from_str8(data);
        rdi_paths_array[idx] = path;
        scratch_end(scratch);
        idx += 1;
      }
    }
    {
      U64 idx = 0;
      for EachNode(n, String8Node, dump_paths.first)
      {
        Temp scratch = scratch_begin(0, 0);
        String8 path = n->string;
        String8 data = data_from_file_path(scratch.arena, path);
        TestCheck(data.size != 0);
        dump_hashes[idx] = u128_hash_from_str8(data);
        dump_paths_array[idx] = path;
        scratch_end(scratch);
        idx += 1;
      }
    }
    
    // rjf: determine if all hashes match
    U64 mismatch_num = 0;
    for EachIndex(idx, rdi_hashes_count)
    {
      if(!u128_match(rdi_hashes[idx], rdi_hashes[0]))
      {
        mismatch_num = idx+1;
        break;
      }
    }
    for EachIndex(idx, dump_hashes_count)
    {
      if(!u128_match(dump_hashes[idx], dump_hashes[0]))
      {
        mismatch_num = idx+1;
        break;
      }
    }
    
    // rjf: output bad case info
    if(mismatch_num != 0)
    {
      U64 idx = mismatch_num-1;
      test_outf("  bin[%I64u] \"%S\"\n", idx, bin_path);
      test_outf("    rdi[%I64u] 0x%I64x:%I64x \"%S\"\n", idx, rdi_hashes[idx].u64[0], rdi_hashes[idx].u64[1], rdi_paths_array[idx]);
      test_outf("    dump[%I64u] 0x%I64x:%I64x \"%S\"\n", idx, dump_hashes[idx].u64[0], dump_hashes[idx].u64[1], dump_paths_array[idx]);
    }
    TestCheck(mismatch_num == 0);
  }
}
