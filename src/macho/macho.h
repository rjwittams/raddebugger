// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MACHO_H
#define MACHO_H

#define MACHO_MAGIC_32 0xfeedface
#define MACHO_MAGIC_64 0xfeedfacf
#define MACHO_LC_SEGMENT_64 0x19
#define MACHO_LC_UUID 0x1b
#define MACHO_LC_FUNCTION_STARTS 0x26
#define MACHO_LC_MAIN 0x80000028
#define MACHO_CPU_TYPE_X86_64 0x01000007
#define MACHO_CPU_TYPE_ARM64  0x0100000c

#define MACHO_UNWIND_SECTION_VERSION 1
#define MACHO_UNWIND_SECOND_LEVEL_REGULAR 2
#define MACHO_UNWIND_SECOND_LEVEL_COMPRESSED 3
#define MACHO_UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(entry) ((entry) & 0x00ffffff)
#define MACHO_UNWIND_INFO_COMPRESSED_ENTRY_ENCODING_INDEX(entry) (((entry) >> 24) & 0xff)

#define MACHO_UNWIND_X64_MODE_MASK 0x0f000000
#define MACHO_UNWIND_X64_MODE_RBP_FRAME 0x01000000
#define MACHO_UNWIND_X64_MODE_STACK_IMMD 0x02000000
#define MACHO_UNWIND_X64_MODE_STACK_IND 0x03000000
#define MACHO_UNWIND_X64_MODE_DWARF 0x04000000
#define MACHO_UNWIND_X64_RBP_FRAME_REGISTERS 0x00007fff
#define MACHO_UNWIND_X64_RBP_FRAME_OFFSET 0x00ff0000
#define MACHO_UNWIND_X64_FRAMELESS_STACK_SIZE 0x00ff0000
#define MACHO_UNWIND_X64_FRAMELESS_STACK_ADJUST 0x0000e000
#define MACHO_UNWIND_X64_FRAMELESS_STACK_REG_COUNT 0x00001c00
#define MACHO_UNWIND_X64_FRAMELESS_STACK_REG_PERMUTATION 0x000003ff
#define MACHO_UNWIND_X64_DWARF_SECTION_OFFSET 0x00ffffff

#define MACHO_UNWIND_ARM64_MODE_MASK 0x0f000000
#define MACHO_UNWIND_ARM64_MODE_FRAMELESS 0x02000000
#define MACHO_UNWIND_ARM64_MODE_DWARF 0x03000000
#define MACHO_UNWIND_ARM64_MODE_FRAME 0x04000000
#define MACHO_UNWIND_ARM64_FRAME_X19_X20_PAIR 0x00000001
#define MACHO_UNWIND_ARM64_FRAME_X21_X22_PAIR 0x00000002
#define MACHO_UNWIND_ARM64_FRAME_X23_X24_PAIR 0x00000004
#define MACHO_UNWIND_ARM64_FRAME_X25_X26_PAIR 0x00000008
#define MACHO_UNWIND_ARM64_FRAME_X27_X28_PAIR 0x00000010
#define MACHO_UNWIND_ARM64_FRAME_D8_D9_PAIR 0x00000100
#define MACHO_UNWIND_ARM64_FRAME_D10_D11_PAIR 0x00000200
#define MACHO_UNWIND_ARM64_FRAME_D12_D13_PAIR 0x00000400
#define MACHO_UNWIND_ARM64_FRAME_D14_D15_PAIR 0x00000800
#define MACHO_UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK 0x00fff000
#define MACHO_UNWIND_ARM64_DWARF_SECTION_OFFSET 0x00ffffff

typedef struct MachO_UUID MachO_UUID;
struct MachO_UUID
{
  U8 v[16];
};

typedef enum MachO_UnwindX64Reg
{
  MachO_UnwindX64Reg_Null,
  MachO_UnwindX64Reg_RBX,
  MachO_UnwindX64Reg_R12,
  MachO_UnwindX64Reg_R13,
  MachO_UnwindX64Reg_R14,
  MachO_UnwindX64Reg_R15,
  MachO_UnwindX64Reg_RBP,
}
MachO_UnwindX64Reg;

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

typedef struct MachO_EntryPointCommand MachO_EntryPointCommand;
struct MachO_EntryPointCommand
{
  U32 cmd;
  U32 cmd_size;
  U64 entryoff;
  U64 stacksize;
};

typedef struct MachO_LinkeditDataCommand MachO_LinkeditDataCommand;
struct MachO_LinkeditDataCommand
{
  U32 cmd;
  U32 cmd_size;
  U32 dataoff;
  U32 datasize;
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

typedef struct MachO_UnwindInfoSectionHeader MachO_UnwindInfoSectionHeader;
struct MachO_UnwindInfoSectionHeader
{
  U32 version;
  U32 common_encodings_array_section_offset;
  U32 common_encodings_array_count;
  U32 personality_array_section_offset;
  U32 personality_array_count;
  U32 index_section_offset;
  U32 index_count;
};

typedef struct MachO_UnwindInfoSectionHeaderIndexEntry MachO_UnwindInfoSectionHeaderIndexEntry;
struct MachO_UnwindInfoSectionHeaderIndexEntry
{
  U32 function_offset;
  U32 second_level_pages_section_offset;
  U32 lsda_index_array_section_offset;
};

typedef struct MachO_UnwindInfoRegularSecondLevelEntry MachO_UnwindInfoRegularSecondLevelEntry;
struct MachO_UnwindInfoRegularSecondLevelEntry
{
  U32 function_offset;
  U32 encoding;
};

typedef struct MachO_UnwindInfoRegularSecondLevelPageHeader MachO_UnwindInfoRegularSecondLevelPageHeader;
struct MachO_UnwindInfoRegularSecondLevelPageHeader
{
  U32 kind;
  U16 entry_page_offset;
  U16 entry_count;
};

typedef struct MachO_UnwindInfoCompressedSecondLevelPageHeader MachO_UnwindInfoCompressedSecondLevelPageHeader;
struct MachO_UnwindInfoCompressedSecondLevelPageHeader
{
  U32 kind;
  U16 entry_page_offset;
  U16 entry_count;
  U16 encodings_page_offset;
  U16 encodings_count;
};

typedef struct MachO_UnwindInfoLookupResult MachO_UnwindInfoLookupResult;
struct MachO_UnwindInfoLookupResult
{
  Rng1U64 voff_range;
  U32 encoding;
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
internal B32 macho_uuid_is_zero(MachO_UUID uuid);
internal B32 macho_uuid_match(MachO_UUID a, MachO_UUID b);
internal String8 macho_string_from_uuid(Arena *arena, MachO_UUID uuid);
internal String8 macho_dsym_path_from_executable_path(Arena *arena, String8 executable_path);
internal U64 macho_base_vaddr_from_bin(String8 data, MachO_Bin *bin);
internal U64 macho_image_size_from_bin(String8 data, MachO_Bin *bin);
internal B32 macho_linkedit_data_vrange_from_bin(String8 data, MachO_Bin *bin, U32 cmd, U64 slide, Rng1U64 *vrange_out);
internal U64Array macho_function_start_voffs_from_base_vaddr_data(Arena *arena, U64 image_base_vaddr, U64 text_vaddr, String8 function_starts_data);
internal U64Array macho_function_start_voffs_from_data(Arena *arena, String8 data, MachO_Bin *bin);
internal B32 macho_unwind_info_lookup(String8 data, U64 voff, MachO_UnwindInfoLookupResult *result_out);
internal B32 macho_unwind_x64_saved_regs_from_permutation(U32 reg_count, U32 permutation, U32 *regs_out);

#endif // MACHO_H
