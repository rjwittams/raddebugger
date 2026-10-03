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
  // Set column (opcode 5) after set_address, before each emitted row.
  // A preceding end_sequence must not emit a row at the next set_address.
  U8 first[] = {0,9,2, 0,0x10,0,0,0,0,0,0, 5,7,1, 3,1, 2,4, 5,19,1, 2,4, 0,1,1};
  U8 second[] = {0,9,2, 0,0x20,0,0,0,0,0,0, 5,7,1, 3,1, 2,4, 5,19,1, 2,4, 0,1,1};
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
    // Each intervening sequence must reset the VM even if it has no source row.
    // set_address 0x1800; advance_line -1; copy; advance_pc 4; end_sequence.
    {str8_lit("line-zero-sequence"),  str8_lit("\0\11\2\0\30\0\0\0\0\0\0\3\177\1\2\4\0\1\1"), 2},
    // set_address 0x1800; advance_pc 4; end_sequence (no copy).
    {str8_lit("empty-sequence"),      str8_lit("\0\11\2\0\30\0\0\0\0\0\0\2\4\0\1\1"), 2},
    // set_address 0; copy; end_sequence, followed by the real second sequence.
    {str8_lit("zero-address-sequence"), str8_lit("\0\11\2\0\0\0\0\0\0\0\0\1\0\1\1"), 2},
    // Linker tombstones invalidate later rows too, including wrapped addresses.
    {str8_lit("tombstone-sequence"), str8_lit("\0\11\2\377\377\377\377\377\377\377\377\1\3\1\2\4\1\2\4\0\1\1"), 2},
  };
  for(U64 version = 4; version <= 5; version += 1) for EachElement(case_idx, cases)
  {
    String8List parts = {0};
    U8 set_file[] = {4, version == 5 ? 0 : 1};
    str8_list_push(arena, &parts, version == 5 ? str8_array_fixed(header5) : str8_array_fixed(header));
    str8_list_push(arena, &parts, str8_array_fixed(set_file));
    str8_list_push(arena, &parts, str8_array_fixed(first));
    str8_list_push(arena, &parts, str8_array_fixed(set_file));
    str8_list_push(arena, &parts, cases[case_idx].data);
    str8_list_push(arena, &parts, str8_array_fixed(set_file));
    str8_list_push(arena, &parts, str8_array_fixed(second));
    String8 lines = str8_list_join(arena, &parts, 0);
    U32 unit_size = lines.size - 4;
    MemoryCopy(lines.str, &unit_size, 4);
    // Preserve the exact input for independent llvm-dwarfdump checks.
    TestCheck(t_write_file(str8f(arena, "v%I64u-%S.line", version, cases[case_idx].name), lines));
    RDIM_BinarySectionNode section = {0};
    section.v.flags = RDI_BinarySectionFlag_Read|RDI_BinarySectionFlag_Execute;
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
    if(table->line_count != 2*cases[case_idx].sequences)
    {
      for EachNode(n, RDIM_LineSequenceNode, table->first_seq)
      {
        for EachIndex(idx, n->v.line_count)
        {
          test_outf("  voff 0x%I64x: line %u\n", n->v.voffs[idx], n->v.line_nums[idx]);
        }
      }
    }
    TestCheck(table->seq_count == cases[case_idx].sequences && table->line_count == 2*cases[case_idx].sequences);
    U64 seq_idx = 0;
    for EachNode(n, RDIM_LineSequenceNode, table->first_seq)
    {
      RDIM_LineSequence *seq = &n->v;
      TestCheck(seq->line_count == 2);
      TestCheck(seq->col_nums[0] == 7 && seq->col_nums[1] == 0);
      TestCheck(seq->col_nums[2] == 19 && seq->col_nums[3] == 0);
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

Test(d2r_line_operation_advance)
{
  U8 abbrev[] = {1,0x11,1, 3,8, 0x1b,8, 0x10,0x17, 0x11,1, 0x12,6, 0,0,
                 2,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0, 0};
  U8 info4[] = {0,0,0,0, 4,0, 0,0,0,0, 8,1,
                'm','a','i','n','.','c',0, '/','s','r','c',0,
                0,0,0,0, 0,0x10,0,0,0,0,0,0, 0,0x20,0,0,
                2,'i','n','t',0,5,4,0};
  U32 info4_size = sizeof(info4)-4;
  MemoryCopy(info4, &info4_size, 4);
  U8 info5[sizeof(info4)+1] = {0,0,0,0, 5,0,1,8, 0,0,0,0};
  MemoryCopy(info5+12, info4+11, sizeof(info4)-11);
  U32 info5_size = sizeof(info5)-4;
  MemoryCopy(info5, &info5_size, 4);
  U8 header4[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                  0,1,1,1,1,0,0,0,1,0,0,1, 0, 'm','a','i','n','.','c',0,0,0,0,0};
  U32 header4_size = sizeof(header4)-10;
  MemoryCopy(header4+6, &header4_size, 4);
  U8 header5[] = {0,0,0,0, 5,0,8,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                  0,1,1,1,1,0,0,0,1,0,0,1,
                  1,1,8,1, '/','s','r','c',0,
                  2,1,8,2,0x0f,1, 'm','a','i','n','.','c',0,0};
  U32 header5_size = sizeof(header5)-12;
  MemoryCopy(header5+8, &header5_size, 4);
  // Four source rows and their end_sequence address, including operation-index carry.
  struct {U8 min_inst, max_ops; U64 offsets[5];} cases[] =
  {
    {1,1, {1,2,3,6,7}},
    {4,1, {4,8,12,24,28}},
    {1,2, {0,1,1,3,4}},
    {4,2, {0,4,4,12,16}},
    {4,3, {0,0,4,8,12}},
  };
  B32 all_rows_match = 1;
  for(U64 version = 4; version <= 5; version += 1)
  for EachElement(case_idx, cases)
  for(U64 standard = 0; standard <= 1; standard += 1)
  {
    header4[10] = header5[12] = cases[case_idx].min_inst;
    header4[11] = header5[13] = cases[case_idx].max_ops;
    String8List parts = {0};
    str8_serial_begin(arena, &parts);
    str8_serial_push_data(arena, &parts, version == 5 ? header5 : header4,
                         version == 5 ? sizeof(header5) : sizeof(header4));
    U8 start[] = {4, version == 5 ? 0 : 1, 0,9,2, 0,0x10,0,0,0,0,0,0};
    str8_serial_push_data(arena, &parts, start, sizeof(start));
    U8 advances[] = {1,1,1,3};
    for EachElement(idx, advances)
    {
      // Equivalent line increments encoded with standard or special opcodes.
      U8 row[] = {2,advances[idx],3,1,1};
      U8 special = 13 + 14*advances[idx] + 6;
      str8_serial_push_data(arena, &parts, standard ? row : &special, standard ? sizeof(row) : 1);
    }
    U8 end[] = {2,cases[case_idx].max_ops,0,1,1};
    str8_serial_push_data(arena, &parts, end, sizeof(end));
    String8 lines = str8_serial_end(arena, &parts);
    U32 line_size = lines.size-4;
    MemoryCopy(lines.str, &line_size, 4);
    String8 name = push_str8f(arena, "ops-v%I64u-min%u-max%u-%s", version,
                            cases[case_idx].min_inst, cases[case_idx].max_ops, standard ? "standard" : "special");
    TestCheck(t_write_file(push_str8f(arena, "%S.info", name), version == 5 ? str8_array_fixed(info5) : str8_array_fixed(info4)));
    TestCheck(t_write_file(push_str8f(arena, "%S.abbrev", name), str8_array_fixed(abbrev)));
    TestCheck(t_write_file(push_str8f(arena, "%S.line", name), lines));
    RDIM_BinarySectionNode section = {0};
    section.v.flags = RDI_BinarySectionFlag_Read|RDI_BinarySectionFlag_Execute;
    section.v.voff_first = 0x1000;
    section.v.voff_opl = 0x3000;
    D2R_ConvertParams params = {0};
    params.arch = Arch_x64;
    params.path_style = PathStyle_UnixAbsolute;
    params.subset_flags = RDIM_SubsetFlag_All;
    params.deterministic = 1;
    params.binary_sections.first = params.binary_sections.last = &section;
    params.binary_sections.count = 1;
    params.raw.sec[DW_SectionKind_Info].data = version == 5 ? str8_array_fixed(info5) : str8_array_fixed(info4);
    params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
    params.raw.sec[DW_SectionKind_Line].data = lines;
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count = 1, .broadcast_memory = &broadcast}));
    Arena *convert_arena = arena_alloc();
    RDIM_BakeParams result = d2r_convert(convert_arena, &params);
    TestCheck(result.units.total_count == 1);
    RDIM_LineTable *table = result.units.first->v[0].line_table;
    TestCheck(table->seq_count == 1 && table->line_count == 4);
    RDIM_LineSequence *seq = &table->first_seq->v;
    for(U64 idx = 0; idx <= 4; idx += 1)
    {
      test_outf("%S row %I64u: 0x%I64x, expected 0x%I64x\n", name, idx, seq->voffs[idx], 0x1000+cases[case_idx].offsets[idx]);
      all_rows_match &= seq->voffs[idx] == 0x1000+cases[case_idx].offsets[idx];
      if(idx < 4) {TestCheck(seq->line_nums[idx] == idx+2);}
    }
    RDIM_BakeResults baked = rdim_bake(convert_arena, &result);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(convert_arena, &baked.section_bundle);
    String8 data = str8_list_join(convert_arena, &blobs, 0);
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    RDI_ParsedLineTable parsed = {0};
    U64 last_row_voff = 0x1000+cases[case_idx].offsets[3];
    TestCheck(t_write_file(push_str8f(arena, "%S.rdi", name), data));
    rdi_parsed_from_line_table(&rdi, rdi_line_table_from_unit(&rdi, rdi_unit_from_voff(&rdi, last_row_voff)), &parsed);
    for(U64 voff = last_row_voff; voff < 0x1000+cases[case_idx].offsets[4]; voff += 1)
    {
      U64 row = rdi_line_info_idx_from_voff(&parsed, voff);
      all_rows_match &= row < parsed.count && parsed.voffs[row] == last_row_voff &&
                        parsed.lines[row].line_num == 5 && parsed.lines[row].file_idx != 0;
    }
    arena_release(convert_arena);
  }
  TestCheck(all_rows_match);
}

