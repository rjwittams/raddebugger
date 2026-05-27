// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal B32
macho_magic_is_32(U32 magic)
{
  B32 result = (magic == MACHO_MAGIC_32);
  return result;
}

internal B32
macho_magic_is_64(U32 magic)
{
  B32 result = (magic == MACHO_MAGIC_64);
  return result;
}

internal B32
macho_magic_is_supported(U32 magic)
{
  B32 result = (macho_magic_is_32(magic) || macho_magic_is_64(magic));
  return result;
}

internal String8
macho_string_from_fixed_name(U8 *name, U64 cap)
{
  U64 size = 0;
  for(; size < cap && name[size] != 0; size += 1);
  String8 result = str8(name, size);
  return result;
}

internal U64
macho_header_size_from_magic(U32 magic)
{
  U64 result = 0;
  if(macho_magic_is_64(magic))
  {
    result = sizeof(MachO_Header64);
  }
  else if(macho_magic_is_32(magic))
  {
    result = sizeof(MachO_Header32);
  }
  return result;
}

internal MachO_LoadCommandInfoArray
macho_load_commands_from_data(Arena *arena, String8 data, U64 load_command_offset, U32 load_command_count, U32 load_commands_size)
{
  MachO_LoadCommandInfoArray result = {0};
  U64 load_commands_opl = load_command_offset + load_commands_size;
  load_commands_opl = Min(load_commands_opl, data.size);
  U64 load_command_count_cap = 0;
  if(load_command_offset <= load_commands_opl)
  {
    load_command_count_cap = (load_commands_opl - load_command_offset)/sizeof(MachO_LoadCommand);
    load_command_count_cap = Min(load_command_count_cap, load_command_count);
  }
  result.v = push_array(arena, MachO_LoadCommandInfo, load_command_count_cap);
  for(U64 idx = 0; idx < load_command_count_cap && load_command_offset + sizeof(MachO_LoadCommand) <= load_commands_opl; idx += 1)
  {
    MachO_LoadCommand command = {0};
    str8_deserial_read_struct(data, load_command_offset, &command);
    if(command.cmd_size < sizeof(command) || load_command_offset + command.cmd_size > load_commands_opl)
    {
      break;
    }
    MachO_LoadCommandInfo *info = &result.v[result.count];
    info->cmd = command.cmd;
    info->cmd_size = command.cmd_size;
    info->offset = load_command_offset;
    result.count += 1;
    load_command_offset += command.cmd_size;
  }
  return result;
}

internal MachO_Bin
macho_bin_from_data(Arena *arena, String8 data)
{
  MachO_Bin result = {0};
  U32 magic = 0;
  if(str8_deserial_read_struct(data, 0, &magic) == sizeof(magic))
  {
    result.is_64 = macho_magic_is_64(magic);
    if(result.is_64)
    {
      if(str8_deserial_read_struct(data, 0, &result.header) == sizeof(result.header))
      {
        result.load_commands = macho_load_commands_from_data(arena, data, macho_header_size_from_magic(magic),
                                                             result.header.load_command_count,
                                                             result.header.load_commands_size);
      }
    }
    else if(macho_magic_is_32(magic))
    {
      MachO_Header32 header32 = {0};
      if(str8_deserial_read_struct(data, 0, &header32) == sizeof(header32))
      {
        result.header.magic = header32.magic;
        result.header.cpu_type = header32.cpu_type;
        result.header.cpu_subtype = header32.cpu_subtype;
        result.header.file_type = header32.file_type;
        result.header.load_command_count = header32.load_command_count;
        result.header.load_commands_size = header32.load_commands_size;
        result.header.flags = header32.flags;
        result.load_commands = macho_load_commands_from_data(arena, data, macho_header_size_from_magic(magic),
                                                             result.header.load_command_count,
                                                             result.header.load_commands_size);
      }
    }
  }
  return result;
}

internal Arch
arch_from_macho_cpu_type(S32 cpu_type)
{
  Arch result = Arch_Null;
  switch(cpu_type)
  {
    case MACHO_CPU_TYPE_X86_64: { result = Arch_x64; } break;
    case MACHO_CPU_TYPE_ARM64:  { result = Arch_arm64; } break;
  }
  return result;
}

internal MachO_UUID
macho_uuid_from_bin(String8 data, MachO_Bin *bin)
{
  MachO_UUID result = {0};
  for EachIndex(idx, bin->load_commands.count)
  {
    MachO_LoadCommandInfo *info = &bin->load_commands.v[idx];
    if(info->cmd == MACHO_LC_UUID && info->offset + sizeof(MachO_UUIDCommand) <= data.size)
    {
      MachO_UUIDCommand command = {0};
      str8_deserial_read_struct(data, info->offset, &command);
      MemoryCopy(result.v, command.uuid, sizeof(result.v));
      break;
    }
  }
  return result;
}

