// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

//- GENERATED CODE

#ifndef ARM64_META_H
#define ARM64_META_H

typedef enum ARM64_RegCode
{
ARM64_RegCode_nil,
ARM64_RegCode_x0,
ARM64_RegCode_x1,
ARM64_RegCode_x2,
ARM64_RegCode_x3,
ARM64_RegCode_x4,
ARM64_RegCode_x5,
ARM64_RegCode_x6,
ARM64_RegCode_x7,
ARM64_RegCode_x8,
ARM64_RegCode_x9,
ARM64_RegCode_x10,
ARM64_RegCode_x11,
ARM64_RegCode_x12,
ARM64_RegCode_x13,
ARM64_RegCode_x14,
ARM64_RegCode_x15,
ARM64_RegCode_x16,
ARM64_RegCode_x17,
ARM64_RegCode_x18,
ARM64_RegCode_x19,
ARM64_RegCode_x20,
ARM64_RegCode_x21,
ARM64_RegCode_x22,
ARM64_RegCode_x23,
ARM64_RegCode_x24,
ARM64_RegCode_x25,
ARM64_RegCode_x26,
ARM64_RegCode_x27,
ARM64_RegCode_x28,
ARM64_RegCode_fp,
ARM64_RegCode_lr,
ARM64_RegCode_sp,
ARM64_RegCode_pc,
ARM64_RegCode_cpsr,
ARM64_RegCode_x29,
ARM64_RegCode_x30,
ARM64_RegCode_w0,
ARM64_RegCode_w1,
ARM64_RegCode_w2,
ARM64_RegCode_w3,
ARM64_RegCode_w4,
ARM64_RegCode_w5,
ARM64_RegCode_w6,
ARM64_RegCode_w7,
ARM64_RegCode_w8,
ARM64_RegCode_w9,
ARM64_RegCode_w10,
ARM64_RegCode_w11,
ARM64_RegCode_w12,
ARM64_RegCode_w13,
ARM64_RegCode_w14,
ARM64_RegCode_w15,
ARM64_RegCode_w16,
ARM64_RegCode_w17,
ARM64_RegCode_w18,
ARM64_RegCode_w19,
ARM64_RegCode_w20,
ARM64_RegCode_w21,
ARM64_RegCode_w22,
ARM64_RegCode_w23,
ARM64_RegCode_w24,
ARM64_RegCode_w25,
ARM64_RegCode_w26,
ARM64_RegCode_w27,
ARM64_RegCode_w28,
ARM64_RegCode_w29,
ARM64_RegCode_w30,
ARM64_RegCode_COUNT,
} ARM64_RegCode;

typedef struct ARM64_RegBlock ARM64_RegBlock;
struct ARM64_RegBlock
{
U64 x0;
U64 x1;
U64 x2;
U64 x3;
U64 x4;
U64 x5;
U64 x6;
U64 x7;
U64 x8;
U64 x9;
U64 x10;
U64 x11;
U64 x12;
U64 x13;
U64 x14;
U64 x15;
U64 x16;
U64 x17;
U64 x18;
U64 x19;
U64 x20;
U64 x21;
U64 x22;
U64 x23;
U64 x24;
U64 x25;
U64 x26;
U64 x27;
U64 x28;
U64 fp;
U64 lr;
U64 sp;
U64 pc;
U32 cpsr;
};

C_LINKAGE_BEGIN
extern B8 arm64_reg_code_is_vector_table[68];
extern String8 arm64_reg_code_name_table[68];
extern U8 arm64_reg_code_base_table[68];
extern Rng1U16 arm64_reg_code_rng_table[68];

C_LINKAGE_END

#endif // ARM64_META_H