Test(d2r_line_tombstones)
{
  String8 case_names[] = {str8_lit("control"), str8_lit("tombstone"),
                         str8_lit("zero-dead"), str8_lit("addend-dead"), str8_lit("zero-live"),
                         str8_lit("delayed-first-row"), str8_lit("dead-crossing-code"), str8_lit("dead-file-change"),
                         str8_lit("zero-no-code-metadata")};
  B32 all_cases_match = 1;
  U8 abbrev[] = {1,0x11,1, 3,8, 0x1b,8, 0x10,0x17, 0x11,1, 0x12,6, 0,0,
                 2,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0, 0};
  U8 header4[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                  0,1,1,1,1,0,0,0,1,0,0,1, 0,
                  'm','a','i','n','.','c',0, 0,0,0,
                  'o','t','h','e','r','.','c',0, 0,0,0, 0};
  U32 header4_size = sizeof(header4)-10;
  MemoryCopy(header4+6, &header4_size, 4);
  U8 header5[] = {0,0,0,0, 5,0,8,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                  0,1,1,1,1,0,0,0,1,0,0,1,
                  1,1,8,1, '/','s','r','c',0,
                  2,1,8,2,0x0f,2, 'm','a','i','n','.','c',0,0,
                  'o','t','h','e','r','.','c',0,0};
  U32 header5_size = sizeof(header5)-12;
  MemoryCopy(header5+8, &header5_size, 4);
  for(U64 version = 4; version <= 5; version += 1)
  for(U64 addr_size = 4; addr_size <= 8; addr_size += 4)
  for EachElement(case_idx, case_names)
  {
    U8 unit_header4[] = {0,0,0,0, 4,0, 0,0,0,0, (U8)addr_size};
    U8 unit_header5[] = {0,0,0,0, 5,0,1,(U8)addr_size, 0,0,0,0};
    U64 low_pc = 0x1000;
    String8List unit_parts = {0};
    str8_serial_begin(arena, &unit_parts);
    str8_serial_push_data(arena, &unit_parts,
                         version == 5 ? unit_header5 : unit_header4,
                         version == 5 ? sizeof(unit_header5) : sizeof(unit_header4));
    str8_serial_push_u8(arena, &unit_parts, 1);
    str8_serial_push_cstr(arena, &unit_parts, str8_lit("main.c"));
    str8_serial_push_cstr(arena, &unit_parts, str8_lit("/src"));
    str8_serial_push_u32(arena, &unit_parts, 0); // DW_AT_stmt_list
    str8_serial_push_data(arena, &unit_parts, &low_pc, addr_size);
    str8_serial_push_u32(arena, &unit_parts, 0x1008); // DW_AT_high_pc extent
    U8 type[] = {2,'i','n','t',0,5,4,0};
    str8_serial_push_data(arena, &unit_parts, type, sizeof(type));
    String8 info = str8_serial_end(arena, &unit_parts);
    U32 info_size = info.size-4;
    MemoryCopy(info.str, &info_size, 4);

    header5[6] = (U8)addr_size;
    String8List line_parts = {0};
    str8_serial_begin(arena, &line_parts);
    str8_serial_push_data(arena, &line_parts,
                         version == 5 ? header5 : header4,
                         version == 5 ? sizeof(header5) : sizeof(header4));
    U64 middle = case_idx == 1 ? (addr_size == 4 ? max_U32 : max_U64) : case_idx == 0 || case_idx == 5 ? 0x1800 : case_idx == 3 || case_idx == 7 ? 4 : 0;
    B32 discarded = (case_idx >= 1 && case_idx <= 3) || case_idx == 6 || case_idx == 7;
    U64 addresses[] = {0x1000, middle, 0x2000};
    for EachElement(idx, addresses)
    {
      // set_file; set_address; copy; advance_line; advance_pc; copy;
      // advance_pc; end_sequence. A tombstone's second row advances/wraps.
      U8 set_address[] = {4, version == 5 ? 0 : 1, 0,(U8)(addr_size+1),2};
      U8 rows[] = {1,3,1,2,4,1,2,4,0,1,1};
      str8_serial_push_data(arena, &line_parts, set_address, sizeof(set_address));
      U64 address = case_idx == 5 && idx == 1 ? 0 : addresses[idx];
      str8_serial_push_data(arena, &line_parts, &address, addr_size);
      if(idx == 1 && case_idx == 5)
      {
        // set_address is not the first row: advance to real code before copy.
        U8 advance[] = {2,0x80,0x30}; // advance_pc 0x1800
        str8_serial_push_data(arena, &line_parts, advance, sizeof(advance));
      }
      if(idx == 1 && (case_idx == 6 || case_idx == 7))
      {
        // A rejected sequence stays rejected after reaching code or changing file.
        U8 crossing[] = {1,3,1,2,0x80,0x20,1,2,4,0,1,1};
        U8 file_change[] = {1,4,version == 5 ? 1 : 2,3,1,2,0x80,0x20,1,2,4,0,1,1};
        str8_serial_push_data(arena, &line_parts, case_idx == 7 ? file_change : crossing,
                             case_idx == 7 ? sizeof(file_change) : sizeof(crossing));
      }
      else
      {
        str8_serial_push_data(arena, &line_parts, rows, sizeof(rows));
      }
    }
    String8 lines = str8_serial_end(arena, &line_parts);
    U32 line_size = lines.size-4;
    MemoryCopy(lines.str, &line_size, 4);
    String8 name = str8f(arena, "v%I64u-addr%I64u-%S", version, addr_size, case_names[case_idx]);
    TestCheck(t_write_file(str8f(arena, "%S.info", name), info));
    TestCheck(t_write_file(str8f(arena, "%S.abbrev", name), str8_array_fixed(abbrev)));
    TestCheck(t_write_file(str8f(arena, "%S.line", name), lines));
    RDIM_BinarySectionNode section = {0};
    section.v.flags = RDI_BinarySectionFlag_Read;
    if(case_idx != 8) {section.v.flags |= RDI_BinarySectionFlag_Execute;}
    section.v.voff_first = case_idx == 4 ? 0 : 0x1000;
    section.v.voff_opl = 0x3000;
    D2R_ConvertParams params = {0};
    params.arch = addr_size == 4 ? Arch_x86 : Arch_x64;
    params.path_style = PathStyle_UnixAbsolute;
    params.subset_flags = RDIM_SubsetFlag_All;
    params.deterministic = 1;
    params.binary_sections.first = params.binary_sections.last = &section;
    params.binary_sections.count = 1;
    params.raw.sec[DW_SectionKind_Info].data = info;
    params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
    params.raw.sec[DW_SectionKind_Line].data = lines;
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count=1, .broadcast_memory=&broadcast}));
    Arena *convert_arena = arena_alloc();
    RDIM_BakeParams result = d2r_convert(convert_arena, &params);
    TestCheck(result.units.total_count == 1);
    RDIM_LineTable *table = result.units.first->v[0].line_table;
    test_outf("%S: %I64u sequences, %I64u lines\n", name, table->seq_count, table->line_count);
    all_cases_match &= table->seq_count == (discarded ? 2 : 3) && table->line_count == (discarded ? 4 : 6);
    RDIM_BakeResults baked = rdim_bake(convert_arena, &result);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(convert_arena, &baked.section_bundle);
    String8 data = str8_list_join(convert_arena, &blobs, 0);
    TestCheck(t_write_file(str8f(arena, "%S.rdi", name), data));
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    RDI_ParsedLineTable parsed = {0};
    rdi_parsed_from_line_table(&rdi, rdi_line_table_from_unit(&rdi, rdi_unit_from_voff(&rdi, 0x1000)), &parsed);
    U64 source_rows = 0;
    for EachIndex(idx, parsed.count)
    {
      if(parsed.lines[idx].file_idx != 0)
      {
        U64 voff = parsed.voffs[idx];
        all_cases_match &= voff == 0x1000 || voff == 0x1004 || voff == 0x2000 || voff == 0x2004 ||
                           (!discarded && (voff == middle || voff == middle+4));
        all_cases_match &= parsed.lines[idx].line_num == (voff%8 == 0 ? 1 : 2);
        source_rows += 1;
      }
    }
    all_cases_match &= source_rows == (discarded ? 4 : 6);
    arena_release(convert_arena);
  }
  TestCheck(all_cases_match);
}

Test(d2r_line_unresolved_file)
{
  U8 abbrev[] = {1,0x11,1, 3,8, 0x1b,8, 0x10,0x17, 0x11,1, 0x12,6, 0,0,
                 2,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0, 0};
  U8 info[] = {0,0,0,0, 4,0, 0,0,0,0, 8, 1,
               'm','a','i','n','.','c',0, '/','s','r','c',0,
               0,0,0,0, 0,0x10,0,0,0,0,0,0, 0,0x20,0,0,
               2,'i','n','t',0,5,4, 0};
  U32 info_size = sizeof(info)-4;
  MemoryCopy(info, &info_size, 4);
  U8 info5[sizeof(info)+1] = {0,0,0,0, 5,0,1,8, 0,0,0,0};
  MemoryCopy(info5+12, info+11, sizeof(info)-11);
  U32 info5_size = sizeof(info5)-4;
  MemoryCopy(info5, &info5_size, 4);
  // v4 files are 1-based; v5 files are 0-based. Both tables name two files.
  U8 header[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                 0,1,1,1,1,0,0,0,1,0,0,1, 0,
                 'm','a','i','n','.','c',0,0,0,0,
                 'o','t','h','e','r','.','c',0,0,0,0, 0};
  U32 header_size = sizeof(header)-10;
  MemoryCopy(header+6, &header_size, 4);
  U8 header5[] = {0,0,0,0, 5,0,8,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                  0,1,1,1,1,0,0,0,1,0,0,1,
                  1,1,8,1, '/','s','r','c',0,
                  2,1,8,2,0x0f,2, 'm','a','i','n','.','c',0,0,
                  'o','t','h','e','r','.','c',0,0};
  U32 header5_size = sizeof(header5)-12;
  MemoryCopy(header5+8, &header5_size, 4);
  struct {U8 files[3]; U8 line_step;} cases[] =
  {
    {{0,9,1}, 1}, // unresolved middle
    {{9,0,1}, 1}, // unresolved prefix
    {{0,1,9}, 1}, // unresolved suffix
    {{9,9,9}, 1}, // no resolved file
    {{0,9,1}, 0}, // file changes without line-number changes
  };
  String8 paths[] = {str8_lit("/src/main.c"), str8_lit("/src/other.c")};
  for(U64 version = 4; version <= 5; version += 1) for EachElement(case_idx, cases)
  {
    // Three four-byte ranges beginning at 0x1000. Logical file 9 deliberately
    // refers beyond the file table; logical files 0/1 name main.c/other.c.
    U8 files[3];
    for EachElement(idx, files) {files[idx] = cases[case_idx].files[idx] + (version == 4 && cases[case_idx].files[idx] < 2);}
    U8 program[] = {0,9,2, 0,0x10,0,0,0,0,0,0,
                    4,files[0], 1,
                    2,4, 3,cases[case_idx].line_step, 4,files[1], 1,
                    2,4, 3,cases[case_idx].line_step, 4,files[2], 1,
                    2,4, 0,1,1};
    String8List parts = {0};
    str8_list_push(arena, &parts, version == 5 ? str8_array_fixed(header5) : str8_array_fixed(header));
    str8_list_push(arena, &parts, str8_array_fixed(program));
    String8 lines = str8_list_join(arena, &parts, 0);
    U32 line_size = lines.size-4;
    MemoryCopy(lines.str, &line_size, 4);
    TestCheck(t_write_file(str8f(arena, "v%I64u-case%I64u.line", version, case_idx), lines));
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
    for EachNode(n, RDIM_LineSequenceNode, table->first_seq)
    {
      test_outf("v%I64u case %I64u: [%I64x,%I64x) %S, %I64u rows\n", version, case_idx,
                n->v.voffs[0], n->v.voffs[n->v.line_count],
                n->v.src_file ? n->v.src_file->path : str8_lit("<no file>"), n->v.line_count);
    }
    TestCheck(table->seq_count == (case_idx == 3 ? 1 : 3) && table->line_count == 3);
    U64 idx = 0;
    for EachNode(n, RDIM_LineSequenceNode, table->first_seq)
    {
      for EachIndex(row, n->v.line_count)
      {
        TestCheck(idx < 3);
        TestCheck(n->v.voffs[row] == 0x1000+4*idx && n->v.voffs[row+1] == 0x1004+4*idx);
        TestCheck(n->v.line_nums[row] == 1+idx*cases[case_idx].line_step);
        U8 file = cases[case_idx].files[idx];
        TestCheck(file == 9 ? n->v.src_file == 0 :
                  n->v.src_file != 0 && str8_match(n->v.src_file->path, paths[file], 0));
        idx += 1;
      }
    }
    restore = lane_ctx(((LaneCtx){.lane_count = 1, .broadcast_memory = &broadcast}));
    RDIM_BakeResults baked = rdim_bake(convert_arena, &result);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(convert_arena, &baked.section_bundle);
    String8 data = str8_list_join(convert_arena, &blobs, 0);
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    RDI_ParsedLineTable parsed = {0};
    rdi_parsed_from_line_table(&rdi, rdi_line_table_from_unit(&rdi, rdi_unit_from_voff(&rdi, 0x1000)), &parsed);
    for(U64 voff = 0x1000; voff < 0x100c; voff += 1)
    {
      U64 row = rdi_line_info_idx_from_voff(&parsed, voff);
      TestCheck(row < parsed.count);
      RDI_Line *line = &parsed.lines[row];
      U64 input_row = (voff-0x1000)/4;
      U8 file = cases[case_idx].files[input_row];
      // With no resolved file, lookup may select the preceding sequence's
      // terminator at the same address. Neither row may name a source file.
      if(file == 9) {TestCheck(line->file_idx == 0);}
      else
      {
        TestCheck(line->line_num == 1+input_row*cases[case_idx].line_step);
        RDI_SourceFile *expected = rdi_source_file_from_normal_path(&rdi, paths[file].str, paths[file].size);
        TestCheck(line->file_idx != 0 && rdi_source_file_from_line(&rdi, line) == expected);
      }
    }
    arena_release(convert_arena);
  }
}

