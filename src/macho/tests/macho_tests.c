Test(macho_file_identity)
{
  MachO_UUID expected = {{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}};
  for(U64 cpu = 0; cpu < 2; cpu += 1) for(U64 test_case = 0; test_case < 10; test_case += 1)
  {
    struct {MachO_Header64 header; MachO_UUIDCommand uuid;} input = {0};
    input.header = (MachO_Header64){.magic=MACHO_MAGIC_64, .cpu_type=MACHO_CPU_TYPE_ARM64,
      .load_command_count=1, .load_commands_size=sizeof(input.uuid)};
    input.header.cpu_type = cpu ? MACHO_CPU_TYPE_ARM64 : MACHO_CPU_TYPE_X86_64;
    input.uuid = (MachO_UUIDCommand){.cmd=MACHO_LC_UUID, .cmd_size=sizeof(input.uuid)};
    MemoryCopy(input.uuid.uuid, expected.v, sizeof(expected.v));
    if(test_case == 1) {input.uuid.uuid[0] ^= 1;}
    if(test_case == 2) {input.header.cpu_type = cpu ? MACHO_CPU_TYPE_X86_64 : MACHO_CPU_TYPE_ARM64;}
    if(test_case == 3) {MemoryZeroArray(input.uuid.uuid);}
    if(test_case == 4) {input.header.load_command_count = 0; input.header.load_commands_size = 0;}
    if(test_case == 5) {input.header.load_commands_size = max_U32;}
    if(test_case == 9) {input.uuid.cmd_size = sizeof(MachO_LoadCommand);}
    String8 bytes = str8_struct(&input);
    if(test_case == 6) {bytes.size -= 1;}
    String8 name = str8f(arena, "%I64u-%I64u.macho", cpu, test_case);
    TestCheck(t_write_file(name, bytes));
    File file = file_open(AccessFlag_Read|AccessFlag_ShareRead, t_make_file_path(arena, name));
    B32 matches = macho_file_matches_image(file, test_case == 8 ? Arch_Null : cpu ? Arch_arm64 : Arch_x64,
                                          test_case == 7 ? (MachO_UUID){0} : expected);
    file_close(file);
    TestCheck(matches == (test_case == 0));
  }
}

