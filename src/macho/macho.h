// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MACHO_H
#define MACHO_H

#define MACHO_MAGIC_32 0xfeedface
#define MACHO_MAGIC_64 0xfeedfacf
#define MACHO_LC_SEGMENT_64 0x19
#define MACHO_LC_UUID 0x1b
#define MACHO_CPU_TYPE_X86_64 0x01000007
#define MACHO_CPU_TYPE_ARM64  0x0100000c

typedef struct MachO_UUID MachO_UUID;
struct MachO_UUID
{
  U8 v[16];
};

typedef struct MachO_Header32 MachO_Header32;
struct MachO_Header32
{
  U32 magic;
  S32 cpu_type;
  S32 cpu_subtype;
  U32 file_type;
  U32 load_command_count;
  U32 load_commands_size;
  U32 flags;
};

typedef struct MachO_Header64 MachO_Header64;
struct MachO_Header64
{
  U32 magic;
  S32 cpu_type;
  S32 cpu_subtype;
  U32 file_type;
  U32 load_command_count;
  U32 load_commands_size;
  U32 flags;
  U32 reserved;
};

typedef struct MachO_LoadCommand MachO_LoadCommand;
struct MachO_LoadCommand
{
  U32 cmd;
  U32 cmd_size;
};

typedef struct MachO_LoadCommandInfo MachO_LoadCommandInfo;
struct MachO_LoadCommandInfo
{
  U32 cmd;
  U32 cmd_size;
  U64 offset;
};

typedef struct MachO_LoadCommandInfoArray MachO_LoadCommandInfoArray;
struct MachO_LoadCommandInfoArray
{
  MachO_LoadCommandInfo *v;
  U64 count;
};

typedef struct MachO_UUIDCommand MachO_UUIDCommand;
struct MachO_UUIDCommand
{
  U32 cmd;
  U32 cmd_size;
  U8 uuid[16];
};

typedef struct MachO_SegmentCommand64 MachO_SegmentCommand64;
struct MachO_SegmentCommand64
{
  U32 cmd;
  U32 cmd_size;
  U8 segment_name[16];
  U64 vmaddr;
  U64 vmsize;
  U64 fileoff;
  U64 filesize;
  S32 maxprot;
  S32 initprot;
  U32 section_count;
  U32 flags;
};

typedef struct MachO_Section64 MachO_Section64;
struct MachO_Section64
{
  U8 section_name[16];
  U8 segment_name[16];
  U64 addr;
  U64 size;
  U32 offset;
  U32 align;
  U32 reloc_offset;
  U32 reloc_count;
  U32 flags;
  U32 reserved1;
  U32 reserved2;
  U32 reserved3;
};

typedef struct MachO_Bin MachO_Bin;
struct MachO_Bin
{
  MachO_Header64 header;
  MachO_LoadCommandInfoArray load_commands;
  B32 is_64;
};

internal B32 macho_magic_is_32(U32 magic);
internal B32 macho_magic_is_64(U32 magic);
internal B32 macho_magic_is_supported(U32 magic);
internal String8 macho_string_from_fixed_name(U8 *name, U64 cap);
internal MachO_Bin macho_bin_from_data(Arena *arena, String8 data);
internal Arch arch_from_macho_cpu_type(S32 cpu_type);
internal MachO_UUID macho_uuid_from_bin(String8 data, MachO_Bin *bin);
internal String8 macho_dsym_path_from_executable_path(Arena *arena, String8 executable_path);
internal U64 macho_base_vaddr_from_bin(String8 data, MachO_Bin *bin);
internal U64 macho_image_size_from_bin(String8 data, MachO_Bin *bin);

#endif // MACHO_H
