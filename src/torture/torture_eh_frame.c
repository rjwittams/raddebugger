// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#undef T_Group
#define T_Group "EHFrame"

TEST(eh_frame_fde_augmentation_length)
{
  // A personality-bearing CIE and an FDE with an independently sized LSDA.
  U8 cie_bytes[] = {24,0,0,0, 0,0,0,0, 1,'z','P','L','R',0, 1,0x78,30,
                    7, 0x9b,0,0,0,0, 0x10,0x04, 0x0c,31,0};
  U8 fde_bytes[36] = {0};
  U64 pc = 0x1000, size = 0x100;
  MemoryCopy(fde_bytes+8, &pc, 8);
  MemoryCopy(fde_bytes+16, &size, 8);
  fde_bytes[24] = 8;
  fde_bytes[33] = 0x0c;
  EH_PtrCtx ptr_ctx = {0};
  DW_CIE cie = {0};
  DW_FDE fde = {0};
  U64 cie_off = eh_read_cie(str8_array_fixed(cie_bytes), 0, DW_Format_32Bit, Arch_arm64, 0, &ptr_ctx, &cie);
  T_Ok(cie_off == 25);
  T_Ok(cie.ext[EH_CIE_Ext_LSDAEnc] == 0x10);
  U64 fde_off = eh_read_fde(str8_array_fixed(fde_bytes), 0, DW_Format_32Bit, Arch_arm64, 0, &ptr_ctx, &cie, &fde);
  T_Ok(fde_off == 33);
  T_Ok(fde.pc_range.min == pc && fde.pc_range.max == pc+size);

  // A zero-length FDE augmentation still has its own length byte.
  fde_bytes[24] = 0;
  T_Ok(eh_read_fde(str8_array_fixed(fde_bytes), 0, DW_Format_32Bit, Arch_arm64, 0, &ptr_ctx, &cie, &fde) == 25);
}

TEST(eh_frame_caller_registers)
{
  Arch arches[] = {Arch_arm64, Arch_x64};
  for EachElement(arch_idx, arches)
  {
    Arch arch = arches[arch_idx];
    ARCH_Info *arch_info = arch_info_from_arch(arch);
    U8 ra = arch == Arch_arm64 ? 30 : 16;
    U8 fp = arch == Arch_arm64 ? 29 : 6;
    U8 sp = arch == Arch_arm64 ? 31 : 7;
    U8 bytes[] = {16,0,0,0, 0,0,0,0, 1,'z','R',0, 1,0x78,ra,1,4,0x0c,sp,0,
                  28,0,0,0, 24,0,0,0, 0,0,0,0,0,0,0,0, 0,1,0,0,0,0,0,0,
                  0, 0x0c,fp,16, (U8)(0x80|ra),1, (U8)(0x80|fp),2};
    U64 function_pc = 0x1000;
    MemoryCopy(bytes+28, &function_pc, 8);
    U64 stack_va = 0x2000;
    U64 stack[] = {0x2100, 0x3000};
    MemoryMap map = {0};
    memory_map_push(arena, &map, r1u64(0x4000, 0x4000+sizeof(bytes)), bytes);
    memory_map_push(arena, &map, r1u64(stack_va, stack_va+sizeof(stack)), stack);
    EH_UWND_ModuleUnwindInfo info = {0};
    info.override_fde_vaddr = 0x4000+20;
    UWND_ModuleInfo module = {.unwind_info = &info};
    void *regs = push_array(arena, U8, arch_info->reg_block_size);
    arch_reg_block_write_ip(arch_info, regs, function_pc+4);
    arch_reg_block_write_sp(arch_info, regs, stack_va-64);
    ARCH_RegCode fp_reg = arch_reg_code_from_dw(arch, fp);
    arch_reg_block_write_range(arch_info, regs, arch_info->reg_code_rng_table[fp_reg], &stack_va);
    U64 cfa = 0;
    UWND_StepResult step = eh_uwnd_step(arch, &map, &module, 0, regs, &cfa);
    T_Ok(step.status == UWND_StepStatus_Good);
    T_Ok(arch_ip_from_reg_block(arch_info, regs) == stack[1]);
    T_Ok(arch_sp_from_reg_block(arch_info, regs) == stack_va+16);
    T_Ok(cfa == stack_va+16);
  }
}
