// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#include "third_party/bn_arm64_disasm/bn_arm64_disasm.h"

#if defined(assert)
# pragma push_macro("assert")
# undef assert
# define BN_ARM64_RESTORE_ASSERT 1
#endif
#if defined(TRUE)
# pragma push_macro("TRUE")
# undef TRUE
# define BN_ARM64_RESTORE_TRUE 1
#endif
#if defined(FALSE)
# pragma push_macro("FALSE")
# undef FALSE
# define BN_ARM64_RESTORE_FALSE 1
#endif
#include "third_party/bn_arm64_disasm/bn_arm64_disasm_prefix.h"

#include "third_party/bn_arm64_disasm/pcode.c"
#include "third_party/bn_arm64_disasm/decode.c"
#include "third_party/bn_arm64_disasm/decode0.c"
#include "third_party/bn_arm64_disasm/decode1.c"
#include "third_party/bn_arm64_disasm/decode2.c"
#include "third_party/bn_arm64_disasm/decode_fields32.c"
#include "third_party/bn_arm64_disasm/decode_scratchpad.c"
#include "third_party/bn_arm64_disasm/encodings_dec.c"
#include "third_party/bn_arm64_disasm/operations.c"
#include "third_party/bn_arm64_disasm/sysregs_gen.c"
#include "third_party/bn_arm64_disasm/sysregs_fmt_gen.c"
#include "third_party/bn_arm64_disasm/regs.c"
#include "third_party/bn_arm64_disasm/format.c"

#undef assert
#undef TRUE
#undef FALSE
#if defined(BN_ARM64_RESTORE_ASSERT)
# pragma pop_macro("assert")
# undef BN_ARM64_RESTORE_ASSERT
#endif
#if defined(BN_ARM64_RESTORE_TRUE)
# pragma pop_macro("TRUE")
# undef BN_ARM64_RESTORE_TRUE
#endif
#if defined(BN_ARM64_RESTORE_FALSE)
# pragma pop_macro("FALSE")
# undef BN_ARM64_RESTORE_FALSE
#endif

static BN_ARM64_RegCode
bn_arm64_reg_code_from_register(Register reg)
{
  BN_ARM64_RegCode result = BN_ARM64_RegCode_Nil;
  if(REG_X0 <= reg && reg <= REG_X30)
  {
    result = (BN_ARM64_RegCode)(BN_ARM64_RegCode_X0 + (reg - REG_X0));
  }
  else if(REG_W0 <= reg && reg <= REG_W30)
  {
    result = (BN_ARM64_RegCode)(BN_ARM64_RegCode_W0 + (reg - REG_W0));
  }
  else if(reg == REG_SP || reg == REG_WSP)
  {
    result = BN_ARM64_RegCode_SP;
  }
  return result;
}

static int
bn_arm64_operand_is_memory(const InstructionOperand *operand)
{
  int result = (operand->operandClass == MEM_REG ||
                operand->operandClass == MEM_PRE_IDX ||
                operand->operandClass == MEM_POST_IDX ||
                operand->operandClass == MEM_OFFSET ||
                operand->operandClass == MEM_EXTENDED);
  return result;
}

static int
bn_arm64_operand_memory_address_is_fixed(const InstructionOperand *operand)
{
  int result = (operand->operandClass == MEM_PRE_IDX ||
                operand->operandClass == MEM_POST_IDX ||
                operand->operandClass == MEM_OFFSET);
  return result;
}

static int64_t
bn_arm64_memory_effective_offset_from_operand(const InstructionOperand *operand)
{
  int64_t result = (int64_t)operand->immediate;
  if(operand->operandClass == MEM_POST_IDX)
  {
    result = 0;
  }
  return result;
}

static int
bn_arm64_operation_is_store(Operation operation)
{
  int result = (ARM64_ST1 <= operation && operation <= ARM64_STZGM);
  return result;
}

static int
bn_arm64_operation_has_no_dst_reg(Operation operation)
{
  int result = 0;
  switch(operation)
  {
    case ARM64_CCMN:
    case ARM64_CCMP:
    case ARM64_CMN:
    case ARM64_CMP:
    case ARM64_FCMP:
    case ARM64_FCMPE:
    case ARM64_TST:
    {
      result = 1;
    }break;

    default:{}break;
  }
  return result;
}

static BN_ARM64_InstFlags
bn_arm64_inst_flags_from_operation(Operation operation)
{
  BN_ARM64_InstFlags result = BN_ARM64_InstFlag_NonFlow;
  switch(operation)
  {
    case ARM64_BL:
    case ARM64_BLR:
    case ARM64_BLRAA:
    case ARM64_BLRAAZ:
    case ARM64_BLRAB:
    case ARM64_BLRABZ:
    {
      result = BN_ARM64_InstFlag_Call;
    }break;

    case ARM64_B:
    case ARM64_BR:
    case ARM64_BRAA:
    case ARM64_BRAAZ:
    case ARM64_BRAB:
    case ARM64_BRABZ:
    {
      result = BN_ARM64_InstFlag_UnconditionalJump;
    }break;

    case ARM64_B_EQ:
    case ARM64_B_NE:
    case ARM64_B_CS:
    case ARM64_B_CC:
    case ARM64_B_MI:
    case ARM64_B_PL:
    case ARM64_B_VS:
    case ARM64_B_VC:
    case ARM64_B_HI:
    case ARM64_B_LS:
    case ARM64_B_GE:
    case ARM64_B_LT:
    case ARM64_B_GT:
    case ARM64_B_LE:
    case ARM64_B_AL:
    case ARM64_B_NV:
    case ARM64_CBZ:
    case ARM64_CBNZ:
    case ARM64_TBZ:
    case ARM64_TBNZ:
    {
      result = BN_ARM64_InstFlag_Branch;
    }break;

    case ARM64_RET:
    case ARM64_RETAA:
    case ARM64_RETAB:
    case ARM64_ERET:
    case ARM64_ERETAA:
    case ARM64_ERETAB:
    case ARM64_DRPS:
    {
      result = BN_ARM64_InstFlag_Return;
    }break;

    default:{}break;
  }
  return result;
}