Test(d2r_inline_sequence_end)
{
  // A row at end_sequence has no address range. It must not cover the gap
  // before the next sequence when inline caller/body tables are constructed.
  U8 abbrev[] = {1,0x11,1, 3,8, 0x1b,8, 0x10,0x17, 0x11,1, 0x12,6, 0,0,
                 2,0x2e,1, 3,8, 0x11,1, 0x12,6, 0,0,
                 3,0x1d,0, 3,8, 0x11,1, 0x12,6, 0x58,0x0b, 0x59,0x0b, 0,0, 0};
  U8 info[] = {0,0,0,0, 4,0, 0,0,0,0, 8, 1,
               'm','a','i','n','.','c',0, '/','s','r','c',0,
               0,0,0,0, 0,0x10,0,0,0,0,0,0, 0,0x20,0,0,
               2,'f',0, 0,0x10,0,0,0,0,0,0, 8,0,0,0,
               3,'g',0, 0,0x10,0,0,0,0,0,0, 4,0,0,0, 1,40, 0,0};
  U32 info_size = sizeof(info)-4;
  MemoryCopy(info, &info_size, 4);
  U8 header[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                 0,1,1,1,1,0,0,0,1,0,0,1, 0,
                 'm','a','i','n','.','c',0, 0,0,0, 0};
  U32 header_size = sizeof(header)-10;
  MemoryCopy(header+6, &header_size, 4);
  U8 first[] = {0,9,2, 0,0x10,0,0,0,0,0,0, 1, 3,1, 2,4, 1, 3,1, 2,4, 1, 0,1,1};
  U8 second[] = {0,9,2, 0,0x20,0,0,0,0,0,0, 1, 3,1, 2,4, 1, 2,4, 0,1,1};
  String8List parts = {0};
  str8_list_push(arena, &parts, str8_array_fixed(header));
  str8_list_push(arena, &parts, str8_array_fixed(first));
  str8_list_push(arena, &parts, str8_array_fixed(second));
  String8 lines = str8_list_join(arena, &parts, 0);
  U32 line_size = lines.size-4;
  MemoryCopy(lines.str, &line_size, 4);
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
  params.raw.sec[DW_SectionKind_Info].data = str8_array_fixed(info);
  params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
  params.raw.sec[DW_SectionKind_Line].data = lines;
  U64 broadcast = 0;
  LaneCtx restore = lane_ctx(((LaneCtx){.lane_count = 1, .broadcast_memory = &broadcast}));
  Arena *convert_arena = arena_alloc();
  RDIM_BakeParams result = d2r_convert(convert_arena, &params);
  lane_ctx(restore);
  TestCheck(result.units.total_count == 1);
  RDIM_Unit *unit = &result.units.first->v[0];
  TestCheck(unit->inline_sites.total_count == 1);
  RDIM_LineTable *table = unit->line_table;
  U64 seq_idx = 0;
  for EachNode(n, RDIM_LineSequenceNode, table->first_seq)
  {
    RDIM_LineSequence *seq = &n->v;
    test_outf("sequence %I64u: %I64u rows, range [%I64x, %I64x)\n",
              seq_idx, seq->line_count, seq->voffs[0], seq->voffs[seq->line_count]);
    TestCheck(seq->voffs[seq->line_count] == (seq_idx == 0 ? 0x1008 : 0x2008));
    TestCheck(seq->line_count == 2);
    TestCheck(seq->voffs[0] == (seq_idx == 0 ? 0x1000 : 0x2000));
    TestCheck(seq->voffs[1] == (seq_idx == 0 ? 0x1004 : 0x2004));
    TestCheck(seq->line_nums[0] == (seq_idx == 0 ? 40 : 1));
    TestCheck(seq->line_nums[1] == 2);
    seq_idx += 1;
  }
  TestCheck(table->seq_count == 2);
  RDIM_LineTable *inline_table = unit->inline_sites.first->v[0].line_table;
  TestCheck(inline_table->seq_count == 1 && inline_table->line_count == 1);
  RDIM_LineSequence *body = &inline_table->first_seq->v;
  TestCheck(body->voffs[0] == 0x1000 && body->voffs[1] == 0x1004 && body->line_nums[0] == 1);
  arena_release(convert_arena);
}

Test(d2r_source_directories)
{
  U8 abbrev[] = {1,0x11,1, 3,8, 0x1b,8, 0x10,0x17, 0x11,1, 0x12,6, 0,0,
                 2,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0, 0};
  U8 info[] = {0,0,0,0, 5,0,1,8, 0,0,0,0, 1,
               'm','a','i','n','.','c',0, '/','s','r','c',0,
               0,0,0,0, 0,0x10,0,0,0,0,0,0, 8,0,0,0,
               2,'i','n','t',0,5,4,0};
  U32 info_size = sizeof(info)-4;
  MemoryCopy(info, &info_size, 4);
  U8 info4[sizeof(info)-1] = {0,0,0,0, 4,0, 0,0,0,0, 8};
  MemoryCopy(info4+11, info+12, sizeof(info)-12);
  U32 info4_size = sizeof(info4)-4;
  MemoryCopy(info4, &info4_size, 4);
  U8 header[] = {0,0,0,0, 5,0,8,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                 0,1,1,1,1,0,0,0,1,0,0,1};
  U8 header4[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                  0,1,1,1,1,0,0,0,1,0,0,1};
  struct {String8 name, file, expected; U64 dirs, dir_idx;} cases[] =
  {
    {str8_lit("control"), str8_lit("main.c"), str8_lit("/src/main.c"), 1,0},
    {str8_lit("empty-directories"), str8_lit("main.c"), str8_lit("main.c"), 0,0},
    {str8_lit("one-past-end"), str8_lit("main.c"), str8_lit("main.c"), 1,1},
    {str8_lit("large-index"), str8_lit("main.c"), str8_lit("main.c"), 1,max_U64},
    {str8_lit("relative-directory"), str8_lit("main.c"), str8_lit("/src/inc/main.c"), 2,1},
    {str8_lit("absolute-invalid-index"), str8_lit("/else/main.c"), str8_lit("/else/main.c"), 1,max_U64},
    {str8_lit("absolute-empty-directories"), str8_lit("/src/main.c"), str8_lit("/src/main.c"), 0,0},
  };
  B32 all_paths_match = 1;
  for(U64 version = 4; version <= 5; version += 1)
  for EachElement(case_idx, cases)
  {
    // DWARF 4 always has an implicit compilation-directory entry at index zero.
    if(version == 4 && cases[case_idx].dirs == 0) {continue;}
    String8 name = push_str8f(arena, "v%I64u-%S", version, cases[case_idx].name);
    String8List parts = {0};
    str8_serial_begin(arena, &parts);
    str8_serial_push_data(arena, &parts, version == 5 ? header : header4, version == 5 ? sizeof(header) : sizeof(header4));
    if(version == 5)
    {
      U8 dirs[] = {1,1,8,(U8)cases[case_idx].dirs}; // path/string format; directory count
      str8_serial_push_data(arena, &parts, dirs, sizeof(dirs));
      if(cases[case_idx].dirs != 0) {str8_serial_push_cstr(arena, &parts, str8_lit("/src"));}
    }
    if(cases[case_idx].dirs == 2) {str8_serial_push_cstr(arena, &parts, str8_lit("inc"));}
    B32 has_index = case_idx >= 2 && case_idx <= 5;
    if(version == 5)
    {
      if(has_index)
      {
        U8 files[] = {2,1,8,2,0x0f,2}; // path/string and directory-index/ULEB formats
        str8_serial_push_data(arena, &parts, files, sizeof(files));
      }
      else
      {
        U8 files[] = {1,1,8,2}; // no directory-index field
        str8_serial_push_data(arena, &parts, files, sizeof(files));
      }
    }
    else {str8_serial_push_u8(arena, &parts, 0);}
    String8 files[] = {cases[case_idx].file, str8_lit("/src/known.c")};
    for EachElement(file_idx, files)
    {
      str8_serial_push_cstr(arena, &parts, files[file_idx]);
      if(version == 4 || has_index)
      {
        if(cases[case_idx].dir_idx == max_U64)
        {
          U8 index[] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,1};
          str8_serial_push_data(arena, &parts, index, sizeof(index));
        }
        else {str8_serial_push_u8(arena, &parts, (U8)cases[case_idx].dir_idx);}
      }
      if(version == 4) {str8_serial_push_u8(arena, &parts, 0); str8_serial_push_u8(arena, &parts, 0);}
    }
    if(version == 4) {str8_serial_push_u8(arena, &parts, 0);}
    U32 header_size = parts.total_size-(version == 5 ? 12 : 10);
    U8 program[] = {0,9,2, 0,0x10,0,0,0,0,0,0, 4,version == 5 ? 0 : 1,1,
                    2,4,4,version == 5 ? 1 : 2,3,1,1, 2,4,0,1,1};
    str8_serial_push_data(arena, &parts, program, sizeof(program));
    String8 lines = str8_serial_end(arena, &parts);
    U32 line_size = lines.size-4;
    MemoryCopy(lines.str, &line_size, 4);
    MemoryCopy(lines.str+(version == 5 ? 8 : 6), &header_size, 4);
    String8 unit_info = version == 5 ? str8_array_fixed(info) : str8_array_fixed(info4);
    TestCheck(t_write_file(push_str8f(arena, "%S.info", name), unit_info));
    TestCheck(t_write_file(push_str8f(arena, "%S.abbrev", name), str8_array_fixed(abbrev)));
    TestCheck(t_write_file(push_str8f(arena, "%S.line", name), lines));
    RDIM_BinarySectionNode section = {0};
    section.v.flags = RDI_BinarySectionFlag_Read|RDI_BinarySectionFlag_Execute;
    section.v.voff_first = 0x1000;
    section.v.voff_opl = 0x1008;
    D2R_ConvertParams params = {0};
    params.arch = Arch_x64;
    params.path_style = PathStyle_UnixAbsolute;
    params.subset_flags = RDIM_SubsetFlag_All;
    params.deterministic = 1;
    params.binary_sections.first = params.binary_sections.last = &section;
    params.binary_sections.count = 1;
    params.raw.sec[DW_SectionKind_Info].data = unit_info;
    params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
    params.raw.sec[DW_SectionKind_Line].data = lines;
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count = 1, .broadcast_memory = &broadcast}));
    Arena *convert_arena = arena_alloc();
    RDIM_BakeParams result = d2r_convert(convert_arena, &params);
    TestCheck(result.units.total_count == 1 && result.src_files.total_count == 2);
    RDIM_LineTable *table = result.units.first->v[0].line_table;
    TestCheck(table->seq_count == 2 && table->line_count == 2);
    String8 paths[] = {cases[case_idx].expected, str8_lit("/src/known.c")};
    U64 seq_idx = 0;
    for EachNode(n, RDIM_LineSequenceNode, table->first_seq)
    {
      TestCheck(seq_idx < 2 && n->v.line_count == 1 && n->v.src_file != 0);
      test_outf("%S file %I64u: %S, expected %S\n", name, seq_idx, n->v.src_file->path, paths[seq_idx]);
      all_paths_match &= str8_match(n->v.src_file->path, paths[seq_idx], 0);
      TestCheck(n->v.voffs[0] == 0x1000+4*seq_idx && n->v.voffs[1] == 0x1004+4*seq_idx);
      TestCheck(n->v.line_nums[0] == 1+seq_idx);
      seq_idx += 1;
    }
    RDIM_BakeResults baked = rdim_bake(convert_arena, &result);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(convert_arena, &baked.section_bundle);
    String8 data = str8_list_join(convert_arena, &blobs, 0);
    TestCheck(t_write_file(push_str8f(arena, "%S.rdi", name), data));
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    RDI_ParsedLineTable parsed = {0};
    rdi_parsed_from_line_table(&rdi, rdi_line_table_from_unit(&rdi, rdi_unit_from_voff(&rdi, 0x1000)), &parsed);
    for(U64 voff = 0x1000; voff < 0x1008; voff += 1)
    {
      U64 row = rdi_line_info_idx_from_voff(&parsed, voff);
      U64 file = (voff-0x1000)/4;
      TestCheck(row < parsed.count && parsed.lines[row].line_num == 1+file);
      RDI_SourceFile *expected = rdi_source_file_from_normal_path(&rdi, paths[file].str, paths[file].size);
      all_paths_match &= parsed.lines[row].file_idx != 0 && rdi_source_file_from_line(&rdi, &parsed.lines[row]) == expected;
    }
    arena_release(convert_arena);
  }
  TestCheck(all_paths_match);
}

