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

TEST(eh_frame_register_availability_rules)
{
  Arch arches[] = {Arch_arm64, Arch_x64};
  for EachElement(arch_idx, arches) for(U64 scenario = 0; scenario < 10; scenario += 1)
  {
    Arch arch = arches[arch_idx];
    ARCH_Info *ai = arch_info_from_arch(arch);
    U8 ra = arch == Arch_arm64 ? 30 : 16, fp = arch == Arch_arm64 ? 29 : 6, sp = arch == Arch_arm64 ? 31 : 7;
    U8 preserved = arch == Arch_arm64 ? 19 : 3;
    U8 bytes[80] = {16,0,0,0, 0,0,0,0, 1,'z','R',0, 1,0x78,ra,1,4,0x0c,sp,0,
                    28,0,0,0, 24,0,0,0, 0,0,0,0,0,0,0,0, 0,1,0,0,0,0,0,0,
                    0,0x0c,fp,16,(U8)(0x80|ra),1,(U8)(0x80|fp),2};
    U8 rules[][8] = {
      {0x08,0},                         // same_value of an unknown volatile register
      {0x07,preserved},                 // explicitly undefined nonvolatile register
      {0x09,0,1,0x09,1,0},             // simultaneous register-to-register rules
      {0x80,2},                         // stack recovery of zero from CFA-16
      {0x16,0,1,0x51},                 // value expression using an unknown register
      {0x16,0,2,0x10,0},               // constant-zero value expression
      {0},                             // unavailable CFA input
      {0x05,arch == Arch_arm64 ? 72 : 23,2}, // d8 / xmm6; never the whole backing vector
      {0x16,0,3,0x71,0,0x06},          // memory miss at the final expression opcode
      {0x80,2,0x14,sp,0},              // signal context explicitly restores volatile x0/rax
    };
    U8 sizes[] = {2,2,6,2,4,5,0,3,6,5};
    MemoryCopy(bytes+52, rules[scenario], sizes[scenario]);
    bytes[20] += sizes[scenario];
    U64 pc = 0x1000, stack_va = 0x2000;
    MemoryCopy(bytes+28, &pc, 8);
    if(scenario == 9)
    {
      // Add S to zR, growing the CIE by one byte and adjusting the FDE link.
      MemoryCopy(bytes+12, bytes+11, sizeof(bytes)-12);
      bytes[11] = 'S';
      bytes[0] = 17;
      bytes[25] = 25;
    }
    U64 stack[] = {0, 0x3000};
    MemoryMap map = {0};
    memory_map_push(arena, &map, r1u64(0x4000, 0x4000+sizeof(bytes)), bytes);
    memory_map_push(arena, &map, r1u64(stack_va, stack_va+sizeof(stack)), stack);
    EH_UWND_ModuleUnwindInfo info = {.override_fde_vaddr = 0x4014 + (scenario == 9)};
    UWND_ModuleInfo module = {.unwind_info = &info};
    void *regs = push_array(arena, U8, ai->reg_block_size);
    U8 *valid = push_array(arena, U8, ai->reg_block_size);
    MemorySet(valid, 1, ai->reg_block_size);
    Rng1U16 r0 = ai->reg_code_rng_table[arch_reg_code_from_dw(arch, 0)];
    Rng1U16 r1 = ai->reg_code_rng_table[arch_reg_code_from_dw(arch, 1)];
    Rng1U16 rp = ai->reg_code_rng_table[arch_reg_code_from_dw(arch, preserved)];
    Rng1U16 rf = ai->reg_code_rng_table[arch_reg_code_from_dw(arch, fp)];
    U64 v0 = 1234, v1 = 5678;
    arch_reg_block_write_range(ai, regs, r0, &v0);
    arch_reg_block_write_range(ai, regs, r1, &v1);
    arch_reg_block_write_range(ai, regs, rp, &v0);
    arch_reg_block_write_range(ai, regs, rf, &stack_va);
    arch_reg_block_write_ip(ai, regs, pc+4);
    arch_reg_block_write_sp(ai, regs, stack_va);
    if(scenario == 0 || scenario == 2 || scenario == 3) {uwnd_reg_set_valid(valid, r0, 0);}
    if(scenario == 4) {uwnd_reg_set_valid(valid, r1, 0);}
    if(scenario == 6) {uwnd_reg_set_valid(valid, rf, 0);}
    U64 cfa = 0;
    UWND_StepResult step = eh_uwnd_step(arch, &map, &module, 0, regs, valid, &cfa);
    if(scenario == 6) {T_Ok(step.status == UWND_StepStatus_Error); continue;}
    if(scenario == 8)
    {
      T_Ok(step.status == UWND_StepStatus_FailedMemoryRead);
      T_Ok(step.missed_read_vaddr_range.min == 5678 && step.missed_read_vaddr_range.max == 5686);
      continue;
    }
    T_Ok(step.status == UWND_StepStatus_Good);
    T_Ok(arch_ip_from_reg_block(ai, regs) == 0x3000);
    T_Ok(uwnd_reg_range_is_valid(valid, rf)); // a recovered zero is known
    U64 got = 0;
    arch_reg_block_read_range(ai, regs, r0, &got);
    switch(scenario)
    {
      case 0: case 4: {T_Ok(!uwnd_reg_range_is_valid(valid, r0));}break;
      case 1:
      {
        T_Ok(!uwnd_reg_range_is_valid(valid, rp));
        arch_reg_block_read_range(ai, regs, rp, &got);
        T_Ok(got == 1234); // undefined is not a write of zero
      }break;
      case 2:
      {
        T_Ok(got == 5678 && uwnd_reg_range_is_valid(valid, r0));
        arch_reg_block_read_range(ai, regs, r1, &got);
        T_Ok(got == 1234 && !uwnd_reg_range_is_valid(valid, r1));
      }break;
      case 3: case 5: {T_Ok(got == 0 && uwnd_reg_range_is_valid(valid, r0));}break;
      case 7:
      {
        Rng1U16 q8 = ai->reg_code_rng_table[arch == Arch_arm64 ? ARM64_RegCode_q8 : X64_RegCode_zmm6];
        U64 known_size = arch == Arch_arm64 ? 8 : 16;
        T_Ok(uwnd_reg_range_is_valid(valid, r1u16(q8.min, q8.min+known_size)));
        T_Ok(!uwnd_reg_range_is_valid(valid, q8));
        T_Ok(!uwnd_reg_range_is_valid(valid, r1u16(q8.min+known_size, q8.max)));
      }break;
      case 9:
      {
        T_Ok(got == 0 && uwnd_reg_range_is_valid(valid, r0));
        T_Ok(!uwnd_reg_range_is_valid(valid, rp));
        if(arch == Arch_arm64) {T_Ok(uwnd_reg_is_valid(arch, valid, ARM64_RegCode_lr));}
      }break;
    }
  }
}