Test(macho_linkedit_mapped_bounds)
{
  struct
  {
    String8 name;
    U64 vmaddr;
    U64 vmsize;
    U64 slide;
    U32 dataoff;
    U32 datasize;
    B32 mapped;
    Rng1U64 range;
  } cases[] =
  {
    {str8_lit("upward"), 0x100008000, 0x4000, 0x50000000, 0x6120, 0x30, 1, {0x150008120,0x150008150}},
    {str8_lit("downward"), 0x100008000, 0x4000, 0-0x50000000ull, 0x6120, 0x30, 1, {0xb0008120,0xb0008150}},
    {str8_lit("zero-base"), 0x100008000, 0x4000, 0-0x100008000ull, 0x6000, 0x30, 1, {0,0x30}},
    {str8_lit("vm-before"), 0x100008000, 8, 0, 0x6120, 0x30},
    {str8_lit("vm-overrun"), 0x100008000, 0x140, 0, 0x6120, 0x30},
    {str8_lit("file-before"), 0x100008000, 0x4000, 0, 0x5ff0, 0x30},
    {str8_lit("file-after"), 0x100008000, 0x4000, 0, 0xa010, 0x30},
    {str8_lit("file-overrun"), 0x100008000, 0x5000, 0, 0x9ff0, 0x30},
    {str8_lit("exact-end"), 0x100008000, 0x4000, 0, 0x9fd0, 0x30, 1, {0x10000bfd0,0x10000c000}},
    {str8_lit("empty-end"), 0x100008000, 0x4000, 0, 0xa000, 0, 1, {0x10000c000,0x10000c000}},
    {str8_lit("slide-overflow"), max_U64-0x4000, 0x4000, 0x5000, 0x6120, 0x30},
    {str8_lit("slide-underflow"), 0x4000, 0x4000, 0-0x5000ull, 0x6120, 0x30},
    {str8_lit("offset-overflow"), max_U64-0x4000, 0x4000, 0x3ff0, 0x6120, 0x30},
    {str8_lit("end-overflow"), max_U64-0x4000, 0x4000, 0x3ed0, 0x6120, 0x30},
  };
  for(U64 cpu = 0; cpu < 2; cpu += 1) for EachElement(test_case, cases)
  {
    struct
    {
      MachO_Header64 header;
      MachO_SegmentCommand64 segment;
      MachO_LinkeditDataCommand table;
    } input = {0};
    input.header = (MachO_Header64){.magic=MACHO_MAGIC_64, .cpu_type=cpu ? MACHO_CPU_TYPE_ARM64 : MACHO_CPU_TYPE_X86_64,
      .load_command_count=2, .load_commands_size=sizeof(input.segment)+sizeof(input.table)};
    input.segment = (MachO_SegmentCommand64){.cmd=MACHO_LC_SEGMENT_64, .cmd_size=sizeof(input.segment),
      .vmaddr=cases[test_case].vmaddr, .vmsize=cases[test_case].vmsize, .fileoff=0x6000, .filesize=0x4000};
    MemoryCopy(input.segment.segment_name, "__LINKEDIT", 10);
    input.table = (MachO_LinkeditDataCommand){.cmd=MACHO_LC_FUNCTION_STARTS, .cmd_size=sizeof(input.table),
      .dataoff=cases[test_case].dataoff, .datasize=cases[test_case].datasize};
    String8 bytes = str8_struct(&input);
    MachO_Bin bin = macho_bin_from_data(arena, bytes);
    Rng1U64 range = r1u64(1, 2);
    B32 mapped = macho_linkedit_data_vrange_from_bin(bytes, &bin, MACHO_LC_FUNCTION_STARTS, cases[test_case].slide, &range);
    test_outf("cpu %I64u %S: mapped %i, range [%I64x,%I64x)\n", cpu, cases[test_case].name, mapped, range.min, range.max);
    TestCheck(mapped == cases[test_case].mapped);
    TestCheck(range.min == cases[test_case].range.min);
    TestCheck(range.max == cases[test_case].range.max);
    TestCheck(macho_linkedit_data_vrange_from_bin(bytes, &bin, MACHO_LC_FUNCTION_STARTS, cases[test_case].slide, 0) == mapped);
  }
}

