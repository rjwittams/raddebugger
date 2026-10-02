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
