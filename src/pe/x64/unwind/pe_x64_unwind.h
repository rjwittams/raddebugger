// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef PE_X64_UNWIND_H
#define PE_X64_UNWIND_H

////////////////////////////////
//~ rjf: Module Unwind Info Type

typedef struct PE_X64_UWND_ModuleUnwindInfo PE_X64_UWND_ModuleUnwindInfo;
struct PE_X64_UWND_ModuleUnwindInfo
{
  PE_IntelPdata *pdatas;
  U64 pdatas_count;
};

////////////////////////////////
//~ rjf: Helpers

internal PE_IntelPdata *pe_x64_uwnd_intel_pdata_from_voff(PE_X64_UWND_ModuleUnwindInfo *unwind_info, U64 voff);
internal X64_RegCode pe_x64_uwnd_reg_code_from_pe_gpr_reg(PE_UnwindGprRegX64 gpr_reg);

////////////////////////////////
//~ rjf: Unwinding Abstraction Implementation

internal UWND_StepResult pe_x64_uwnd_step(Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, void *regs_opaque, U8 *reg_valid, U64 *cfa_out);

#endif // PE_X64_UNWIND_H
