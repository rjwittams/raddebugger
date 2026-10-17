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