Test(d2r_source_paths)
{
  U8 abbrev[] = {1,0x11,1, 3,8, 0x1b,8, 0x10,0x17, 0x11,1, 0x12,6, 0,0,
                 2,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0, 0};
  U8 unit4[] = {0,0,0,0, 4,0, 0,0,0,0, 8};
  U8 unit5[] = {0,0,0,0, 5,0, 1,8, 0,0,0,0};
  U8 line4[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                0,1,1,1,1,0,0,0,1,0,0,1, 0};
  U8 line5[] = {0,0,0,0, 5,0,8,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                0,1,1,1,1,0,0,0,1,0,0,1,
                1,1,8,1, '/','s','r','c',0, 2,1,8,2,0x0f,1};
  U8 type[] = {2,'i','n','t',0,5,4,0};
  struct {String8 name, paths[2], canonical; U64 files;} cases[] =
  {
    {str8_lit("control"), {str8_lit("/src/shared.h"), str8_lit("/src/shared.h")}, str8_lit("/src/shared.h"), 1},
    {str8_lit("absolute-dots"), {str8_lit("/src/part/../shared.h"), str8_lit("/src/shared.h")}, str8_lit("/src/shared.h"), 1},
    {str8_lit("relative-dots"), {str8_lit("part/../shared.h"), str8_lit("/src/shared.h")}, str8_lit("/src/shared.h"), 1},
    {str8_lit("repeated-separators"), {str8_lit("/src//./shared.h"), str8_lit("/src/shared.h")}, str8_lit("/src/shared.h"), 1},
    {str8_lit("windows-dots"), {str8_lit("C:\\src\\part\\..\\shared.h"), str8_lit("C:/src/shared.h")}, str8_lit("c:/src/shared.h"), 1},
    {str8_lit("case-distinct"), {str8_lit("/src/Shared.h"), str8_lit("/src/shared.h")}, str8_lit("/src/shared.h"), 2},
    {str8_lit("case-reversed"), {str8_lit("/src/shared.h"), str8_lit("/src/Shared.h")}, str8_lit("/src/shared.h"), 2},
    {str8_lit("case-directory"), {str8_lit("/src/Dir/shared.h"), str8_lit("/src/dir/shared.h")}, str8_lit("/src/dir/shared.h"), 2},
    {str8_lit("case-windows"), {str8_lit("C:/src/Shared.h"), str8_lit("C:/src/shared.h")}, str8_lit("c:/src/shared.h"), 2},
  };
  B32 all_paths_match = 1;
  for(U64 version = 4; version <= 5; version += 1) for EachElement(case_idx, cases)
  {
    String8List info_parts = {0}, line_parts = {0};
    for EachElement(unit_idx, cases[case_idx].paths)
    {
      U32 stmt_list = line_parts.total_size;
      String8List parts = {0};
      str8_serial_begin(arena, &parts);
      str8_serial_push_data(arena, &parts, version == 4 ? line4 : line5, version == 4 ? sizeof(line4) : sizeof(line5));
      str8_serial_push_cstr(arena, &parts, cases[case_idx].paths[unit_idx]);
      U8 file_tail4[] = {0,0,0,0};
      U8 file_tail5[] = {0};
      str8_serial_push_data(arena, &parts, version == 4 ? file_tail4 : file_tail5, version == 4 ? sizeof(file_tail4) : sizeof(file_tail5));
      U32 header_size = parts.total_size - (version == 4 ? 10 : 12);
      U64 address = 0x1000+0x1000*unit_idx;
      U8 set_address[] = {4,version == 4 ? 1 : 0,0,9,2};
      U8 rows[] = {3,6,1,2,4,0,1,1}; // line 7; copy; advance_pc 4; end_sequence
      str8_serial_push_data(arena, &parts, set_address, sizeof(set_address));
      str8_serial_push_u64(arena, &parts, address);
      str8_serial_push_data(arena, &parts, rows, sizeof(rows));
      String8 line = str8_serial_end(arena, &parts);
      U32 line_size = line.size-4;
      MemoryCopy(line.str, &line_size, 4);
      MemoryCopy(line.str+(version == 4 ? 6 : 8), &header_size, 4);
      str8_list_push(arena, &line_parts, line);

      MemoryZeroStruct(&parts);
      str8_serial_begin(arena, &parts);
      str8_serial_push_data(arena, &parts, version == 4 ? unit4 : unit5, version == 4 ? sizeof(unit4) : sizeof(unit5));
      str8_serial_push_u8(arena, &parts, 1);
      str8_serial_push_cstr(arena, &parts, unit_idx == 0 ? str8_lit("a.c") : str8_lit("b.c"));
      str8_serial_push_cstr(arena, &parts, str8_lit("/src"));
      str8_serial_push_u32(arena, &parts, stmt_list);
      str8_serial_push_u64(arena, &parts, address);
      str8_serial_push_u32(arena, &parts, 4);
      str8_serial_push_data(arena, &parts, type, sizeof(type));
      String8 unit = str8_serial_end(arena, &parts);
      U32 unit_size = unit.size-4;
      MemoryCopy(unit.str, &unit_size, 4);
      str8_list_push(arena, &info_parts, unit);
    }
    String8 info = str8_list_join(arena, &info_parts, 0);
    String8 lines = str8_list_join(arena, &line_parts, 0);
    String8 name = str8f(arena, "v%I64u-%S", version, cases[case_idx].name);
    TestCheck(t_write_file(str8f(arena, "%S.info", name), info));
    TestCheck(t_write_file(str8f(arena, "%S.abbrev", name), str8_array_fixed(abbrev)));
    TestCheck(t_write_file(str8f(arena, "%S.line", name), lines));
    RDIM_BinarySectionNode section = {0};
    section.v.flags = RDI_BinarySectionFlag_Read|RDI_BinarySectionFlag_Execute;
    section.v.voff_first = 0x1000;
    section.v.voff_opl = 0x2004;
    D2R_ConvertParams params = {0};
    params.arch = Arch_x64;
    params.path_style = PathStyle_UnixAbsolute;
    params.subset_flags = RDIM_SubsetFlag_All;
    params.deterministic = 1;
    params.binary_sections = (RDIM_BinarySectionList){.first=&section, .last=&section, .count=1};
    params.raw.sec[DW_SectionKind_Info].data = info;
    params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
    params.raw.sec[DW_SectionKind_Line].data = lines;
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count=1, .broadcast_memory=&broadcast}));
    Arena *convert_arena = arena_alloc();
    RDIM_BakeParams result = d2r_convert(convert_arena, &params);
    TestCheck(result.units.total_count == 2);
    test_outf("%S: %I64u source files\n", name, result.src_files.total_count);
    all_paths_match &= result.src_files.total_count == cases[case_idx].files;
    RDIM_BakeResults baked = rdim_bake(convert_arena, &result);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(convert_arena, &baked.section_bundle);
    String8 data = str8_list_join(convert_arena, &blobs, 0);
    TestCheck(t_write_file(str8f(arena, "%S.rdi", name), data));
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    U32 file_ids[2] = {0};
    for EachElement(unit_idx, file_ids)
    {
      U64 address = 0x1000+0x1000*unit_idx;
      RDI_ParsedLineTable table = {0};
      rdi_parsed_from_line_table(&rdi, rdi_line_table_from_unit(&rdi, rdi_unit_from_voff(&rdi, address)), &table);
      U64 row = rdi_line_info_idx_from_voff(&table, address);
      TestCheck(row < table.count && table.lines[row].line_num == 7);
      file_ids[unit_idx] = table.lines[row].file_idx;
    }
    all_paths_match &= file_ids[0] != 0 && file_ids[1] != 0 &&
                       ((file_ids[0] == file_ids[1]) == (cases[case_idx].files == 1));
    RDI_ParsedNameMap map = {0};
    rdi_parsed_from_name_map(&rdi, rdi_element_from_name_idx(&rdi, NameMaps, RDI_NameMapKind_NormalSourcePaths), &map);
    U32 id_count = 0;
    String8 canonical = cases[case_idx].canonical;
    U32 *ids = rdi_matches_from_map_node(&rdi, rdi_name_map_lookup(&rdi, &map, canonical.str, canonical.size), &id_count);
    TestCheck(id_count == cases[case_idx].files);
    U32 total_voffs = 0;
    U32 seen_voffs = 0;
    for EachIndex(idx, id_count)
    {
      RDI_SourceFile *file = rdi_element_from_name_idx(&rdi, SourceFiles, ids[idx]);
      RDI_ParsedSourceLineMap source_map = {0};
      rdi_parsed_from_source_line_map(&rdi, rdi_source_line_map_from_source_file(&rdi, file), &source_map);
      U32 voff_count = 0;
      U64 *voffs = rdi_line_voffs_from_num(&source_map, 7, &voff_count);
      for EachIndex(voff_idx, voff_count)
      {
        all_paths_match &= voffs[voff_idx] == 0x1000 || voffs[voff_idx] == 0x2000;
        if(voffs[voff_idx] == 0x1000) {seen_voffs |= 1;}
        if(voffs[voff_idx] == 0x2000) {seen_voffs |= 2;}
      }
      total_voffs += voff_count;
    }
    test_outf("  canonical lookup: %u source IDs, %u line-7 addresses\n", id_count, total_voffs);
    all_paths_match &= total_voffs == 2 && seen_voffs == 3;
    if(cases[case_idx].files == 2)
    {
      for EachElement(unit_idx, cases[case_idx].paths)
      {
        String8 requested = cases[case_idx].paths[unit_idx];
        U32 selected_idx = rdi_source_file_idx_from_path(&rdi, requested);
        RDI_SourceFile *selected = rdi_element_from_name_idx(&rdi, SourceFiles, selected_idx);
        U64 selected_voff = rdi_first_voff_from_source_file_line_num(&rdi, selected, 7);
        U64 expected_voff = 0x1000+0x1000*unit_idx;
        test_outf("  %S selects 0x%I64x (expected 0x%I64x)\n", requested, selected_voff, expected_voff);
        all_paths_match &= selected_voff == expected_voff;
        String8 backslashes = str8_copy(arena, requested);
        for EachIndex(idx, backslashes.size) {if(backslashes.str[idx] == '/') {backslashes.str[idx] = '\\';}}
        all_paths_match &= rdi_source_file_idx_from_path(&rdi, backslashes) == selected_idx;
      }
    }
    else
    {
      all_paths_match &= rdi_source_file_idx_from_path(&rdi, canonical) == file_ids[0];
    }
    String8 unmatched_case = upper_from_str8(arena, canonical);
    all_paths_match &= rdi_source_file_idx_from_path(&rdi, unmatched_case) == ids[0];
    all_paths_match &= rdi_source_file_idx_from_path(&rdi, str8_lit("/missing/file.c")) == 0;
    arena_release(convert_arena);
  }
  TestCheck(all_paths_match);
}