internal B32
macho_uuid_is_zero(MachO_UUID uuid)
{
  MachO_UUID zero = {0};
  B32 result = MemoryMatch(uuid.v, zero.v, sizeof(uuid.v));
  return result;
}

internal B32
macho_uuid_match(MachO_UUID a, MachO_UUID b)
{
  B32 result = MemoryMatch(a.v, b.v, sizeof(a.v));
  return result;
}

internal String8
macho_string_from_uuid(Arena *arena, MachO_UUID uuid)
{
  String8 result = push_str8f(arena, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                              uuid.v[0], uuid.v[1], uuid.v[2], uuid.v[3],
                              uuid.v[4], uuid.v[5],
                              uuid.v[6], uuid.v[7],
                              uuid.v[8], uuid.v[9],
                              uuid.v[10], uuid.v[11], uuid.v[12], uuid.v[13], uuid.v[14], uuid.v[15]);
  return result;
}

internal String8
macho_dsym_path_from_executable_path(Arena *arena, String8 executable_path)
{
  String8 result = {0};
  String8 binary_name = str8_skip_last_slash(executable_path);
  if(binary_name.size != 0)
  {
    String8 app_marker = str8_lit(".app/Contents/MacOS/");
    U64 app_marker_opl = str8_find_needle_reverse(executable_path, 0, app_marker, 0);
    if(app_marker_opl != 0)
    {
      U64 app_path_size = app_marker_opl - str8_lit("/Contents/MacOS/").size;
      String8 app_path = str8_prefix(executable_path, app_path_size);
      result = push_str8f(arena, "%S.dSYM/Contents/Resources/DWARF/%S", app_path, binary_name);
    }
    else
    {
      result = push_str8f(arena, "%S.dSYM/Contents/Resources/DWARF/%S", executable_path, binary_name);
    }
  }
  return result;
}

internal U64
macho_base_vaddr_from_bin(String8 data, MachO_Bin *bin)
{
  U64 result = 0;
  for EachIndex(idx, bin->load_commands.count)
  {
    MachO_LoadCommandInfo *info = &bin->load_commands.v[idx];
    if(info->cmd == MACHO_LC_SEGMENT_64 && info->offset + sizeof(MachO_SegmentCommand64) <= data.size)
    {
      MachO_SegmentCommand64 segment = {0};
      str8_deserial_read_struct(data, info->offset, &segment);
      String8 segment_name = macho_string_from_fixed_name(segment.segment_name, sizeof(segment.segment_name));
      if(str8_match(segment_name, str8_lit("__TEXT"), 0))
      {
        result = segment.vmaddr;
        break;
      }
    }
  }
  return result;
}

internal U64
macho_image_size_from_bin(String8 data, MachO_Bin *bin)
{
  U64 result = 0;
  U64 base_vaddr = macho_base_vaddr_from_bin(data, bin);
  if(base_vaddr != 0)
  {
    U64 max_vaddr = base_vaddr;
    for EachIndex(idx, bin->load_commands.count)
    {
      MachO_LoadCommandInfo *info = &bin->load_commands.v[idx];
      if(info->cmd == MACHO_LC_SEGMENT_64 && info->offset + sizeof(MachO_SegmentCommand64) <= data.size)
      {
        MachO_SegmentCommand64 segment = {0};
        str8_deserial_read_struct(data, info->offset, &segment);
        if(segment.vmaddr >= base_vaddr)
        {
          max_vaddr = Max(max_vaddr, segment.vmaddr + segment.vmsize);
        }
      }
    }
    result = max_vaddr - base_vaddr;
  }
  return result;
}

