// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal DW_SectionKind
dw_section_kind_from_macho_name(String8 name)
{
  DW_SectionKind result = DW_Section_Null;
  for(DW_SectionKind section_kind = DW_Section_Null+1; section_kind < DW_Section_COUNT; section_kind += 1)
  {
    String8 mach_name = dw_mach_name_string_from_section_kind(section_kind);
    if(str8_match(name, mach_name, 0) ||
       (mach_name.size > 16 && str8_match(name, str8_prefix(mach_name, 16), 0)))
    {
      result = section_kind;
      break;
    }
  }
  return result;
}

internal B32
dw_is_dwarf_present_from_macho_bin(String8 data, MachO_Bin *bin)
{
  B32 result = 0;
  if(bin->is_64)
  {
    for EachIndex(command_idx, bin->load_commands.count)
    {
      MachO_LoadCommandInfo *info = &bin->load_commands.v[command_idx];
      if(info->cmd != MACHO_LC_SEGMENT_64 || info->offset + sizeof(MachO_SegmentCommand64) > data.size)
      {
        continue;
      }
      MachO_SegmentCommand64 segment = {0};
      str8_deserial_read_struct(data, info->offset, &segment);
      U64 section_offset = info->offset + sizeof(segment);
      for(U64 section_idx = 0; section_idx < segment.section_count && section_offset + sizeof(MachO_Section64) <= info->offset + info->cmd_size; section_idx += 1, section_offset += sizeof(MachO_Section64))
      {
        MachO_Section64 section = {0};
        str8_deserial_read_struct(data, section_offset, &section);
        String8 section_name = macho_string_from_fixed_name(section.section_name, sizeof(section.section_name));
        if(dw_section_kind_from_macho_name(section_name) != DW_Section_Null)
        {
          result = 1;
          goto done;
        }
      }
    }
  }
  done:;
  return result;
}

internal DW_Raw
dw_input_from_macho_bin(Arena *arena, String8 data, MachO_Bin *bin)
{
  DW_Raw result = {0};
  B32 is_section_present[ArrayCount(result.sec)] = {0};
  if(bin->is_64)
  {
    for EachIndex(command_idx, bin->load_commands.count)
    {
      MachO_LoadCommandInfo *info = &bin->load_commands.v[command_idx];
      if(info->cmd != MACHO_LC_SEGMENT_64 || info->offset + sizeof(MachO_SegmentCommand64) > data.size)
      {
        continue;
      }
      MachO_SegmentCommand64 segment = {0};
      str8_deserial_read_struct(data, info->offset, &segment);
      U64 section_offset = info->offset + sizeof(segment);
      for(U64 section_idx = 0; section_idx < segment.section_count && section_offset + sizeof(MachO_Section64) <= info->offset + info->cmd_size; section_idx += 1, section_offset += sizeof(MachO_Section64))
      {
        MachO_Section64 section = {0};
        str8_deserial_read_struct(data, section_offset, &section);
        String8 section_name = macho_string_from_fixed_name(section.section_name, sizeof(section.section_name));
        DW_SectionKind section_kind = dw_section_kind_from_macho_name(section_name);
        if(section_kind == DW_Section_Null || is_section_present[section_kind])
        {
          continue;
        }
        U64 section_opl = (U64)section.offset + section.size;
        if(section.offset > data.size || section_opl > data.size)
        {
          continue;
        }
        is_section_present[section_kind] = 1;
        DW_Section *dst = &result.sec[section_kind];
        dst->name = push_str8_copy(arena, section_name);
        dst->data = str8_substr(data, r1u64(section.offset, section_opl));
      }
    }
  }
  return result;
}
