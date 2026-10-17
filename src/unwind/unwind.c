// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Generated Code

#include "unwind/generated/unwind.meta.c"

internal B32
uwnd_reg_range_is_valid(U8 *valid, Rng1U16 range)
{
  if(range.min >= range.max) {return 0;}
  if(valid != 0) for(U64 i = range.min; i < range.max; i += 1)
  {
    if(!valid[i]) {return 0;}
  }
  return 1;
}

internal void
uwnd_reg_set_valid(U8 *valid, Rng1U16 range, B32 value)
{
  if(valid != 0) {MemorySet(valid + range.min, !!value, dim_1u16(range));}
}

internal B32
uwnd_reg_is_valid(Arch arch, U8 *valid, U8 code)
{
  return uwnd_reg_range_is_valid(valid, arch_info_from_arch(arch)->reg_code_rng_table[code]);
}

internal void
uwnd_reg_mark_valid(Arch arch, U8 *valid, U8 code, U64 size)
{
  Rng1U16 range = arch_info_from_arch(arch)->reg_code_rng_table[code];
  if(size != 0) {range.max = range.min + Min(dim_1u16(range), size);}
  uwnd_reg_set_valid(valid, range, 1);
}

// These are the bytes an ordinary call's unwind row may leave unchanged.
// Explicit saves can recover additional bytes, including previously unknown
// ones. This is not a rule for guessing registers from a bare frame chain.
internal void
uwnd_reg_valid_from_abi(Arch arch, OperatingSystem os, U8 *src, U8 *dst)
{
  ARCH_Info *info = arch_info_from_arch(arch);
  MemoryZero(dst, info->reg_block_size);
  ARCH_RegCode codes[32] = {0};
  U64 count = 0;
  if(arch == Arch_arm64)
  {
    for(U32 r = ARM64_RegCode_x19; r <= ARM64_RegCode_fp; r += 1) {codes[count++] = r;}
    if(os == OperatingSystem_Mac) {codes[count++] = ARM64_RegCode_x18;}
    for(U32 r = ARM64_RegCode_q8; r <= ARM64_RegCode_q15; r += 1)
    {
      Rng1U16 range = info->reg_code_rng_table[r];
      range.max = range.min + 8;
      if(src) {MemoryCopy(dst+range.min, src+range.min, 8);}
      else {uwnd_reg_set_valid(dst, range, 1);}
    }
  }
  else if(arch == Arch_x64)
  {
    codes[count++] = X64_RegCode_rbx;
    codes[count++] = X64_RegCode_rbp;
    for(U32 r = X64_RegCode_r12; r <= X64_RegCode_r15; r += 1) {codes[count++] = r;}
    if(os == OperatingSystem_Windows)
    {
      codes[count++] = X64_RegCode_rdi;
      codes[count++] = X64_RegCode_rsi;
      for(U32 r = X64_RegCode_xmm6; r <= X64_RegCode_xmm15; r += 1) {codes[count++] = r;}
    }
  }
  codes[count++] = info->stack_pointer_reg_code;
  for(U64 i = 0; i < count; i += 1)
  {
    Rng1U16 range = info->reg_code_rng_table[codes[i]];
    if(src) {MemoryCopy(dst+range.min, src+range.min, dim_1u16(range));}
    else {uwnd_reg_set_valid(dst, range, 1);}
  }
}

////////////////////////////////
//~ rjf: Moduleless Unwinder

internal UWND_StepResult
uwnd_step_moduleless(Arch arch, MemoryMap *memory_map, void *regs, U8 *reg_valid)
{
  UWND_StepResult result = {UWND_StepStatus_Error};
  {
    B32 done = 0;
    ARCH_Info *arch_info = arch_info_from_arch(arch);
    U64 addr_size = byte_size_from_arch(arch);
    if(!uwnd_reg_range_is_valid(reg_valid, arch_info->reg_code_rng_table[arch_info->stack_pointer_reg_code])) {return result;}
    
    // rjf: read new ip from stack pointer
    U64 sp = arch_sp_from_reg_block(arch_info, regs);
    U64 new_ip = 0;
    Rng1U64 new_ip_vaddr_range = r1u64(sp, sp+addr_size);
    if(memory_map_read(memory_map, new_ip_vaddr_range, &new_ip) != addr_size)
    {
      done = 1;
      result.status = UWND_StepStatus_FailedMemoryRead;
      result.missed_read_vaddr_range = new_ip_vaddr_range;
    }
    
    // rjf: commit
    if(!done)
    {
      U64 new_sp = sp+addr_size;
      arch_reg_block_write_ip(arch_info, regs, new_ip);
      arch_reg_block_write_sp(arch_info, regs, new_sp);
      uwnd_reg_set_valid(reg_valid, arch_info->reg_code_rng_table[arch_info->instruction_pointer_reg_code], 1);
      result.status = UWND_StepStatus_Good;
    }
  }
  return result;
}

////////////////////////////////
//~ rjf: Abstracted Unwind Step Functions

internal UWND_StepResult
uwnd_step(UWND_Unwinder unwinder, Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, void *regs, U8 *reg_valid, U64 *cfa_out)
{
  UWND_StepResult result = {UWND_StepStatus_Error};
  switch(unwinder)
  {
    default:
    {
      result = uwnd_step_moduleless(arch, memory_map, regs, reg_valid);
      if(result.status == UWND_StepStatus_Good && reg_valid)
      {
        ARCH_Info *info = arch_info_from_arch(arch);
        MemoryZero(reg_valid, info->reg_block_size);
        uwnd_reg_mark_valid(arch, reg_valid, info->instruction_pointer_reg_code, 0);
        uwnd_reg_mark_valid(arch, reg_valid, info->stack_pointer_reg_code, 0);
      }
    }break;
#define X(name, namespace_lower) case UWND_Unwinder_##name:{result = namespace_lower##_uwnd_step(arch, memory_map, module_info, tls_vaddr, regs, reg_valid, cfa_out);}break;
    UWND_Unwinder_XList
#undef X
  }
  return result;
}
