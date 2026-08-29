// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal RDIM_BinarySectionList
m2r_rdi_binary_sections_from_macho_bin(Arena *arena, String8 data, MachO_Bin *bin)
{
  RDIM_BinarySectionList result = {0};
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
        U64 voff_first = section.addr;
        U64 voff_opl = voff_first + section.size;
        U64 foff_first = section.offset;
        U64 foff_opl = foff_first + section.size;
        if(section_name.size != 0 || voff_opl != voff_first || foff_opl != foff_first)
        {
          RDIM_BinarySection *dst = rdim_binary_section_list_push(arena, &result);
          dst->name = push_str8_copy(arena, section_name);
          dst->flags = RDI_BinarySectionFlag_Read;
          dst->voff_first = voff_first;
          dst->voff_opl = voff_opl;
          dst->foff_first = foff_first;
          dst->foff_opl = foff_opl;
        }
      }
    }
  }
  return result;
}