TEST(pe_leaf_register_availability)
{
  PE_X64_UWND_ModuleUnwindInfo info = {0};
  UWND_ModuleInfo module = {.unwind_info = &info, .os = OperatingSystem_Windows};
  U64 stack[] = {0x3000};
  MemoryMap map = {0};
  memory_map_push(arena, &map, r1u64(0x2000, 0x2008), stack);
  X64_RegBlock regs = {.rip = 0x1000, .rsp = 0x2000, .rax = 1234};
  U8 valid[sizeof(regs)];
  MemorySet(valid, 1, sizeof(valid));
  uwnd_reg_set_valid(valid, x64_reg_code_rng_table[X64_RegCode_rbx], 0);
  U64 cfa = 0;
  UWND_StepResult step = pe_x64_uwnd_step(Arch_x64, &map, &module, 0, &regs, valid, &cfa);
  T_Ok(step.status == UWND_StepStatus_Good && regs.rip == 0x3000);
  T_Ok(!uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_rax));
  T_Ok(!uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_rbx));
  T_Ok(uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_rbp));
  T_Ok(uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_rdi));
  T_Ok(uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_xmm6));
  T_Ok(!uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_ymm6));
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
    U8 *valid = push_array(arena, U8, arch_info->reg_block_size);
    MemorySet(valid, 1, arch_info->reg_block_size);
    arch_reg_block_write_ip(arch_info, regs, function_pc+4);
    arch_reg_block_write_sp(arch_info, regs, stack_va-64);
    ARCH_RegCode fp_reg = arch_reg_code_from_dw(arch, fp);
    arch_reg_block_write_range(arch_info, regs, arch_info->reg_code_rng_table[fp_reg], &stack_va);
    U64 cfa = 0;
    UWND_StepResult step = eh_uwnd_step(arch, &map, &module, 0, regs, valid, &cfa);
    T_Ok(step.status == UWND_StepStatus_Good);
    T_Ok(arch_ip_from_reg_block(arch_info, regs) == stack[1]);
    T_Ok(arch_sp_from_reg_block(arch_info, regs) == stack_va+16);
    T_Ok(cfa == stack_va+16);
    // An omitted volatile-register rule must not expose the callee's bytes.
    Rng1U16 volatile_rng = arch_info->reg_code_rng_table[arch_reg_code_from_dw(arch, 0)];
    T_Ok(!uwnd_reg_range_is_valid(valid, volatile_rng));
    T_Ok(uwnd_reg_range_is_valid(valid, arch_info->reg_code_rng_table[fp_reg]));
  }
}
