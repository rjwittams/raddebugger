// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef DWARF_MACHO_H
#define DWARF_MACHO_H

internal DW_SectionKind dw_section_kind_from_macho_name(String8 name);
internal B32 dw_is_dwarf_present_from_macho_bin(String8 data, MachO_Bin *bin);
internal DW_Raw dw_input_from_macho_bin(Arena *arena, String8 data, MachO_Bin *bin);

#endif // DWARF_MACHO_H