Test(macho_linkedit_command_bounds)
{
  for(U64 cpu = 0; cpu < 2; cpu += 1) for(U64 test_case = 0; test_case < 7; test_case += 1)
  {
    struct
    {
      MachO_Header64 header;
      MachO_LinkeditDataCommand table;
      MachO_SegmentCommand64 segment;
    } input = {0};
    input.header = (MachO_Header64){.magic=MACHO_MAGIC_64, .cpu_type=cpu ? MACHO_CPU_TYPE_ARM64 : MACHO_CPU_TYPE_X86_64,
      .load_command_count=test_case == 1 || test_case == 2 ? 3 : 2,
      .load_commands_size=sizeof(input.table)+sizeof(input.segment)};
    input.table = (MachO_LinkeditDataCommand){.cmd=MACHO_LC_FUNCTION_STARTS, .cmd_size=sizeof(input.table),
      .dataoff=0x6120, .datasize=0x30};
    input.segment = (MachO_SegmentCommand64){.cmd=MACHO_LC_SEGMENT_64, .cmd_size=sizeof(input.segment),
      .vmaddr=0x100008000, .vmsize=0x4000, .fileoff=0x6000, .filesize=0x4000};
    if(test_case == 1)
    {
      // These bytes belong to the next command, not the short table command.
      input.table.cmd_size = sizeof(MachO_LoadCommand);
      input.table.dataoff = MACHO_LC_UUID;
      input.table.datasize = sizeof(MachO_LoadCommand);
      input.segment.fileoff = 0;
    }
    if(test_case == 2)
    {
      // Keep the segment-shaped bytes available, but outside its declared size.
      input.segment.cmd_size = sizeof(MachO_LoadCommand);
      MachO_LoadCommand next = {.cmd=MACHO_LC_UUID, .cmd_size=sizeof(input.segment)-sizeof(MachO_LoadCommand)};
      MemoryCopy(input.segment.segment_name, &next, sizeof(next));
    }
    String8 bytes = str8_struct(&input);
    if(test_case == 3) {bytes.size = sizeof(input.header)+sizeof(MachO_LoadCommand);}
    if(test_case == 4) {bytes.size -= 1;}
    if(test_case == 5) {input.table.cmd = MACHO_LC_UUID;}
    if(test_case == 6) {input.segment.cmd = MACHO_LC_UUID;}
    MachO_Bin bin = macho_bin_from_data(arena, bytes);
    Rng1U64 range = r1u64(1, 2);
    B32 mapped = macho_linkedit_data_vrange_from_bin(bytes, &bin, MACHO_LC_FUNCTION_STARTS, 0, &range);
    test_outf("cpu %I64u command case %I64u: mapped %i\n", cpu, test_case, mapped);
    TestCheck(mapped == (test_case == 0));
    TestCheck(range.min == (test_case == 0 ? 0x100008120 : 0));
    TestCheck(range.max == (test_case == 0 ? 0x100008150 : 0));
    TestCheck(macho_linkedit_data_vrange_from_bin(bytes, &bin, MACHO_LC_FUNCTION_STARTS, 0, 0) == mapped);
  }
}

Test(macho_symbol_table_slide_bounds)
{
  struct
  {
    String8 name;
    U64 vmaddr;
    U64 slide;
    B32 mapped;
    Rng1U64 symbols;
    Rng1U64 strings;
  } cases[] =
  {
    {str8_lit("upward"), 0x100008000, 0x50000000, 1, {0x150008120,0x150008150}, {0x150008160,0x150008180}},
    {str8_lit("downward"), 0x100008000, 0-0x50000000ull, 1, {0xb0008120,0xb0008150}, {0xb0008160,0xb0008180}},
    {str8_lit("zero-base"), 0x100008000, 0-0x100008000ull, 1, {0x120,0x150}, {0x160,0x180}},
    {str8_lit("slide-overflow"), max_U64-0x4000, 0x5000},
    {str8_lit("slide-underflow"), 0x4000, 0-0x5000ull},
    {str8_lit("offset-overflow"), max_U64-0x4000, 0x3ff0},
    {str8_lit("end-overflow"), max_U64-0x4000, 0x3ed0},
  };
  for(U64 cpu = 0; cpu < 2; cpu += 1) for EachElement(test_case, cases)
  {
    struct
    {
      MachO_Header64 header;
      MachO_SegmentCommand64 segment;
      MachO_SymtabCommand table;
    } input = {0};
    input.header = (MachO_Header64){.magic=MACHO_MAGIC_64, .cpu_type=cpu ? MACHO_CPU_TYPE_ARM64 : MACHO_CPU_TYPE_X86_64,
      .load_command_count=2, .load_commands_size=sizeof(input.segment)+sizeof(input.table)};
    input.segment = (MachO_SegmentCommand64){.cmd=MACHO_LC_SEGMENT_64, .cmd_size=sizeof(input.segment),
      .vmaddr=cases[test_case].vmaddr, .vmsize=0x4000, .fileoff=0x6000, .filesize=0x4000};
    input.table = (MachO_SymtabCommand){.cmd=MACHO_LC_SYMTAB, .cmd_size=sizeof(input.table),
      .symoff=0x6120, .nsyms=3, .stroff=0x6160, .strsize=0x20};
    String8 bytes = str8_struct(&input);
    MachO_Bin bin = macho_bin_from_data(arena, bytes);
    Rng1U64 symbols = r1u64(1, 2), strings = r1u64(3, 4);
    B32 mapped = macho_symbol_table_vranges(bytes, &bin, cases[test_case].slide, &symbols, &strings);
    test_outf("cpu %I64u %S: mapped %i\n", cpu, cases[test_case].name, mapped);
    TestCheck(mapped == cases[test_case].mapped);
    TestCheck(symbols.min == cases[test_case].symbols.min);
    TestCheck(symbols.max == cases[test_case].symbols.max);
    TestCheck(strings.min == cases[test_case].strings.min);
    TestCheck(strings.max == cases[test_case].strings.max);
  }
}

