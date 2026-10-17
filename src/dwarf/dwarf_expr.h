// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef DWARF_EXPR_H
#define DWARF_EXPR_H

////////////////////////////////
//~ rjf: Evaluation Values

typedef struct DW_EvalVal DW_EvalVal;
struct DW_EvalVal
{
  union
  {
    U512 u512;
    S64 s64;
  };
  String8 data;
};

typedef struct DW_EvalValNode DW_EvalValNode;
struct DW_EvalValNode
{
  DW_EvalValNode *next;
  DW_EvalVal v;
};

////////////////////////////////
//~ rjf: Evaluation Results

typedef enum DW_EvalStatus
{
  DW_EvalStatus_Error,
  DW_EvalStatus_FailedMemoryRead,
  DW_EvalStatus_UnavailableRegister,
  DW_EvalStatus_ExecOpLimitReached,
  DW_EvalStatus_UnsupportedOp,
  DW_EvalStatus_Good,
}
DW_EvalStatus;

typedef struct DW_Eval DW_Eval;
struct DW_Eval
{
  DW_EvalVal val;
  DW_EvalStatus status;
  Rng1U64 missed_read_vaddr_range;
};

////////////////////////////////
//~ rjf: Evaluation State Machine

typedef struct DW_EvalState DW_EvalState;
struct DW_EvalState
{
  U8 *reg_valid;
  B32 unwind_register_sizes;
  U64 off;
  U64 op_idx;
  DW_EvalValNode *top_val;
  DW_EvalValNode *free_val;
};

////////////////////////////////
//~ rjf: Expression Evaluation Functions

internal Rng1U16 dw_reg_range_from_code(Arch arch, DW_RegCode code, B32 unwind);
internal DW_Eval dw_eval(Arena *arena, DW_Format fmt, Arch arch, MemoryMap *memory_map, void *regs, U64 frame_base, U64 cfa, U64 tls, DW_EvalState *state, String8 expr, U64 op_idx_cap);

#endif // DWARF_EXPR_H
