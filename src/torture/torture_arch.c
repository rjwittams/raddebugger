// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define T_Group "Arch"

TEST(arm64_metadata)
{
  ARCH_Info *info = arch_info_from_arch(Arch_arm64);
  T_Ok(info != &arch_info_nil);
  T_Ok(info->reg_block_size == sizeof(ARM64_RegBlock));
  T_Ok(info->instruction_pointer_reg_code == ARM64_RegCode_pc);
  T_Ok(info->stack_pointer_reg_code == ARM64_RegCode_sp);
  T_Ok(info->reg_code_count == ARM64_RegCode_COUNT);
  T_Ok(info->trap_instruction.size == 4);
  T_Ok(((U8 *)info->trap_instruction.str)[0] == 0x00);
  T_Ok(((U8 *)info->trap_instruction.str)[1] == 0x00);
  T_Ok(((U8 *)info->trap_instruction.str)[2] == 0x20);
  T_Ok(((U8 *)info->trap_instruction.str)[3] == 0xd4);
  T_Ok(max_ops_per_instruction_from_arch(Arch_arm64) == 1);
  T_Ok(min_instruction_size_from_arch(Arch_arm64) == 4);
  T_Ok(max_instruction_size_from_arch(Arch_arm64) == 4);
  
  ARM64_RegBlock regs = {0};
  regs.pc = 0x1234567887654321ull;
  regs.sp = 0x100000000ull;
  T_Ok(arch_ip_from_reg_block(info, &regs) == regs.pc);
  T_Ok(arch_sp_from_reg_block(info, &regs) == regs.sp);
  
  U64 new_ip = 0x200000000ull;
  U64 new_sp = 0x300000000ull;
  T_Ok(arch_reg_block_write_ip(info, &regs, new_ip));
  T_Ok(arch_reg_block_write_sp(info, &regs, new_sp));
  T_Ok(regs.pc == new_ip);
  T_Ok(regs.sp == new_sp);
}

TEST(arm64_rdi_dwarf_mappings)
{
  T_Ok(rdi_arch_from_arch(Arch_arm64) == RDI_Arch_ARM64);
  T_Ok(arch_from_rdi_arch(RDI_Arch_ARM64) == Arch_arm64);
  
  ARCH_Info *info = arch_info_from_arch(Arch_arm64);
  T_Ok(arch_reg_code_from_name(info, str8_lit("pc")) == ARM64_RegCode_pc);
  T_Ok(arch_reg_code_from_rdi(Arch_arm64, RDI_RegCodeARM64_pc) == ARM64_RegCode_pc);
  T_Ok(arch_reg_code_from_dw(Arch_arm64, DW_RegARM64_Pc) == ARM64_RegCode_pc);
  
  T_Ok(dw_reg_size_from_code(Arch_arm64, DW_RegARM64_X0) == 8);
  T_Ok(dw_reg_size_from_code(Arch_arm64, DW_RegARM64_Pc) == 8);
  T_Ok(dw_reg_pos_from_code(Arch_arm64, DW_RegARM64_Pc) == 0);
  T_Ok(dw_sp_from_arch(Arch_arm64) == DW_RegARM64_Sp);
  T_Ok(d2r_rdi_reg_code_from_dw_reg(Arch_arm64, DW_RegARM64_Pc) == RDI_RegCodeARM64_pc);
  T_Ok(str8_match(rdi_string_from_reg_code(arena, RDI_Arch_ARM64, RDI_RegCodeARM64_pc), str8_lit("pc"), 0));
}
