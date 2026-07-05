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