int
bn_arm64_disasm(uint32_t insword, uint64_t address, char *buffer, size_t buffer_size, BN_ARM64_DisasmResult *result_out)
{
  int ok = 0;
  BN_ARM64_DisasmResult result = {0};
  Instruction instruction = {0};

  if(buffer != 0 && buffer_size != 0 && aarch64_decompose(insword, &instruction, address) == DECODE_STATUS_OK)
  {
    if(aarch64_disassemble(&instruction, buffer, buffer_size) == DISASM_SUCCESS)
    {
      ok = 1;
      result.ok = 1;
      result.flags = bn_arm64_inst_flags_from_operation(instruction.operation);
      int is_flow = !(result.flags & BN_ARM64_InstFlag_NonFlow);
      int is_store = bn_arm64_operation_is_store(instruction.operation);
      int has_no_dst_reg = bn_arm64_operation_has_no_dst_reg(instruction.operation);
      int has_memory = 0;
      for(size_t idx = 0; idx < MAX_OPERANDS; idx += 1)
      {
        if(bn_arm64_operand_is_memory(&instruction.operands[idx]))
        {
          has_memory = 1;
          break;
        }
      }

      for(size_t idx = 0; idx < MAX_OPERANDS; idx += 1)
      {
        InstructionOperand *operand = &instruction.operands[idx];
        if(operand->operandClass == LABEL)
        {
          if(is_flow)
          {
            result.dst_vaddr = operand->immediate;
          }
        }
        else if(operand->operandClass == REG)
        {
          BN_ARM64_RegCode reg_code = bn_arm64_reg_code_from_register(operand->reg[0]);
          if(is_flow)
          {
            if(result.src_reg_code == BN_ARM64_RegCode_Nil)
            {
              result.src_reg_code = reg_code;
            }
          }
          else if(has_no_dst_reg)
          {
            if(result.src_reg_code == BN_ARM64_RegCode_Nil)
            {
              result.src_reg_code = reg_code;
            }
          }
          else if(is_store)
          {
            if(result.src_reg_code == BN_ARM64_RegCode_Nil)
            {
              result.src_reg_code = reg_code;
            }
          }
          else if(result.dst_reg_code == BN_ARM64_RegCode_Nil)
          {
            result.dst_reg_code = reg_code;
          }
          else if(!has_memory && result.src_reg_code == BN_ARM64_RegCode_Nil)
          {
            result.src_reg_code = reg_code;
          }
        }
        else if(bn_arm64_operand_is_memory(operand))
        {
          BN_ARM64_RegCode base_reg_code = bn_arm64_reg_code_from_register(operand->reg[0]);
          int64_t off = bn_arm64_memory_effective_offset_from_operand(operand);
          if(bn_arm64_operand_memory_address_is_fixed(operand))
          {
            if(is_flow)
            {
              if(result.src_reg_code == BN_ARM64_RegCode_Nil)
              {
                result.src_reg_code = base_reg_code;
                result.src_reg_off = off;
              }
            }
            else if(is_store)
            {
              if(result.dst_reg_code == BN_ARM64_RegCode_Nil)
              {
                result.dst_reg_code = base_reg_code;
                result.dst_reg_off = off;
              }
            }
            else if(result.src_reg_code == BN_ARM64_RegCode_Nil)
            {
              result.src_reg_code = base_reg_code;
              result.src_reg_off = off;
            }
          }
          if((operand->operandClass == MEM_PRE_IDX || operand->operandClass == MEM_POST_IDX) &&
             base_reg_code == BN_ARM64_RegCode_SP)
          {
            result.flags |= BN_ARM64_InstFlag_ChangesStack;
          }
        }
      }
      if(!is_store && result.dst_reg_code == BN_ARM64_RegCode_SP)
      {
        result.flags |= BN_ARM64_InstFlag_ChangesStack;
        int is_fixed_sp_adjust = ((instruction.operation == ARM64_ADD || instruction.operation == ARM64_SUB) &&
                                  instruction.operands[0].operandClass == REG &&
                                  instruction.operands[1].operandClass == REG &&
                                  bn_arm64_reg_code_from_register(instruction.operands[1].reg[0]) == BN_ARM64_RegCode_SP &&
                                  (instruction.operands[2].operandClass == IMM32 ||
                                   instruction.operands[2].operandClass == IMM64));
        if(!is_fixed_sp_adjust)
        {
          result.flags |= BN_ARM64_InstFlag_ChangesStackVar;
        }
      }
    }
  }

  if(result_out != 0)
  {
    *result_out = result;
  }

  return ok;
}

#define BN_ARM64_DISASM_PREFIX_UNDEF
#include "third_party/bn_arm64_disasm/bn_arm64_disasm_prefix.h"
#undef BN_ARM64_DISASM_PREFIX_UNDEF