internal U64Array
macho_function_start_voffs_from_data(Arena *arena, String8 data, MachO_Bin *bin)
{
  U64Array result = {0};
  U64 image_base_vaddr = 0;
  U64 text_vaddr = 0;
  B32 found_text = 0;
  MachO_LinkeditDataCommand function_starts_command = {0};
  B32 found_function_starts = 0;
  for EachIndex(idx, bin->load_commands.count)
  {
    MachO_LoadCommandInfo *info = &bin->load_commands.v[idx];
    if(info->cmd == MACHO_LC_SEGMENT_64 && info->offset + sizeof(MachO_SegmentCommand64) <= data.size)
    {
      MachO_SegmentCommand64 segment = {0};
      str8_deserial_read_struct(data, info->offset, &segment);
      String8 segment_name = macho_string_from_fixed_name(segment.segment_name, sizeof(segment.segment_name));
      if(str8_match(segment_name, str8_lit("__TEXT"), 0))
      {
        image_base_vaddr = segment.vmaddr;
        text_vaddr = segment.vmaddr;
        found_text = 1;
      }
    }
    else if(info->cmd == MACHO_LC_FUNCTION_STARTS && info->offset + sizeof(MachO_LinkeditDataCommand) <= data.size)
    {
      str8_deserial_read_struct(data, info->offset, &function_starts_command);
      found_function_starts = 1;
    }
  }

  if(found_text && found_function_starts &&
     (U64)function_starts_command.dataoff <= data.size)
  {
    U64 data_opl = Min(data.size, (U64)function_starts_command.dataoff + (U64)function_starts_command.datasize);
    String8 function_starts_data = str8_substr(data, r1u64(function_starts_command.dataoff, data_opl));
    U64 max_start_count = function_starts_data.size;
    result.v = push_array(arena, U64, max_start_count);
    U64 cursor = 0;
    U64 function_vaddr = text_vaddr;
    for(; cursor < function_starts_data.size;)
    {
      U64 delta = 0;
      U64 bytes_read = str8_deserial_read_uleb128(function_starts_data, cursor, &delta);
      if(bytes_read == 0 || delta == 0)
      {
        break;
      }
      cursor += bytes_read;
      function_vaddr += delta;
      if(function_vaddr >= image_base_vaddr)
      {
        result.v[result.count] = function_vaddr - image_base_vaddr;
        result.count += 1;
      }
    }
  }
  return result;
}

internal B32
macho_unwind_info_lookup(String8 data, U64 voff, MachO_UnwindInfoLookupResult *result_out)
{
  B32 result = 0;
  if(result_out != 0)
  {
    MemoryZeroStruct(result_out);
  }

  MachO_UnwindInfoSectionHeader *header = str8_deserial_get_raw_ptr(data, 0, sizeof(MachO_UnwindInfoSectionHeader));
  if(header != 0 &&
     header->version == MACHO_UNWIND_SECTION_VERSION &&
     header->index_count >= 2)
  {
    U64 common_encodings_size = (U64)header->common_encodings_array_count*sizeof(U32);
    U64 index_size = (U64)header->index_count*sizeof(MachO_UnwindInfoSectionHeaderIndexEntry);
    U32 *common_encodings = str8_deserial_get_raw_ptr(data, header->common_encodings_array_section_offset, common_encodings_size);
    MachO_UnwindInfoSectionHeaderIndexEntry *index = str8_deserial_get_raw_ptr(data, header->index_section_offset, index_size);
    if(common_encodings != 0 && index != 0)
    {
      U64 index_idx = max_U64;
      for(U64 idx = 0; idx + 1 < header->index_count; idx += 1)
      {
        if((U64)index[idx].function_offset <= voff && voff < (U64)index[idx+1].function_offset)
        {
          index_idx = idx;
          break;
        }
      }

      if(index_idx != max_U64)
      {
        MachO_UnwindInfoSectionHeaderIndexEntry *index_entry = index + index_idx;
        U32 next_index_function_offset = index[index_idx + 1].function_offset;
        U32 page_kind = 0;
        if(str8_deserial_read_struct(data, index_entry->second_level_pages_section_offset, &page_kind) == sizeof(page_kind))
        {
          switch(page_kind)
          {
            case MACHO_UNWIND_SECOND_LEVEL_REGULAR:
            {
              MachO_UnwindInfoRegularSecondLevelPageHeader *page = str8_deserial_get_raw_ptr(data, index_entry->second_level_pages_section_offset, sizeof(*page));
              if(page != 0)
              {
                U64 entries_off = index_entry->second_level_pages_section_offset + page->entry_page_offset;
                U64 entries_size = (U64)page->entry_count*sizeof(MachO_UnwindInfoRegularSecondLevelEntry);
                MachO_UnwindInfoRegularSecondLevelEntry *entries = str8_deserial_get_raw_ptr(data, entries_off, entries_size);
                if(entries != 0 && page->entry_count != 0)
                {
                  for(U64 entry_idx = 0; entry_idx < page->entry_count; entry_idx += 1)
                  {
                    U64 entry_voff = entries[entry_idx].function_offset;
                    U64 next_voff = (entry_idx + 1 < page->entry_count) ? entries[entry_idx + 1].function_offset : next_index_function_offset;
                    if(entry_voff <= voff && voff < next_voff && entries[entry_idx].encoding != 0)
                    {
                      if(result_out != 0)
                      {
                        result_out->voff_range = r1u64(entry_voff, next_voff);
                        result_out->encoding = entries[entry_idx].encoding;
                      }
                      result = 1;
                      break;
                    }
                  }
                }
              }
            }break;

            case MACHO_UNWIND_SECOND_LEVEL_COMPRESSED:
            {
              MachO_UnwindInfoCompressedSecondLevelPageHeader *page = str8_deserial_get_raw_ptr(data, index_entry->second_level_pages_section_offset, sizeof(*page));
              if(page != 0)
              {
                U64 entries_off = index_entry->second_level_pages_section_offset + page->entry_page_offset;
                U64 entries_size = (U64)page->entry_count*sizeof(U32);
                U32 *entries = str8_deserial_get_raw_ptr(data, entries_off, entries_size);

                U64 local_encodings_off = index_entry->second_level_pages_section_offset + page->encodings_page_offset;
                U64 local_encodings_size = (U64)page->encodings_count*sizeof(U32);
                U32 *local_encodings = str8_deserial_get_raw_ptr(data, local_encodings_off, local_encodings_size);

                U64 target_page_voff = voff - index_entry->function_offset;
                if(entries != 0 && local_encodings != 0 && page->entry_count != 0)
                {
                  for(U64 entry_idx = 0; entry_idx < page->entry_count; entry_idx += 1)
                  {
                    U32 entry = entries[entry_idx];
                    U64 entry_page_voff = MACHO_UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(entry);
                    U64 next_page_voff = (entry_idx + 1 < page->entry_count) ? MACHO_UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(entries[entry_idx + 1]) : (U64)next_index_function_offset - index_entry->function_offset;
                    if(entry_page_voff <= target_page_voff && target_page_voff < next_page_voff)
                    {
                      U32 encoding_idx = MACHO_UNWIND_INFO_COMPRESSED_ENTRY_ENCODING_INDEX(entry);
                      U32 encoding = 0;
                      if(encoding_idx < header->common_encodings_array_count)
                      {
                        encoding = common_encodings[encoding_idx];
                      }
                      else
                      {
                        encoding_idx -= header->common_encodings_array_count;
                        if(encoding_idx < page->encodings_count)
                        {
                          encoding = local_encodings[encoding_idx];
                        }
                      }

                      if(encoding != 0)
                      {
                        if(result_out != 0)
                        {
                          result_out->voff_range = r1u64(index_entry->function_offset + entry_page_voff,
                                                         index_entry->function_offset + next_page_voff);
                          result_out->encoding = encoding;
                        }
                        result = 1;
                      }
                      break;
                    }
                  }
                }
              }
            }break;
          }
        }
      }
    }
  }

  return result;
}

