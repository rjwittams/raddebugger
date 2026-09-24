// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef UNWIND_H
#define UNWIND_H

////////////////////////////////
//~ rjf: Generated Code

#include "unwind/generated/unwind.meta.h"

////////////////////////////////
//~ rjf: Unwind Artifact Types

typedef enum UWND_StepStatus
{
  UWND_StepStatus_Error,
  UWND_StepStatus_FailedMemoryRead,
  UWND_StepStatus_Good,
  UWND_StepStatus_EndOfStack,
}
UWND_StepStatus;

typedef struct UWND_StepResult UWND_StepResult;
struct UWND_StepResult
{
  UWND_StepStatus status;
  Rng1U64 missed_read_vaddr_range;
  B32 is_interrupted; // recovered PC is an interrupted instruction, not a return address
};

////////////////////////////////
//~ rjf: Module Info

typedef struct UWND_ModuleInfo UWND_ModuleInfo;
struct UWND_ModuleInfo
{
  U64 base_vaddr;
  void *unwind_info;
  OperatingSystem os;
};

// One validity byte per register-block byte. A null mask is allowed only for
// callers that supply a complete live context and do not consume availability.
internal B32 uwnd_reg_range_is_valid(U8 *valid, Rng1U16 range);
internal void uwnd_reg_set_valid(U8 *valid, Rng1U16 range, B32 value);
internal void uwnd_reg_valid_from_abi(Arch arch, OperatingSystem os, U8 *src, U8 *dst);
internal B32 uwnd_reg_is_valid(Arch arch, U8 *valid, U8 code);
internal void uwnd_reg_mark_valid(Arch arch, U8 *valid, U8 code, U64 size);
// Bias only the address used for range/row lookup; register bytes remain exact.
internal U64 uwnd_lookup_vaddr_from_pc(Arch arch, U64 pc, B32 is_return_address);

////////////////////////////////
//~ rjf: Moduleless Unwinder

internal UWND_StepResult uwnd_step_moduleless(Arch arch, MemoryMap *memory_map, void *regs, U8 *reg_valid);

////////////////////////////////
//~ rjf: Abstracted Unwind Step Functions

internal UWND_StepResult uwnd_step(UWND_Unwinder unwinder, Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, U64 lookup_pc, void *regs, U8 *reg_valid, U64 *cfa_out);

#endif // UNWIND_H
