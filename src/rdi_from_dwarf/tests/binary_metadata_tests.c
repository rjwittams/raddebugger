// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

Test(d2r_elf_image_base)
{
  struct {U16 type; U32 phdr_type; U64 first, second, base;} cases[] =
  {
    {ELF_Type_Exec, ELF_PType_Load, 0,        0x800,    0},
    {ELF_Type_Exec, ELF_PType_Load, 0x800,    0,        0},
    {ELF_Type_Exec, ELF_PType_Load, 0x400000, 0x401000, 0x400000},
    {ELF_Type_Exec, ELF_PType_Load, 0x401000, 0x400000, 0x400000},
    {ELF_Type_Exec, ELF_PType_Null, 0x400000, 0x401000, 0},
    {ELF_Type_Dyn,  ELF_PType_Load, 0x400000, 0x401000, 0},
  };
  for EachElement(case_idx, cases)
  {
    U8 bytes[sizeof(ELF_Hdr64)+2*sizeof(ELF_Phdr64)] = {0};
    ELF_Hdr64 header = {0};
    MemoryCopy(header.e_ident, elf_magic_string.str, elf_magic_string.size);
    header.e_ident[ELF_Identifier_Class] = ELF_Class_64;
    header.e_ident[ELF_Identifier_Data] = ELF_Data_2LSB;
    header.e_type = cases[case_idx].type;
    header.e_phoff = sizeof(header);
    header.e_phentsize = sizeof(ELF_Phdr64);
    header.e_phnum = 2;
    ELF_Phdr64 phdrs[] =
    {
      {.p_type=cases[case_idx].phdr_type, .p_vaddr=cases[case_idx].first},
      {.p_type=cases[case_idx].phdr_type, .p_vaddr=cases[case_idx].second},
    };
    MemoryCopy(bytes, &header, sizeof(header));
    MemoryCopy(bytes+sizeof(header), phdrs, sizeof(phdrs));
    ELF_Bin bin = elf_bin_from_data(arena, str8_array_fixed(bytes));

    U8 abbrev[] = {1,0x11,1, 3,8, 0x1b,8, 0x10,0x17, 0x11,1, 0x12,6, 0,0,
                   2,0x24,0, 3,8, 0x3e,0x0b, 0x0b,0x0b, 0,0, 0};
    U8 info[] = {0,0,0,0, 4,0, 0,0,0,0, 8, 1,
                 'm','a','i','n','.','c',0, '/','s','r','c',0,
                 0,0,0,0, 0,0,0,0,0,0,0,0, 8,0,0,0,
                 2,'i','n','t',0,5,4, 0};
    U8 lines[] = {0,0,0,0, 4,0, 0,0,0,0, 1,1,1,0xfb,14,13,
                  0,1,1,1,1,0,0,0,1,0,0,1, 0,
                  'm','a','i','n','.','c',0, 0,0,0, 0,
                  0,9,2, 0,0,0,0,0,0,0,0, 1,2,8,0,1,1};
    U32 info_size = sizeof(info)-4, line_size = sizeof(lines)-4;
    U32 line_header_size = sizeof(lines)-10-17;
    U64 address = cases[case_idx].base+0x1000;
    MemoryCopy(info, &info_size, 4);
    MemoryCopy(info+28, &address, 8);
    MemoryCopy(lines, &line_size, 4);
    MemoryCopy(lines+6, &line_header_size, 4);
    MemoryCopy(lines+sizeof(lines)-14, &address, 8);
    RDIM_BinarySectionNode section = {0};
    section.v.flags = RDI_BinarySectionFlag_Read|RDI_BinarySectionFlag_Execute;
    section.v.voff_first = 0x1000;
    section.v.voff_opl = 0x1008;
    D2R_ConvertParams params = {0};
    params.arch = Arch_x64;
    params.path_style = PathStyle_UnixAbsolute;
    params.subset_flags = RDIM_SubsetFlag_All;
    params.deterministic = 1;
    params.base_vaddr = elf_base_addr_from_bin(&bin);
    params.binary_sections = (RDIM_BinarySectionList){.first=&section, .last=&section, .count=1};
    params.raw.sec[DW_SectionKind_Info].data = str8_array_fixed(info);
    params.raw.sec[DW_SectionKind_Abbrev].data = str8_array_fixed(abbrev);
    params.raw.sec[DW_SectionKind_Line].data = str8_array_fixed(lines);
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count=1, .broadcast_memory=&broadcast}));
    Arena *convert_arena = arena_alloc();
    RDIM_BakeParams result = d2r_convert(convert_arena, &params);
    RDIM_BakeResults baked = rdim_bake(convert_arena, &result);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(convert_arena, &baked.section_bundle);
    String8 data = str8_list_join(convert_arena, &blobs, 0);
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    RDI_ParsedLineTable parsed = {0};
    rdi_parsed_from_line_table(&rdi, rdi_line_table_from_unit(&rdi, rdi_unit_from_voff(&rdi, 0x1000)), &parsed);
    test_outf("case %I64u: base 0x%I64x, %I64u baked rows\n", case_idx, params.base_vaddr, parsed.count);
    TestCheck(parsed.count == 2 && parsed.voffs[0] == 0x1000 && parsed.voffs[1] == 0x1008 &&
              parsed.lines[0].line_num == 1 && parsed.lines[1].file_idx == 0);
    arena_release(convert_arena);
  }
}