internal B32
macho_unwind_x64_saved_regs_from_permutation(U32 reg_count, U32 permutation, U32 *regs_out)
{
  B32 result = 0;
  if(regs_out != 0 && reg_count <= 6)
  {
    U32 permuted[6] = {0};
    switch(reg_count)
    {
      case 6:
      {
        permuted[0] = permutation / 120; permutation -= permuted[0]*120;
        permuted[1] = permutation / 24;  permutation -= permuted[1]*24;
        permuted[2] = permutation / 6;   permutation -= permuted[2]*6;
        permuted[3] = permutation / 2;   permutation -= permuted[3]*2;
        permuted[4] = permutation;
        permuted[5] = 0;
      }break;
      case 5:
      {
        permuted[0] = permutation / 120; permutation -= permuted[0]*120;
        permuted[1] = permutation / 24;  permutation -= permuted[1]*24;
        permuted[2] = permutation / 6;   permutation -= permuted[2]*6;
        permuted[3] = permutation / 2;   permutation -= permuted[3]*2;
        permuted[4] = permutation;
      }break;
      case 4:
      {
        permuted[0] = permutation / 60; permutation -= permuted[0]*60;
        permuted[1] = permutation / 12; permutation -= permuted[1]*12;
        permuted[2] = permutation / 3;  permutation -= permuted[2]*3;
        permuted[3] = permutation;
      }break;
      case 3:
      {
        permuted[0] = permutation / 20; permutation -= permuted[0]*20;
        permuted[1] = permutation / 4;  permutation -= permuted[1]*4;
        permuted[2] = permutation;
      }break;
      case 2:
      {
        permuted[0] = permutation / 5; permutation -= permuted[0]*5;
        permuted[1] = permutation;
      }break;
      case 1:
      {
        permuted[0] = permutation;
      }break;
    }

    U32 used[7] = {0};
    result = 1;
    for(U32 idx = 0; idx < reg_count; idx += 1)
    {
      U32 renum = 0;
      B32 found = 0;
      for(U32 reg = 1; reg < 7; reg += 1)
      {
        if(!used[reg])
        {
          if(renum == permuted[idx])
          {
            regs_out[idx] = reg;
            used[reg] = 1;
            found = 1;
            break;
          }
          renum += 1;
        }
      }
      if(!found)
      {
        result = 0;
        break;
      }
    }
  }
  return result;
}