Test(d2r_missing_stmt_list)
{
  // CU a points to the line table at offset zero. CU b either has no
  // DW_AT_stmt_list or points to its own table (the control).
  U8 abbrev[] = {1,0x11,1, 3,8, 0x1b,8, 0x10,0x17, 0x11,1, 0x12,6, 0,0,
                 2,0x11,1, 3,8, 0x1b,8, 0x11,1, 0x12,6, 0,0,
                 3,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0, 0};
  U8 header4[] = {0,0,0,0, 4,0, 0,0,0,0, 8};
  U8 header5[] = {0,0,0,0, 5,0, 1,8, 0,0,0,0};
  U8 type[] = {3,'i','n','t',0,5,4,0};
  U8 second_type[] = {3,'o','t','h','e','r',0,5,4,0};
  U8 line4[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                0,1,1,1,1,0,0,0,1,0,0,1, 0, 'a','.','c',0,0,0,0,0};
  U8 line5[] = {0,0,0,0, 5,0,8,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                0,1,1,1,1,0,0,0,1,0,0,1,
                1,1,8,1, '/','s','r','c',0,
                2,1,8,2,0x0f,1, 'a','.','c',0,0};
  U32 header_size4 = sizeof(line4)-10, header_size5 = sizeof(line5)-12;
  MemoryCopy(line4+6, &header_size4, 4);
  MemoryCopy(line5+8, &header_size5, 4);
  B32 all_units_match = 1;
  for(U64 version = 4; version <= 5; version += 1) for(U64 present = 0; present <= 1; present += 1)
  {
    String8List parts = {0};
    U32 second_line_off = 0;
    for(U64 idx = 0; idx < 2; idx += 1)
    {
      String8List line_parts = {0};
      line4[sizeof(line4)-8] = line5[sizeof(line5)-5] = idx == 0 ? 'a' : 'b';
      U8 program[] = {0,9,2, 0,idx == 0 ? 0x10 : 0x20,0,0,0,0,0,0,
                      4,version == 4 ? 1 : 0, 1, 2,4, 0,1,1};
      str8_list_push(arena, &line_parts, version == 4 ? str8_array_fixed(line4) : str8_array_fixed(line5));
      str8_list_push(arena, &line_parts, str8_array_fixed(program));
      String8 line_data = str8_list_join(arena, &line_parts, 0);
      U32 line_size = line_data.size-4;
      MemoryCopy(line_data.str, &line_size, 4);
      str8_list_push(arena, &parts, line_data);
      if(idx == 0) {second_line_off = line_data.size;}
    }
    String8 lines = str8_list_join(arena, &parts, 0);
    MemoryZeroStruct(&parts);
    for(U64 idx = 0; idx < 2; idx += 1)
    {
      String8List unit_parts = {0};
      str8_list_push(arena, &unit_parts, version == 4 ? str8_array_fixed(header4) : str8_array_fixed(header5));
      U8 code = idx == 0 || present ? 1 : 2;
      str8_list_push(arena, &unit_parts, str8_struct(&code));
      str8_list_push(arena, &unit_parts, idx == 0 ? str8_lit("a.c\0") : str8_lit("b.c\0"));
      str8_list_push(arena, &unit_parts, str8_lit("/src\0"));
      U32 stmt_list = idx == 0 ? 0 : second_line_off;
      if(code == 1) {str8_list_push(arena, &unit_parts, str8_struct(&stmt_list));}
      U64 low_pc = idx == 0 ? 0x1000 : 0x2000;
      U32 high_pc = 4;
      str8_list_push(arena, &unit_parts, str8_struct(&low_pc));
      str8_list_push(arena, &unit_parts, str8_struct(&high_pc));
      str8_list_push(arena, &unit_parts, idx == 0 ? str8_array_fixed(type) : str8_array_fixed(second_type));
      String8 unit_data = str8_list_join(arena, &unit_parts, 0);
      U32 unit_size = unit_data.size-4;
      MemoryCopy(unit_data.str, &unit_size, 4);
      str8_list_push(arena, &parts, unit_data);
    }
    String8 info = str8_list_join(arena, &parts, 0);
    String8 name = str8f(arena, "v%I64u-%s", version, present ? "present" : "absent");
    TestCheck(t_write_file(str8f(arena, "%S.info", name), info));
    TestCheck(t_write_file(str8f(arena, "%S.abbrev", name), str8_array_fixed(abbrev)));
    TestCheck(t_write_file(str8f(arena, "%S.line", name), lines));
    RDIM_BinarySectionNode section = {0};
    section.v.voff_first = 0x1000;
    section.v.voff_opl = 0x2004;
    D2R_ConvertParams params = {0};
    params.arch = Arch_x64;
    params.path_style = PathStyle_UnixAbsolute;
    params.subset_flags = RDIM_SubsetFlag_All;
    params.deterministic = 1;
    params.binary_sections.first = params.binary_sections.last = &section;
    params.binary_sections.count = 1;
    params.raw.sec[DW_SectionKind_Info].data = info;
    params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
    params.raw.sec[DW_SectionKind_Line].data = lines;
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count=1, .broadcast_memory=&broadcast}));
    Arena *convert_arena = arena_alloc();
    RDIM_BakeParams result = d2r_convert(convert_arena, &params);
    TestCheck(result.units.total_count == 2);
    RDIM_LineTable *first = result.units.first->v[0].line_table;
    RDIM_LineTable *second = result.units.first->v[1].line_table;
    test_outf("%S: first unit %I64u lines, second unit %I64u lines\n", name, first->line_count, second->line_count);
    TestCheck(first->line_count == 1 && first->first_seq->v.voffs[0] == 0x1000);
    all_units_match &= second->line_count == present && second->seq_count == present;
    B32 found_other = 0;
    for EachNode(chunk, RDIM_TypeChunkNode, result.types.first) for EachIndex(idx, chunk->count)
    {
      RDIM_Type *type = &chunk->v[idx];
      found_other |= type->kind == RDI_TypeKind_Alias && type->byte_size == 4 &&
                     str8_match(type->name, str8_lit("other"), 0);
    }
    TestCheck(found_other);
    RDIM_BakeResults baked = rdim_bake(convert_arena, &result);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(convert_arena, &baked.section_bundle);
    String8 data = str8_list_join(convert_arena, &blobs, 0);
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    RDI_ParsedLineTable parsed = {0};
    rdi_parsed_from_line_table(&rdi, rdi_line_table_from_unit(&rdi, rdi_unit_from_voff(&rdi, 0x2000)), &parsed);
    test_outf("%S: baked second unit %I64u lines\n", name, parsed.count);
    all_units_match &= parsed.count == (present ? 2 : 0);
    if(present) {TestCheck(parsed.voffs[0] == 0x2000 && parsed.lines[0].line_num == 1);}
    arena_release(convert_arena);
  }
  TestCheck(all_units_match);
}

Test(d2r_sparse_units)
{
  // Keep line decoding and empty child lists independently testable.
  for(U64 test_case = 0; test_case < 5; test_case += 1)
  {
    U8 abbrev[] = {1,0x11,1, 3,8, 0x10,0x17, 0,0,
                   2,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0, 0};
    U8 info[] = {0,0,0,0, 4,0, 0,0,0,0, 8, 1,'a',0, 0,0,0,0,
                 2,'i','n','t',0,5,4, 0};
    U64 info_size = sizeof(info);
    if(test_case == 0)
    {
      abbrev[2] = 0;
      info_size = 18;
    }
    U32 unit_size = info_size-4;
    MemoryCopy(info, &unit_size, 4);
    U8 lines[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                   0,1,1,1,1,0,0,0,1,0,0,1, 0, 'a',0,0,0,0,0,
                   2,1, 0,1,1};
    U32 line_size = sizeof(lines)-4, header_size = sizeof(lines)-15;
    MemoryCopy(lines, &line_size, 4);
    MemoryCopy(lines+6, &header_size, 4);
    if(test_case == 2)
    {
      lines[14] = 0; // Invalid line_range, used by const_add_pc.
      lines[sizeof(lines)-5] = 8;
    }
    if(test_case == 3) lines[11] = 0; // Invalid max_ops_per_inst.
    if(test_case == 4) {TestCheck(t_write_file(str8_lit("empty.line"), str8_array_fixed(lines)));}
    D2R_ConvertParams params = {0};
    params.arch = Arch_x64;
    params.path_style = PathStyle_UnixAbsolute;
    params.subset_flags = RDIM_SubsetFlag_All;
    params.deterministic = 1;
    params.raw.sec[DW_SectionKind_Info].data = str8(info, info_size);
    params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
    if(test_case != 1) params.raw.sec[DW_SectionKind_Line].data = str8_array_fixed(lines);
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count = 1, .broadcast_memory = &broadcast}));
    Arena *convert_arena = arena_alloc();
    test_outf("sparse unit case: %I64u\n", test_case);
    RDIM_BakeParams result = d2r_convert(convert_arena, &params);
    lane_ctx(restore);
    TestCheck(result.units.total_count == 1);
    TestCheck(result.units.first->v[0].line_table != 0);
    if(test_case != 0)
    {
      // Case 4 has only advance_pc; end_sequence. LLVM emits a terminator,
      // not a source row with a nonempty address range.
      TestCheck(result.units.first->v[0].line_table->line_count == 0);
      B32 found_int = 0;
      for EachNode(chunk, RDIM_TypeChunkNode, result.types.first)
      {
        for EachIndex(idx, chunk->count)
        {
          RDIM_Type *type = &chunk->v[idx];
          found_int |= type->kind == RDI_TypeKind_Alias && type->byte_size == 4 &&
                       str8_match(type->name, str8_lit("int"), 0);
        }
      }
      TestCheck(found_int);
    }
    arena_release(convert_arena);
  }
}

