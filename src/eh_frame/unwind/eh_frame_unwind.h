// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef EH_FRAME_UNWIND_H
#define EH_FRAME_UNWIND_H

////////////////////////////////
//~ rjf: Module Unwind Info Type

typedef struct EH_UWND_ModuleUnwindInfo EH_UWND_ModuleUnwindInfo;
struct EH_UWND_ModuleUnwindInfo
{
  EH_FrameHdr header;
  EH_PtrCtx ptr_ctx;
  U64 eh_frame_vaddr;
  U64 override_fde_vaddr;
};

////////////////////////////////
//~ rjf: Unwinding Abstraction Implementation

internal UWND_StepResult eh_uwnd_step(Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, void *regs, U64 *cfa_out);

#endif // EH_FRAME_UNWIND_H
