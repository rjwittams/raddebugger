// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef ARM64_DISASM_H
#define ARM64_DISASM_H

internal DASM_Inst arm64_dasm_inst_from_code(Arena *arena, U64 vaddr, String8 code, DASM_Syntax syntax);

#endif // ARM64_DISASM_H