Test(d2r_expression_rejection)
{
  struct
  {
    String8 name;
    U8 expr[32];
    U8 expr_size;
    RDI_LocationKind kind;
    U8 framebase[8];
    U8 framebase_size;
    U8 list_kind; // 1: frame-base list; 2: variable-location list.
    RDI_LocationKind first_kind;
    B32 is_thread_variable;
    U8 bytecode[32];
    U8 bytecode_size;
    B32 check_value;
    U64 value;
  }
  cases[] =
  {
    {str8_lit("register"), {DW_ExprOp_Reg0}, 1, RDI_LocationKind_ValReg},
    {str8_lit("unsupported-xderef"), {DW_ExprOp_Lit1, DW_ExprOp_Lit2, DW_ExprOp_XDeref}, 3},
    {str8_lit("register-nop"), {DW_ExprOp_Reg0, DW_ExprOp_Nop}, 2, RDI_LocationKind_ValReg},
    {str8_lit("implicit-value"), {DW_ExprOp_ImplicitValue, 1, 42}, 3, RDI_LocationKind_ValBytecodeStream},
    {str8_lit("padded-block-length"), {DW_ExprOp_ImplicitValue, 0x81,0, 42}, 4, RDI_LocationKind_ValBytecodeStream},
    {str8_lit("truncated-block"), {DW_ExprOp_ImplicitValue, 4, 42}, 3},
    {str8_lit("missing-block-length"), {DW_ExprOp_ImplicitValue}, 1},
    {str8_lit("truncated-const"), {DW_ExprOp_Const4U, 42}, 2},
    {str8_lit("missing-const"), {DW_ExprOp_Const1U}, 1},
    {str8_lit("truncated-uleb"), {DW_ExprOp_ConstU, 0x80}, 2},
    {str8_lit("truncated-sleb"), {DW_ExprOp_BReg0, 0x80}, 2},
    {str8_lit("oversized-uleb"), {DW_ExprOp_ConstU, 0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0}, 12},
    {str8_lit("overflowing-uleb"), {DW_ExprOp_ConstU, 0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,2}, 11},
    {str8_lit("unknown-first"), {0xff, DW_ExprOp_Reg0}, 2},
    {str8_lit("unknown-last"), {DW_ExprOp_Reg0, 0xff}, 2},
    {str8_lit("unknown-middle"), {DW_ExprOp_Lit1, 0xff, DW_ExprOp_Lit2, DW_ExprOp_Plus}, 4},
    {str8_lit("entry-value"), {DW_ExprOp_EntryValue, 1, DW_ExprOp_Reg0, DW_ExprOp_PlusUConst, 4}, 5},
    {str8_lit("gnu-entry-value"), {DW_GNU_ExprOp_EntryValue, 1, DW_ExprOp_Reg0, DW_ExprOp_PlusUConst, 4}, 5},
    {str8_lit("convert"), {DW_ExprOp_Lit1, DW_ExprOp_Convert, 0}, 3},
    {str8_lit("stack-value"), {DW_ExprOp_BReg0, 0, DW_ExprOp_StackValue}, 3, RDI_LocationKind_ValReg},
    {str8_lit("register-address"), {DW_ExprOp_BReg0, 0}, 2, RDI_LocationKind_AddrBytecodeStream},
    {str8_lit("register-offset-address"), {DW_ExprOp_BReg0, 16}, 2, RDI_LocationKind_AddrRegPlusOff},
    {str8_lit("register-offset-value"), {DW_ExprOp_BReg0, 16, DW_ExprOp_StackValue}, 3, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_RegRead,1,8,0,0, RDI_EvalOp_ConstU8,16, RDI_EvalOp_TruncSigned,8,
                   RDI_EvalOp_Add,RDI_EvalTypeGroup_S,0, RDI_EvalOp_Stop}, .bytecode_size = 13},
    {str8_lit("literal-value"), {DW_ExprOp_Lit5, DW_ExprOp_StackValue}, 2, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,5, RDI_EvalOp_Stop}, .bytecode_size = 3},
    {str8_lit("literal-zero-value"), {DW_ExprOp_Lit0, DW_ExprOp_StackValue}, 2, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,0, RDI_EvalOp_Stop}, .bytecode_size = 3},
    {str8_lit("literal-31-value"), {DW_ExprOp_Lit31, DW_ExprOp_StackValue}, 2, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,31, RDI_EvalOp_Stop}, .bytecode_size = 3},
    {str8_lit("dereferenced-value"), {DW_ExprOp_BReg0,0, DW_ExprOp_Deref, DW_ExprOp_StackValue}, 4, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_RegRead,1,8,0,0, RDI_EvalOp_MemRead,8, RDI_EvalOp_Stop}, .bytecode_size = 8},
    {str8_lit("constant-value"), {DW_ExprOp_Const1U,42, DW_ExprOp_StackValue}, 3, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,42, RDI_EvalOp_Stop}, .bytecode_size = 3},
    {str8_lit("over-address"), {DW_ExprOp_BReg0,0, DW_ExprOp_Lit5, DW_ExprOp_Over}, 4, RDI_LocationKind_AddrBytecodeStream},
    {str8_lit("pick-address"), {DW_ExprOp_BReg0,0, DW_ExprOp_Lit5, DW_ExprOp_Lit6, DW_ExprOp_Pick,2}, 6, RDI_LocationKind_AddrBytecodeStream},
    {str8_lit("over-scalar"), {DW_ExprOp_Lit5, DW_ExprOp_BReg0,0, DW_ExprOp_Over}, 4, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,5, RDI_EvalOp_RegRead,1,8,0,0, RDI_EvalOp_Pick,1,
                   RDI_EvalOp_Stop}, .bytecode_size = 10},
    {str8_lit("swap-address"), {DW_ExprOp_BReg0,0, DW_ExprOp_Lit5, DW_ExprOp_Swap}, 4, RDI_LocationKind_AddrBytecodeStream},
    {str8_lit("swap-scalar"), {DW_ExprOp_Lit5, DW_ExprOp_BReg0,0, DW_ExprOp_Swap}, 4, RDI_LocationKind_ValBytecodeStream},
    {str8_lit("dup-address"), {DW_ExprOp_BReg0,0, DW_ExprOp_Dup, DW_ExprOp_Drop}, 4, RDI_LocationKind_AddrBytecodeStream},
    {str8_lit("shra-negative"), {DW_ExprOp_ConstS,0x78, DW_ExprOp_Lit2, DW_ExprOp_Shra, DW_ExprOp_StackValue}, 5, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,0xf8, RDI_EvalOp_TruncSigned,8, RDI_EvalOp_ConstU8,2,
                   RDI_EvalOp_RShift,RDI_EvalTypeGroup_S,8, RDI_EvalOp_Stop},
      .bytecode_size = 10, .check_value = 1, .value = (U64)-2},
    {str8_lit("shra-sign-bit"), {DW_ExprOp_ConstS,0x7f, DW_ExprOp_Lit1, DW_ExprOp_Shra, DW_ExprOp_StackValue}, 5, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,0xff, RDI_EvalOp_TruncSigned,8, RDI_EvalOp_ConstU8,1,
                   RDI_EvalOp_RShift,RDI_EvalTypeGroup_S,8, RDI_EvalOp_Stop},
      .bytecode_size = 10, .check_value = 1, .value = max_U64},
    {str8_lit("shra-positive"), {DW_ExprOp_Lit8, DW_ExprOp_Lit2, DW_ExprOp_Shra, DW_ExprOp_StackValue}, 4, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,8, RDI_EvalOp_ConstU8,2, RDI_EvalOp_RShift,RDI_EvalTypeGroup_S,8, RDI_EvalOp_Stop},
      .bytecode_size = 8, .check_value = 1, .value = 2},
    {str8_lit("shr-negative-bits"), {DW_ExprOp_ConstS,0x78, DW_ExprOp_Lit2, DW_ExprOp_Shr, DW_ExprOp_StackValue}, 5, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,0xf8, RDI_EvalOp_TruncSigned,8, RDI_EvalOp_ConstU8,2,
                   RDI_EvalOp_RShift,RDI_EvalTypeGroup_U,8, RDI_EvalOp_Stop},
      .bytecode_size = 10, .check_value = 1, .value = 0x3ffffffffffffffe},
    {str8_lit("shl"), {DW_ExprOp_Lit1, DW_ExprOp_Lit4, DW_ExprOp_Shl, DW_ExprOp_StackValue}, 4, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,1, RDI_EvalOp_ConstU8,4, RDI_EvalOp_LShift,RDI_EvalTypeGroup_U,8, RDI_EvalOp_Stop},
      .bytecode_size = 8, .check_value = 1, .value = 16},
    {str8_lit("not-zero"), {DW_ExprOp_Lit0, DW_ExprOp_Not, DW_ExprOp_StackValue}, 3, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,0, RDI_EvalOp_BitNot,RDI_EvalTypeGroup_U,0, RDI_EvalOp_Stop},
      .bytecode_size = 6, .check_value = 1, .value = max_U64},
    {str8_lit("not-one"), {DW_ExprOp_Lit1, DW_ExprOp_Not, DW_ExprOp_StackValue}, 3, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,1, RDI_EvalOp_BitNot,RDI_EvalTypeGroup_U,0, RDI_EvalOp_Stop},
      .bytecode_size = 6, .check_value = 1, .value = max_U64-1},
    {str8_lit("ne-equal"), {DW_ExprOp_Lit5, DW_ExprOp_Lit5, DW_ExprOp_Ne, DW_ExprOp_StackValue}, 4, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,5, RDI_EvalOp_ConstU8,5, RDI_EvalOp_NtEq,RDI_EvalTypeGroup_U,0, RDI_EvalOp_Stop},
      .bytecode_size = 8, .check_value = 1, .value = 0},
    {str8_lit("ne-unequal"), {DW_ExprOp_Lit5, DW_ExprOp_Lit6, DW_ExprOp_Ne, DW_ExprOp_StackValue}, 4, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,5, RDI_EvalOp_ConstU8,6, RDI_EvalOp_NtEq,RDI_EvalTypeGroup_U,0, RDI_EvalOp_Stop},
      .bytecode_size = 8, .check_value = 1, .value = 1},
    {str8_lit("module-address"), {DW_ExprOp_Addr, 0,0x10,0,0,0,0,0,0}, 9, RDI_LocationKind_ModuleOff},
    {str8_lit("module-value"), {DW_ExprOp_Addr, 0,0x10,0,0,0,0,0,0, DW_ExprOp_StackValue}, 10, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ModuleOff,0,0x10,0,0, RDI_EvalOp_Stop}, .bytecode_size = 6},
    {str8_lit("module-add-address"), {DW_ExprOp_Addr, 0,0x10,0,0,0,0,0,0, DW_ExprOp_PlusUConst,16}, 11, RDI_LocationKind_ModuleOff},
    {str8_lit("module-add-value"), {DW_ExprOp_Addr, 0,0x10,0,0,0,0,0,0, DW_ExprOp_PlusUConst,16, DW_ExprOp_StackValue}, 12, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ModuleOff,0,0x10,0,0, RDI_EvalOp_ConstU8,16,
                   RDI_EvalOp_Add,RDI_EvalTypeGroup_U,0, RDI_EvalOp_Stop}, .bytecode_size = 11},
    {str8_lit("add-module-value"), {DW_ExprOp_Const1U,16, DW_ExprOp_Addr, 0,0x10,0,0,0,0,0,0, DW_ExprOp_Plus, DW_ExprOp_StackValue}, 13, RDI_LocationKind_ValBytecodeStream,
      .bytecode = {RDI_EvalOp_ConstU8,16, RDI_EvalOp_ModuleOff,0,0x10,0,0,
                   RDI_EvalOp_Add,RDI_EvalTypeGroup_U,0, RDI_EvalOp_Stop}, .bytecode_size = 11},
    {str8_lit("tls-address"), {DW_ExprOp_Const1U,16, DW_ExprOp_FormTlsAddress}, 3, RDI_LocationKind_TLSOff,
      .is_thread_variable = 1},
    {str8_lit("tls-value"), {DW_ExprOp_Const1U,16, DW_ExprOp_FormTlsAddress, DW_ExprOp_StackValue}, 4, RDI_LocationKind_ValBytecodeStream,
      .is_thread_variable = 1, .bytecode = {RDI_EvalOp_ConstU8,16, RDI_EvalOp_TLSOff,0,0,0,0,
                                           RDI_EvalOp_Add,RDI_EvalTypeGroup_U,0, RDI_EvalOp_Stop}, .bytecode_size = 11},
    {str8_lit("stack-value-empty"), {DW_ExprOp_StackValue}, 1},
    {str8_lit("stack-value-after-drop"), {DW_ExprOp_Const1U, 42, DW_ExprOp_Drop, DW_ExprOp_StackValue}, 4},
    {str8_lit("stack-value-after-literal-drop"), {DW_ExprOp_Lit5, DW_ExprOp_Drop, DW_ExprOp_StackValue}, 3},
    {str8_lit("stack-value-not-terminal"), {DW_ExprOp_BReg0, 0, DW_ExprOp_StackValue, DW_ExprOp_PlusUConst, 1}, 5},
    {str8_lit("stack-value-piece"), {DW_ExprOp_Lit5, DW_ExprOp_StackValue, DW_ExprOp_Piece, 4}, 4},
    {str8_lit("skip"), {DW_ExprOp_Lit1, DW_ExprOp_Skip, 1,0, DW_ExprOp_Lit2}, 5},
    {str8_lit("bra"), {DW_ExprOp_Lit1, DW_ExprOp_Lit0, DW_ExprOp_Bra, 1,0, DW_ExprOp_Nop}, 6},
    {str8_lit("large-implicit-value"), {DW_ExprOp_ImplicitValue, 9, 1,2,3,4,5,6,7,8,9}, 11},
    {str8_lit("missing-addr-table"), {DW_ExprOp_Addrx, 0, DW_ExprOp_PlusUConst, 4}, 4},
    {str8_lit("empty"), {0}, 0},
    {str8_lit("nop-only"), {DW_ExprOp_Nop}, 1},
    {str8_lit("missing-framebase"), {DW_ExprOp_FBReg, 0}, 2},
    {str8_lit("valid-framebase"), {DW_ExprOp_FBReg, 0}, 2, RDI_LocationKind_AddrRegPlusOff,
      {DW_ExprOp_BReg6, 0}, 2},
    {str8_lit("framebase-value"), {DW_ExprOp_FBReg, 16, DW_ExprOp_StackValue}, 3, RDI_LocationKind_ValBytecodeStream,
      {DW_ExprOp_BReg6, 0}, 2},
    {str8_lit("rejected-framebase"), {DW_ExprOp_FBReg, 0}, 2, RDI_LocationKind_NULL,
      {DW_ExprOp_Lit1, DW_ExprOp_Lit2, DW_ExprOp_XDeref}, 3},
    {str8_lit("independent-local"), {DW_ExprOp_Reg0}, 1, RDI_LocationKind_ValReg,
      {DW_ExprOp_Lit1, DW_ExprOp_Lit2, DW_ExprOp_XDeref}, 3},
    {str8_lit("ranged-framebase"), {DW_ExprOp_FBReg, 0}, 2, RDI_LocationKind_AddrRegPlusOff,
      {DW_ExprOp_Lit1, DW_ExprOp_Lit2, DW_ExprOp_XDeref}, 3, 1},
    {str8_lit("ranged-independent-local"), {DW_ExprOp_Reg0}, 1, RDI_LocationKind_ValReg,
      {DW_ExprOp_Lit1, DW_ExprOp_Lit2, DW_ExprOp_XDeref}, 3, 1, RDI_LocationKind_ValReg},
    {str8_lit("rejected-tls-case"), {DW_ExprOp_Reg0}, 1, RDI_LocationKind_ValReg,
      {DW_ExprOp_Lit1, DW_ExprOp_FormTlsAddress, 0xff}, 3, 2},
  };
  Arch arches[] = {Arch_x64, Arch_arm64};
  B32 all_cases_match = 1;
  for EachElement(arch_idx, arches) for EachElement(case_idx, cases)
  {
    U8 abbrev[] = {1,0x11,1, 3,8, 0x11,1, 0x12,6, 0,0,
                   2,0x2e,1, 3,8, 0x11,1, 0x12,6, 0,0,
                   3,0x34,0, 3,8, 0x49,0x13, 2,0x18, 0,0,
                   4,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0,
                   5,0x2e,1, 3,8, 0x11,1, 0x12,6, 0x40,0x18, 0,0,
                   6,0x2e,1, 3,8, 0x11,1, 0x12,6, 0x40,0x17, 0,0,
                   7,0x34,0, 3,8, 0x49,0x13, 2,0x17, 0,0, 0};
    U8 header[] = {0,0,0,0, 5,0,1,8, 0,0,0,0,
                   1,'a',0, 0,0x10,0,0,0,0,0,0, 16,0,0,0,
                   4,'i','3','2',0,5,4,
                   2,'f',0, 0,0x10,0,0,0,0,0,0, 16,0,0,0};
    U8 local[] = {3,'x',0, 27,0,0,0};
    if(cases[case_idx].framebase_size != 0) {header[34] = 5;}
    U8 list_offset[] = {12,0,0,0};
    String8 loclists = {0};
    if(cases[case_idx].list_kind != 0)
    {
      // Two adjacent ranges: rejected expression first, valid expression second.
      // DW_LLE_start_end stores absolute addresses; the list starts after its header.
      U8 loc_header[] = {0,0,0,0, 5,0,8,0, 0,0,0,0};
      U8 first[] = {7, 0,0x10,0,0,0,0,0,0, 8,0x10,0,0,0,0,0,0};
      U8 second[] = {7, 8,0x10,0,0,0,0,0,0, 16,0x10,0,0,0,0,0,0};
      U8 valid_framebase[] = {2, DW_ExprOp_BReg6, 0};
      String8List loc_parts = {0};
      str8_list_push(arena, &loc_parts, str8_array_fixed(loc_header));
      str8_list_push(arena, &loc_parts, str8_array_fixed(first));
      str8_list_push(arena, &loc_parts, str8(&cases[case_idx].framebase_size, 1));
      str8_list_push(arena, &loc_parts, str8(cases[case_idx].framebase, cases[case_idx].framebase_size));
      str8_list_push(arena, &loc_parts, str8_array_fixed(second));
      if(cases[case_idx].list_kind == 1)
      {
        header[34] = 6;
        str8_list_push(arena, &loc_parts, str8_array_fixed(valid_framebase));
      }
      else
      {
        header[34] = 2;
        local[0] = 7;
        str8_list_push(arena, &loc_parts, str8(&cases[case_idx].expr_size, 1));
        str8_list_push(arena, &loc_parts, str8(cases[case_idx].expr, cases[case_idx].expr_size));
      }
      str8_list_push(arena, &loc_parts, str8_lit("\0"));
      loclists = str8_list_join(arena, &loc_parts, 0);
      U32 loc_size = loclists.size-4;
      MemoryCopy(loclists.str, &loc_size, 4);
    }
    U8 end[] = {0,0};
    String8List parts = {0};
    str8_list_push(arena, &parts, str8_array_fixed(header));
    if(cases[case_idx].list_kind == 1)
    {
      str8_list_push(arena, &parts, str8_array_fixed(list_offset));
    }
    else if(cases[case_idx].framebase_size != 0 && cases[case_idx].list_kind == 0)
    {
      str8_list_push(arena, &parts, str8(&cases[case_idx].framebase_size, 1));
      str8_list_push(arena, &parts, str8(cases[case_idx].framebase, cases[case_idx].framebase_size));
    }
    str8_list_push(arena, &parts, str8_array_fixed(local));
    if(cases[case_idx].list_kind == 2)
    {
      str8_list_push(arena, &parts, str8_array_fixed(list_offset));
    }
    else
    {
      str8_list_push(arena, &parts, str8(&cases[case_idx].expr_size, 1));
      str8_list_push(arena, &parts, str8(cases[case_idx].expr, cases[case_idx].expr_size));
    }
    str8_list_push(arena, &parts, str8_array_fixed(end));
    String8 info = str8_list_join(arena, &parts, 0);
    U32 info_size = info.size-4;
    MemoryCopy(info.str, &info_size, 4);
    RDIM_BinarySectionNode section = {0};
    section.v.voff_first = 0x1000;
    section.v.voff_opl = 0x1010;
    D2R_ConvertParams params = {0};
    params.arch = arches[arch_idx];
    params.path_style = PathStyle_UnixAbsolute;
    params.subset_flags = RDIM_SubsetFlag_All;
    params.deterministic = 1;
    params.binary_sections.first = params.binary_sections.last = &section;
    params.binary_sections.count = 1;
    params.raw.sec[DW_SectionKind_Info].data = info;
    params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
    params.raw.sec[DW_SectionKind_LocLists].data = loclists;
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count = 1, .broadcast_memory = &broadcast}));
    Arena *convert_arena = arena_alloc();
    RDIM_BakeParams result = d2r_convert(convert_arena, &params);
    RDIM_BakeResults baked = rdim_bake(convert_arena, &result);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(convert_arena, &baked.section_bundle);
    String8 data = str8_list_join(convert_arena, &blobs, 0);
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    U64 locals_count = 0;
    RDI_Symbol *locals = rdi_table_from_name(&rdi, LocalVariables, &locals_count);
    if(cases[case_idx].is_thread_variable)
    {
      TestCheck(locals_count == 1);
      locals = rdi_table_from_name(&rdi, ThreadVariables, &locals_count);
    }
    TestCheck(locals_count == 2);
    RDI_Location location = rdi_location_from_location_voff(&rdi, locals[1].location, 0x1008);
    RDI_LocationKind kind = rdi_kind_from_location(location);
    test_outf("%S %S: location kind %u, expected %u\n", string_from_arch(arches[arch_idx]), cases[case_idx].name, kind, cases[case_idx].kind);
    all_cases_match &= kind == cases[case_idx].kind;
    if(cases[case_idx].list_kind != 0)
    {
      U64 voffs[] = {0x1000, 0x1007, 0x1008, 0x100f};
      for EachElement(voff_idx, voffs)
      {
        RDI_Location actual = rdi_location_from_location_voff(&rdi, locals[1].location, voffs[voff_idx]);
        RDI_LocationKind expected = voff_idx < 2 ? cases[case_idx].first_kind : cases[case_idx].kind;
        all_cases_match &= rdi_kind_from_location(actual) == expected;
      }
    }
    if(kind == RDI_LocationKind_ValReg)
    {
      TestCheck(rdi_regcode_from_location(location) == (arches[arch_idx] == Arch_x64 ? RDI_RegCodeX64_rax : RDI_RegCodeARM64_x0));
    }
    if(cases[case_idx].kind == RDI_LocationKind_ValBytecodeStream &&
       cases[case_idx].expr[0] == DW_ExprOp_ImplicitValue)
    {
      U64 bytes_count = 0;
      U8 *bytes = rdi_table_from_name(&rdi, LocationsBytecodeData, &bytes_count);
      U64 off = rdi_bytecode_data_off_from_location(location);
      all_cases_match &= kind == RDI_LocationKind_ValBytecodeStream && off+3 <= bytes_count &&
                         bytes[off] == RDI_EvalOp_ConstU8 && bytes[off+1] == 42 && bytes[off+2] == RDI_EvalOp_Stop;
    }
    if(cases[case_idx].bytecode_size != 0)
    {
      U64 bytes_count = 0;
      U8 *bytes = rdi_table_from_name(&rdi, LocationsBytecodeData, &bytes_count);
      U64 off = rdi_bytecode_data_off_from_location(location);
      U64 size = cases[case_idx].bytecode_size;
      B32 bytecode_matches = kind == RDI_LocationKind_ValBytecodeStream && off <= bytes_count &&
                             size <= bytes_count-off && MemoryMatch(bytes+off, cases[case_idx].bytecode, size);
      if(!bytecode_matches) {test_outf("%S: value bytecode mismatch\n", cases[case_idx].name);}
      all_cases_match &= bytecode_matches;
      if(cases[case_idx].check_value && kind == RDI_LocationKind_ValBytecodeStream &&
         off <= bytes_count && size <= bytes_count-off)
      {
        String8 bytecode = str8(bytes+off, size);
        E_InterpretCtx interp_ctx = {0};
        E_InterpretCtx *restore_ctx = e_interpret_ctx;
        e_select_interpret_ctx(&interp_ctx, 0, 0);
        E_Interpretation interp = e_interpret(bytecode);
        e_select_interpret_ctx(restore_ctx, 0, 0);
        B32 value_matches = interp.code == E_InterpretationCode_Good && interp.value.u64 == cases[case_idx].value;
        if(!value_matches)
        {
          test_outf("%S: evaluated value %I64x (status %u), expected %I64x\n",
                    cases[case_idx].name, interp.value.u64, interp.code, cases[case_idx].value);
        }
        all_cases_match &= value_matches;
      }
    }
    String8 name = str8f(arena, "%S-%S", string_from_arch(arches[arch_idx]), cases[case_idx].name);
    TestCheck(t_write_file(str8f(arena, "%S.info", name), info));
    TestCheck(t_write_file(str8f(arena, "%S.abbrev", name), str8_array_fixed(abbrev)));
    if(loclists.size != 0) {TestCheck(t_write_file(str8f(arena, "%S.loclists", name), loclists));}
    TestCheck(t_write_file(str8f(arena, "%S.rdi", name), data));
    arena_release(convert_arena);
  }
  TestCheck(all_cases_match);
}