// Name-only lookup must not manufacture procedure extents or cross sections.
Test(macho_symbol_fallback)
{
  U8 bytes[0x2000] = {0};
  MachO_Header64 *header = (MachO_Header64 *)bytes;
  *header = (MachO_Header64){.magic=MACHO_MAGIC_64, .cpu_type=MACHO_CPU_TYPE_ARM64, .load_command_count=4};
  U64 cursor = sizeof(*header);
  MachO_SegmentCommand64 *text = (MachO_SegmentCommand64 *)(bytes+cursor);
  *text = (MachO_SegmentCommand64){.cmd=MACHO_LC_SEGMENT_64, .cmd_size=sizeof(*text)+2*sizeof(MachO_Section64),
                                .vmaddr=0x1000, .vmsize=0x1000, .filesize=0x1000, .initprot=5, .section_count=2};
  MemoryCopy(text->segment_name, "__TEXT", 6);
  MachO_Section64 *sections = (MachO_Section64 *)(text+1);
  sections[0] = (MachO_Section64){.addr=0x1100, .size=0x80, .flags=MACHO_S_ATTR_PURE_INSTRUCTIONS};
  sections[1] = (MachO_Section64){.addr=0x1300, .size=0x20, .flags=MACHO_S_ATTR_PURE_INSTRUCTIONS};
  cursor += text->cmd_size;
  MachO_SegmentCommand64 *data = (MachO_SegmentCommand64 *)(bytes+cursor);
  *data = (MachO_SegmentCommand64){.cmd=MACHO_LC_SEGMENT_64, .cmd_size=sizeof(*data)+sizeof(MachO_Section64),
                                .vmaddr=0x2000, .vmsize=0x1000, .fileoff=0x1000, .filesize=0x1000, .initprot=3, .section_count=1};
  *(MachO_Section64 *)(data+1) = (MachO_Section64){.addr=0x2000, .size=0x80};
  cursor += data->cmd_size;
  MachO_UUIDCommand *uuid = (MachO_UUIDCommand *)(bytes+cursor);
  *uuid = (MachO_UUIDCommand){.cmd=MACHO_LC_UUID, .cmd_size=sizeof(*uuid), .uuid={1,2,3,4}};
  cursor += sizeof(*uuid);
  MachO_SymtabCommand *table = (MachO_SymtabCommand *)(bytes+cursor);
  *table = (MachO_SymtabCommand){.cmd=MACHO_LC_SYMTAB, .cmd_size=sizeof(*table), .symoff=0x600, .nsyms=10, .stroff=0x800, .strsize=39};
  cursor += sizeof(*table);
  header->load_commands_size = (U32)(cursor-sizeof(*header));
  String8 strings = str8_lit("\0_first\0_zalias\0_alpha\0_bad\0_data\0broken");
  table->strsize = (U32)strings.size;
  MemoryCopy(bytes+table->stroff, strings.str, strings.size);
  MachO_Nlist64 *symbols = (MachO_Nlist64 *)(bytes+table->symoff);
  symbols[0] = (MachO_Nlist64){.strx=8, .type=MACHO_N_SECT, .sect=1, .value=0x1120};
  symbols[1] = (MachO_Nlist64){.strx=1, .type=MACHO_N_SECT, .sect=1, .value=0x1100};
  symbols[2] = (MachO_Nlist64){.strx=16, .type=MACHO_N_SECT, .sect=1, .value=0x1120};
  symbols[3] = (MachO_Nlist64){.strx=23, .type=1, .sect=1, .value=0x1124}; // undefined
  symbols[4] = (MachO_Nlist64){.strx=23, .type=MACHO_N_STAB|MACHO_N_SECT, .sect=1, .value=0x1128};
  symbols[5] = (MachO_Nlist64){.strx=28, .type=MACHO_N_SECT, .sect=3, .value=0x2000};
  symbols[6] = (MachO_Nlist64){.strx=23, .type=MACHO_N_SECT, .sect=1, .value=0x1180}; // outside section
  symbols[7] = (MachO_Nlist64){.strx=34, .type=MACHO_N_SECT, .sect=1, .value=0x1130}; // unterminated
  symbols[8] = (MachO_Nlist64){.strx=max_U32, .type=MACHO_N_SECT, .sect=1, .value=0x1140};
  symbols[9] = (MachO_Nlist64){.strx=1, .type=MACHO_N_SECT, .sect=255, .value=0x1150};
  String8 file = str8(bytes, sizeof(bytes));
  MachO_Bin bin = macho_bin_from_data(arena, file);
  MachO_UUID id = macho_uuid_from_bin(file, &bin);
  MachO_CodeSymbolArray names = macho_code_symbols_from_matching_file(arena, file, header, id);
  TestCheck(names.count == 2);
  MachO_CodeSymbol *first = macho_code_symbol_from_voff(names, 0x100);
  MachO_CodeSymbol *alias = macho_code_symbol_from_voff(names, 0x128);
  TestCheck(first && str8_match(first->name, str8_lit("_first"), 0));
  TestCheck(alias && str8_match(alias->name, str8_lit("_alpha"), 0) && alias->voff == 0x120);
  TestCheck(macho_code_symbol_from_voff(names, 0xff) == 0);
  TestCheck(macho_code_symbol_from_voff(names, 0x180) == 0);
  TestCheck(macho_code_symbol_from_voff(names, 0x308) == 0);
  TestCheck(macho_code_symbol_from_voff(names, 0x1000) == 0);
  Rng1U64 nrange, srange;
  TestCheck(macho_symbol_table_vranges(file, &bin, 0x4000, &nrange, &srange));
  TestCheck(nrange.min == 0x5600 && dim_1u64(nrange) == 160 && srange.min == 0x5800);
  MachO_UUID wrong_id = id; wrong_id.v[0] ^= 1;
  TestCheck(macho_code_symbols_from_matching_file(arena, file, header, wrong_id).count == 0);
  MachO_Header64 wrong_cpu = *header; wrong_cpu.cpu_type = MACHO_CPU_TYPE_X86_64;
  TestCheck(macho_code_symbols_from_matching_file(arena, file, &wrong_cpu, id).count == 0);
  table->stroff = max_U32;
  TestCheck(macho_code_symbols_from_matching_file(arena, file, header, id).count == 0);
  table->stroff = 0x800;
  // A universal image must choose the matching slice, not the first slice.
  U8 *fat = push_array(arena, U8, 0x4100);
  U32 fat_header[] = {from_be_u32(0xcafebabe), from_be_u32(2),
                     0, 0, from_be_u32(0x100), from_be_u32(0x2000), 0,
                     0, 0, from_be_u32(0x2100), from_be_u32(0x2000), 0};
  MemoryCopy(fat, fat_header, sizeof(fat_header));
  MemoryCopy(fat+0x100, bytes, sizeof(bytes));
  MemoryCopy(fat+0x2100, bytes, sizeof(bytes));
  ((MachO_Header64 *)(fat+0x100))->cpu_type = MACHO_CPU_TYPE_X86_64;
  TestCheck(macho_code_symbols_from_matching_file(arena, str8(fat, 0x4100), header, id).count == 2);
  TestCheck(macho_code_symbols_from_matching_file(arena, str8(fat, 12), header, id).count == 0);
}