Test(elf_rdi_sections)
{
  for(U64 base = 0; base <= 0x400000; base += 0x400000)
  {
    String8 names = str8_lit("\0.text\0.data\0.debug_info\0");
    ELF_Phdr64 phdr = {.p_type=ELF_PType_Load, .p_vaddr=base};
    ELF_Shdr64 shdrs[] =
    {
      {.sh_name=1,  .sh_addr=base+0x1000, .sh_size=0x20, .sh_offset=0x200, .sh_flags=ELF_Shf_Alloc|ELF_Shf_ExecInstr},
      {.sh_name=7,  .sh_addr=base+0x2000, .sh_size=0x30, .sh_offset=0x300, .sh_flags=ELF_Shf_Alloc|ELF_Shf_Write},
      {.sh_name=13, .sh_size=0x40, .sh_offset=0x400},
    };
    ELF_Bin bin = {0};
    bin.hdr.e_type = ELF_Type_Exec;
    bin.phdrs = (ELF_Phdr64Array){.v=&phdr, .count=1};
    bin.shdrs = (ELF_Shdr64Array){.v=shdrs, .count=ArrayCount(shdrs)};
    bin.sh_name_range = rng_1u64(0, names.size);
    Arena *bake_arena = arena_alloc();
    RDIM_BakeParams params = {0};
    params.subset_flags = RDIM_SubsetFlag_All;
    params.binary_sections = e2r_rdi_binary_sections_from_elf_section_table(bake_arena, names, &bin, &bin.shdrs);
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count=1, .broadcast_memory=&broadcast}));
    RDIM_BakeResults baked = rdim_bake(bake_arena, &params);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(bake_arena, &baked.section_bundle);
    String8 data = str8_list_join(bake_arena, &blobs, 0);
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    U64 count = 0;
    RDI_BinarySection *sections = rdi_table_from_name(&rdi, BinarySections, &count);
    TestCheck(count == 4);
    test_outf("image base 0x%I64x: text [0x%I64x,0x%I64x), debug [0x%I64x,0x%I64x)\n",
              base, sections[1].voff_first, sections[1].voff_opl, sections[3].voff_first, sections[3].voff_opl);
    TestCheck(sections[1].voff_first == 0x1000 && sections[1].voff_opl == 0x1020 &&
              sections[1].foff_first == 0x200 && sections[1].foff_opl == 0x220 &&
              sections[1].flags == (RDI_BinarySectionFlag_Read|RDI_BinarySectionFlag_Execute));
    TestCheck(sections[2].voff_first == 0x2000 && sections[2].voff_opl == 0x2030 &&
              sections[2].flags == (RDI_BinarySectionFlag_Read|RDI_BinarySectionFlag_Write));
    TestCheck(sections[3].voff_first == 0 && sections[3].voff_opl == 0 &&
              sections[3].foff_first == 0x400 && sections[3].foff_opl == 0x440);
    arena_release(bake_arena);
  }
}