Test(d2r_module_types)
{
  // Two compilation units: a procedure references a base type in a named scope
  // in the second unit. Modules and namespaces must both retain that reference.
  for(U64 module = 0; module <= 1; module += 1)
  {
    U8 abbrev[] = {1,0x11,1, 3,8, 0x11,1, 0x12,6, 0,0,
                   2,0x2e,1, 3,8, 0x11,1, 0x12,6, 0,0,
                   3,0x34,0, 3,8, 0x49,0x10, 2,0x18, 0,0,
                   4,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0,
                   5,0x39,1, 3,8, 0,0, 0};
    abbrev[sizeof(abbrev)-7] = module ? DW_TagKind_Module : DW_TagKind_Namespace;
    U8 info1[] = {0,0,0,0, 5,0,1,8, 0,0,0,0,
                  1,'a',0, 0,0x10,0,0,0,0,0,0, 1,0,0,0,
                  5,'a',0,
                  2,'f',0, 0,0x10,0,0,0,0,0,0, 1,0,0,0,
                  3,'x',0, 0,0,0,0, 2,0x44,0x9f, 0,0,0};
    U8 info2[] = {0,0,0,0, 5,0,1,8, 0,0,0,0,
                  1,'b',0, 0,0x10,0,0,0,0,0,0, 1,0,0,0,
                  5,'b',0,
                  4,'i','3','2',0,5,4, 0,0};
    U32 size1 = sizeof(info1)-4, size2 = sizeof(info2)-4;
    U32 type_off = sizeof(info1) + sizeof(info2) - 9;
    MemoryCopy(info1, &size1, 4);
    MemoryCopy(info2, &size2, 4);
    MemoryCopy(info1+sizeof(info1)-10, &type_off, 4);
    U8 lines[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                   0,1,1,1,1,0,0,0,1,0,0,1, 0, 'a',0,0,0,0,0,
                   0,1,1};
    U32 line_size = sizeof(lines)-4, header_size = sizeof(lines)-13;
    MemoryCopy(lines, &line_size, 4);
    MemoryCopy(lines+6, &header_size, 4);
    String8List parts = {0};
    str8_list_push(arena, &parts, str8_array_fixed(info1));
    str8_list_push(arena, &parts, str8_array_fixed(info2));
    RDIM_BinarySectionNode section = {0};
    section.v.voff_first = 0x1000;
    section.v.voff_opl = 0x1001;
    D2R_ConvertParams params = {0};
    params.arch = Arch_x64;
    params.path_style = PathStyle_UnixAbsolute;
    params.subset_flags = RDIM_SubsetFlag_All;
    params.deterministic = 1;
    params.binary_sections.first = params.binary_sections.last = &section;
    params.binary_sections.count = 1;
    params.raw.sec[DW_SectionKind_Info].data = str8_list_join(arena, &parts, 0);
    params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
    params.raw.sec[DW_SectionKind_Line].data = str8_array_fixed(lines);
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count = 1, .broadcast_memory = &broadcast}));
    Arena *convert_arena = arena_alloc();
    RDIM_BakeParams result = d2r_convert(convert_arena, &params);
    lane_ctx(restore);
    test_outf("named scope tag: %s\n", module ? "module" : "namespace");
    TestCheck(result.units.total_count == 2);
    RDIM_Unit *unit = &result.units.first->v[0];
    TestCheck(unit->procedures.total_count == 1);
    RDIM_Symbol *procedure = &unit->procedures.first->v[0];
    TestCheck(procedure->root_scope->locals.total_count == 1);
    RDIM_Symbol *local = &procedure->root_scope->locals.first->v[0];
    TestCheck(local->type != 0);
    TestCheck(local->type->kind == RDI_TypeKind_Alias);
    TestCheck(local->type->byte_size == 4);
    TestCheck(str8_match(local->type->name, str8_lit("i32"), 0));
    TestCheck(local->type->direct_type->kind == RDI_TypeKind_S32);
    TestCheck(result.namespaces.total_count == 2);
    TestCheck(procedure->container_namespace != 0);
    TestCheck(str8_match(procedure->container_namespace->name, str8_lit("a"), 0));
    arena_release(convert_arena);
  }
}

