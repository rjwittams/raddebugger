// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef BN_ARM64_DISASM_H
#define BN_ARM64_DISASM_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t BN_ARM64_InstFlags;
enum
{
  BN_ARM64_InstFlag_Call              = (1u << 0),
  BN_ARM64_InstFlag_Branch            = (1u << 1),
  BN_ARM64_InstFlag_UnconditionalJump = (1u << 2),
  BN_ARM64_InstFlag_Return            = (1u << 3),
  BN_ARM64_InstFlag_NonFlow           = (1u << 4),
  BN_ARM64_InstFlag_ChangesStack      = (1u << 5),
  BN_ARM64_InstFlag_ChangesStackVar   = (1u << 6),
};

typedef uint32_t BN_ARM64_RegCode;
enum
{
  BN_ARM64_RegCode_Nil,
  BN_ARM64_RegCode_X0,
  BN_ARM64_RegCode_X1,
  BN_ARM64_RegCode_X2,
  BN_ARM64_RegCode_X3,
  BN_ARM64_RegCode_X4,
  BN_ARM64_RegCode_X5,
  BN_ARM64_RegCode_X6,
  BN_ARM64_RegCode_X7,
  BN_ARM64_RegCode_X8,
  BN_ARM64_RegCode_X9,
  BN_ARM64_RegCode_X10,
  BN_ARM64_RegCode_X11,
  BN_ARM64_RegCode_X12,
  BN_ARM64_RegCode_X13,
  BN_ARM64_RegCode_X14,
  BN_ARM64_RegCode_X15,
  BN_ARM64_RegCode_X16,
  BN_ARM64_RegCode_X17,
  BN_ARM64_RegCode_X18,
  BN_ARM64_RegCode_X19,
  BN_ARM64_RegCode_X20,
  BN_ARM64_RegCode_X21,
  BN_ARM64_RegCode_X22,
  BN_ARM64_RegCode_X23,
  BN_ARM64_RegCode_X24,
  BN_ARM64_RegCode_X25,
  BN_ARM64_RegCode_X26,
  BN_ARM64_RegCode_X27,
  BN_ARM64_RegCode_X28,
  BN_ARM64_RegCode_X29,
  BN_ARM64_RegCode_X30,
  BN_ARM64_RegCode_SP,
  BN_ARM64_RegCode_W0,
  BN_ARM64_RegCode_W1,
  BN_ARM64_RegCode_W2,
  BN_ARM64_RegCode_W3,
  BN_ARM64_RegCode_W4,
  BN_ARM64_RegCode_W5,
  BN_ARM64_RegCode_W6,
  BN_ARM64_RegCode_W7,
  BN_ARM64_RegCode_W8,
  BN_ARM64_RegCode_W9,
  BN_ARM64_RegCode_W10,
  BN_ARM64_RegCode_W11,
  BN_ARM64_RegCode_W12,
  BN_ARM64_RegCode_W13,
  BN_ARM64_RegCode_W14,
  BN_ARM64_RegCode_W15,
  BN_ARM64_RegCode_W16,
  BN_ARM64_RegCode_W17,
  BN_ARM64_RegCode_W18,
  BN_ARM64_RegCode_W19,
  BN_ARM64_RegCode_W20,
  BN_ARM64_RegCode_W21,
  BN_ARM64_RegCode_W22,
  BN_ARM64_RegCode_W23,
  BN_ARM64_RegCode_W24,
  BN_ARM64_RegCode_W25,
  BN_ARM64_RegCode_W26,
  BN_ARM64_RegCode_W27,
  BN_ARM64_RegCode_W28,
  BN_ARM64_RegCode_W29,
  BN_ARM64_RegCode_W30,
};

typedef struct BN_ARM64_DisasmResult BN_ARM64_DisasmResult;
struct BN_ARM64_DisasmResult
{
  int ok;
  BN_ARM64_InstFlags flags;
  uint64_t dst_vaddr;
  BN_ARM64_RegCode dst_reg_code;
  int64_t dst_reg_off;
  BN_ARM64_RegCode src_reg_code;
  int64_t src_reg_off;
};

int bn_arm64_disasm(uint32_t insword, uint64_t address, char *buffer, size_t buffer_size, BN_ARM64_DisasmResult *result_out);

#endif // BN_ARM64_DISASM_H
