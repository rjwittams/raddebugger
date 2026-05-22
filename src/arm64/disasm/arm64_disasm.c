// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#if !defined(BN_ARM64_DISASM_H)
# include "third_party/bn_arm64_disasm/bn_arm64_disasm.h"
# include "third_party/bn_arm64_disasm/bn_arm64_disasm.c"
#endif

internal ARM64_RegCode
arm64_reg_code_from_bn_arm64_reg_code(BN_ARM64_RegCode reg_code)
{
  ARM64_RegCode result = ARM64_RegCode_nil;
  if(BN_ARM64_RegCode_X0 <= reg_code && reg_code <= BN_ARM64_RegCode_X28)
  {
    result = (ARM64_RegCode)(ARM64_RegCode_x0 + (reg_code - BN_ARM64_RegCode_X0));
  }
  else if(reg_code == BN_ARM64_RegCode_X29)
  {
    result = ARM64_RegCode_x29;
  }
  else if(reg_code == BN_ARM64_RegCode_X30)
  {
    result = ARM64_RegCode_x30;
  }
  else if(reg_code == BN_ARM64_RegCode_SP)
  {
    result = ARM64_RegCode_sp;
  }
  else if(BN_ARM64_RegCode_W0 <= reg_code && reg_code <= BN_ARM64_RegCode_W30)
  {
    result = (ARM64_RegCode)(ARM64_RegCode_w0 + (reg_code - BN_ARM64_RegCode_W0));
  }
  return result;
}

internal DASM_Inst
arm64_dasm_inst_from_code(Arena *arena, U64 vaddr, String8 code, DASM_Syntax syntax)
{
  ProfBeginFunction();
  DASM_Inst inst = {0};
  (void)syntax;

  if(code.size >= 4)
  {
    U32 insword = ((U32)code.str[0] << 0 |
                   (U32)code.str[1] << 8 |
                   (U32)code.str[2] << 16 |
                   (U32)code.str[3] << 24);

    char buffer[256] = {0};
    BN_ARM64_DisasmResult result = {0};
    if(bn_arm64_disasm(insword, vaddr, buffer, sizeof(buffer), &result))
    {
      inst.size = 4;
      inst.string = str8_copy(arena, str8_cstring(buffer));
      inst.dst_vaddr = result.dst_vaddr;
      inst.dst_reg_code = arm64_reg_code_from_bn_arm64_reg_code(result.dst_reg_code);
      inst.dst_reg_off = result.dst_reg_off;
      inst.src_reg_code = arm64_reg_code_from_bn_arm64_reg_code(result.src_reg_code);
      inst.src_reg_off = result.src_reg_off;
      if(result.flags & BN_ARM64_InstFlag_Call)
      {
        inst.flags |= DASM_InstFlag_Call;
      }
      if(result.flags & BN_ARM64_InstFlag_Branch)
      {
        inst.flags |= DASM_InstFlag_Branch;
      }
      if(result.flags & BN_ARM64_InstFlag_UnconditionalJump)
      {
        inst.flags |= DASM_InstFlag_UnconditionalJump;
      }
      if(result.flags & BN_ARM64_InstFlag_Return)
      {
        inst.flags |= DASM_InstFlag_Return;
      }
      if(result.flags & BN_ARM64_InstFlag_NonFlow)
      {
        inst.flags |= DASM_InstFlag_NonFlow;
      }
      if(result.flags & BN_ARM64_InstFlag_ChangesStack)
      {
        inst.flags |= DASM_InstFlag_ChangesStackPointer;
      }
      if(result.flags & BN_ARM64_InstFlag_ChangesStackVar)
      {
        inst.flags |= DASM_InstFlag_ChangesStackPointerVariably;
      }
    }
  }

  ProfEnd();
  return inst;
}