Test(d2r_macho_identity)
{
  U8 abbrev[] = {1,0x11,1, 3,8, 0,0, 2,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0, 0};
  U8 info[] = {0,0,0,0, 4,0, 0,0,0,0, 8, 1,'a',0, 2,'i','n','t',0,5,4, 0};
  U32 info_size = sizeof(info)-4;
  MemoryCopy(info, &info_size, 4);
  U8 uuid[] = {0xbe,0xbd,0x1f,0x6f,0x7b,0xf2,0x3d,0x6d,0x88,0x45,0x62,0x3c,0x11,0x07,0x41,0x85};
  Guid expected = {.data1=0xbebd1f6f, .data2=0x7bf2, .data3=0x3d6d,
                   .data4={0x88,0x45,0x62,0x3c,0x11,0x07,0x41,0x85}};
  String8 radbin = test_build_exe_path(arena, str8_lit("radbin"));
  for(U64 cpu = 0; cpu < 2; cpu += 1) for(U64 identity = 0; identity < 3; identity += 1)
  for(U64 deterministic = 0; deterministic < 2; deterministic += 1)
  {
    // Exercise a real UUID, a zero UUID and a missing LC_UUID on both CPUs.
    U8 bytes[1024] = {0};
    MachO_Header64 *header = (MachO_Header64 *)bytes;
    *header = (MachO_Header64){.magic=MACHO_MAGIC_64,
      .cpu_type=cpu ? MACHO_CPU_TYPE_ARM64 : MACHO_CPU_TYPE_X86_64,
      .cpu_subtype=cpu ? 0 : 3, .file_type=10, // MH_DSYM
      .load_command_count=identity == 2 ? 1 : 2};
    MachO_SegmentCommand64 *segment = (MachO_SegmentCommand64 *)(header+1);
    *segment = (MachO_SegmentCommand64){.cmd=MACHO_LC_SEGMENT_64,
      .cmd_size=sizeof(*segment)+2*sizeof(MachO_Section64), .section_count=2,
      .vmsize=sizeof(bytes), .filesize=sizeof(bytes)};
    MemoryCopy(segment->segment_name, "__DWARF", 7);
    MachO_Section64 *sections = (MachO_Section64 *)(segment+1);
    sections[0].offset = 512;
    sections[0].addr = 512;
    sections[0].size = sizeof(info);
    MemoryCopy(sections[0].section_name, "__debug_info", 12);
    sections[1].offset = 768;
    sections[1].addr = 768;
    sections[1].size = sizeof(abbrev);
    MemoryCopy(sections[1].section_name, "__debug_abbrev", 14);
    MemoryCopy(sections[0].segment_name, "__DWARF", 7);
    MemoryCopy(sections[1].segment_name, "__DWARF", 7);
    MemoryCopy(bytes+512, info, sizeof(info));
    MemoryCopy(bytes+768, abbrev, sizeof(abbrev));
    header->load_commands_size = segment->cmd_size;
    if(identity != 2)
    {
      MachO_UUIDCommand *command = (MachO_UUIDCommand *)((U8 *)segment+segment->cmd_size);
      *command = (MachO_UUIDCommand){.cmd=MACHO_LC_UUID, .cmd_size=sizeof(*command)};
      if(identity == 0) {MemoryCopy(command->uuid, uuid, sizeof(uuid));}
      header->load_commands_size += sizeof(*command);
    }
    String8 name = str8f(arena, "%I64u-%I64u-%I64u.macho", cpu, identity, deterministic);
    TestCheck(t_write_file(name, str8_array_fixed(bytes)));
    String8 input = t_make_file_path(arena, name);
    String8 output = str8f(arena, "%S.rdi", input);
    ProcessLaunchParams launch = {.path = get_process_info()->binary_path, .inherit_env = 1};
    str8_list_push(arena, &launch.cmd_line, radbin);
    str8_list_push(arena, &launch.cmd_line, str8_lit("--rdi"));
    str8_list_push(arena, &launch.cmd_line, str8_lit("--thread_count:1"));
    if(deterministic) {str8_list_push(arena, &launch.cmd_line, str8_lit("--deterministic"));}
    str8_list_push(arena, &launch.cmd_line, input);
    str8_list_pushf(arena, &launch.cmd_line, "--out:%S", output);
    Process process = process_launch(&launch);
    U64 exit_code = max_U64;
    TestCheck(process_join(process, max_U64, &exit_code) && exit_code == 0);
    String8 data = data_from_file_path(arena, output);
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    RDI_TopLevelInfo *tli = rdi_element_from_name_idx(&rdi, TopLevelInfo, 0);
    TestCheck(tli->arch == (cpu ? RDI_Arch_ARM64 : RDI_Arch_X64));
    Guid want = !deterministic && identity == 0 ? expected : (Guid){0};
    TestCheck(MemoryMatch(&tli->guid, &want, sizeof(want)));
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