Test(macho_rdi_sections)
{
  for(U64 base = 0; base <= 0x100000000; base += 0x100000000)
  for(U32 text_prot = 0; text_prot <= 5; text_prot += 5)
  {
    U8 bytes[sizeof(MachO_Header64)+2*sizeof(MachO_SegmentCommand64)+4*sizeof(MachO_Section64)] = {0};
    MachO_Header64 header = {.magic=MACHO_MAGIC_64, .cpu_type=MACHO_CPU_TYPE_ARM64, .load_command_count=2,
                            .load_commands_size=sizeof(bytes)-sizeof(MachO_Header64)};
    MemoryCopy(bytes, &header, sizeof(header));
    MachO_SegmentCommand64 text = {.cmd=MACHO_LC_SEGMENT_64, .cmd_size=sizeof(text)+3*sizeof(MachO_Section64),
                                 .vmaddr=base, .vmsize=0x1000, .initprot=text_prot, .section_count=3};
    MemoryCopy(text.segment_name, "__TEXT", 6);
    MachO_Section64 text_sections[] =
    {
      {.section_name="__text",    .addr=base+0x100, .size=0x20, .offset=0x100, .flags=MACHO_S_ATTR_PURE_INSTRUCTIONS},
      {.section_name="__stubs",   .addr=base+0x200, .size=0x30, .offset=0x200, .flags=MACHO_S_ATTR_SOME_INSTRUCTIONS},
      {.section_name="__cstring", .addr=base+0x80,  .size=0x10, .offset=0x80},
    };
    U64 cursor = sizeof(header);
    MemoryCopy(bytes+cursor, &text, sizeof(text));
    MemoryCopy(bytes+cursor+sizeof(text), text_sections, sizeof(text_sections));
    cursor += text.cmd_size;
    MachO_SegmentCommand64 data_segment = {.cmd=MACHO_LC_SEGMENT_64, .cmd_size=sizeof(data_segment)+sizeof(MachO_Section64),
                                         .vmaddr=base+0x1000, .vmsize=0x1000, .initprot=3, .section_count=1};
    MemoryCopy(data_segment.segment_name, "__DATA", 6);
    MachO_Section64 data_section = {.section_name="__data", .addr=base+0x1000, .size=0x40, .offset=0x1000};
    MemoryCopy(bytes+cursor, &data_segment, sizeof(data_segment));
    MemoryCopy(bytes+cursor+sizeof(data_segment), &data_section, sizeof(data_section));
    String8 image = str8_array_fixed(bytes);
    MachO_Bin bin = macho_bin_from_data(arena, image);
    Arena *bake_arena = arena_alloc();
    RDIM_BakeParams params = {0};
    params.subset_flags = RDIM_SubsetFlag_All;
    params.binary_sections = m2r_rdi_binary_sections_from_macho_bin(bake_arena, image, &bin);
    U64 broadcast = 0;
    LaneCtx restore = lane_ctx(((LaneCtx){.lane_count=1, .broadcast_memory=&broadcast}));
    RDIM_BakeResults baked = rdim_bake(bake_arena, &params);
    lane_ctx(restore);
    String8List blobs = rdim_file_blobs_from_section_bundle(bake_arena, &baked.section_bundle);
    String8 data = str8_list_join(bake_arena, &blobs, 0);
    RDI_Parsed rdi = {0};
    TestCheck(rdi_parse(data.str, data.size, &rdi) == RDI_ParseStatus_Good);
    U64 count = 0;
    RDI_BinarySection *sections = rdi_table_from_name(&rdi, BinarySections, &count);
    TestCheck(count == 5);
    test_outf("image base 0x%I64x, text protection %u: code flags 0x%x, text voff 0x%I64x\n",
              base, text_prot, sections[1].flags, sections[1].voff_first);
    TestCheck(sections[1].flags == (RDI_BinarySectionFlag_Read|RDI_BinarySectionFlag_Execute) &&
              sections[2].flags == (RDI_BinarySectionFlag_Read|RDI_BinarySectionFlag_Execute));
    TestCheck(sections[1].voff_first == 0x100 && sections[1].voff_opl == 0x120 &&
              sections[2].voff_first == 0x200 && sections[2].voff_opl == 0x230);
    TestCheck(sections[3].flags == RDI_BinarySectionFlag_Read && sections[3].voff_first == 0x80 && sections[3].voff_opl == 0x90);
    TestCheck(sections[4].flags == (RDI_BinarySectionFlag_Read|RDI_BinarySectionFlag_Write) &&
              sections[4].voff_first == 0x1000 && sections[4].voff_opl == 0x1040 &&
              sections[4].foff_first == 0x1000 && sections[4].foff_opl == 0x1040);
    arena_release(bake_arena);
  }
}
