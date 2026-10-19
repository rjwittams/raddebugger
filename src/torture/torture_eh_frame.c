// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#undef T_Group
#define T_Group "EHFrame"

TEST(eh_frame_header_optional_table)
{
  // Zig 0.16's Linux header has a relative pointer and omits both table fields.
  U8 zig_bytes[] = {1,0x1b,0xff,0xff, 0x7c,0x43,0xfe,0xff};
  EH_PtrCtx ptr_ctx = {.pc_vaddr = 0x1023e48};
  EH_FrameHdr header = eh_parse_frame_hdr(str8_array_fixed(zig_bytes), 8, &ptr_ctx);
  T_Ok(header.eh_frame_ptr == 0x10081c8);
  T_Ok(header.version == 1 && header.fde_count == 0 && header.table.size == 0);
  T_Ok(header.field_byte_size == 0 && header.entry_byte_size == 0);

  for(U64 scenario = 0; scenario < 5; scenario += 1)
  {
    U8 bytes[] = {1,3,3,3, 0,0x10,0,0, 1,0,0,0, 0,0x20,0,0, 0,0x30,0,0};
    U64 size = sizeof(bytes);
    if(scenario == 1 || scenario == 3) {bytes[2] = EH_PtrEnc_Omit; size = 8;}
    if(scenario == 2 || scenario == 3) {bytes[3] = EH_PtrEnc_Omit; size = scenario == 2 ? 12 : 8;}
    if(scenario == 4) {bytes[8] = 0; size = 12;}
    EH_PtrCtx ptr_ctx = {0};
    EH_FrameHdr header = eh_parse_frame_hdr(str8(bytes, size), 8, &ptr_ctx);
    T_Ok(header.version == 1 && header.eh_frame_ptr == 0x1000);
    T_Ok(header.fde_count == (scenario == 0 ? 1 : 0));
    T_Ok(header.table.size == (scenario == 0 ? 8 : 0));
    T_Ok(header.entry_byte_size == (scenario == 0 || scenario == 4 ? 8 : 0));
  }
}

TEST(eh_frame_cie_versions)
{
  for(U64 version = 1; version <= 3; version += 2)
  {
    for(U64 large_reg = 0; large_reg <= 1; large_reg += 1)
    {
      U8 bytes[21] = {0,0,0,0, 0,0,0,0, 0,'z','R',0, 1,0x78};
      U64 off = 14;
      bytes[8] = version;
      bytes[off++] = large_reg ? 0x82 : 30;
      if(version == 3 && large_reg) {bytes[off++] = 1;}
      U64 aug_off = off;
      bytes[off++] = 1;
      bytes[off++] = EH_PtrEnc_UData8;
      U64 inst_off = off;
      bytes[off++] = 0x0c; bytes[off++] = 31; bytes[off++] = 0;
      bytes[0] = off-4;
      EH_PtrCtx ptr_ctx = {0};
      DW_CIE cie = {0};
      T_Ok(eh_read_cie(str8(bytes, off), 0, DW_Format_32Bit, Arch_arm64, 0, &ptr_ctx, &cie) == inst_off);
      T_Ok(cie.version == version && cie.address_size == 8);
      T_Ok(cie.code_align_factor == 1 && cie.data_align_factor == -8);
      T_Ok(cie.ret_addr_reg == (large_reg ? 130 : 30));
      T_Ok(cie.aug_string_range.min == 9 && cie.aug_string_range.max == 12);
      T_Ok(cie.aug_data_range.min == aug_off && cie.aug_data_range.max == inst_off);
      T_Ok(cie.ext[EH_CIE_Ext_AddrEnc] == EH_PtrEnc_UData8);
    }
  }
  U8 unsupported_versions[] = {0,2,4,255};
  for(U64 i = 0; i < ArrayCount(unsupported_versions); i += 1)
  {
    U8 bytes[] = {16,0,0,0, 0,0,0,0, 0,'z','R',0, 1,0x78,30,1,4,0x0c,31,0};
    bytes[8] = unsupported_versions[i];
    EH_PtrCtx ptr_ctx = {0};
    DW_CIE cie = {0};
    T_Ok(eh_read_cie(str8_array_fixed(bytes), 0, DW_Format_32Bit, Arch_arm64, 0, &ptr_ctx, &cie) == 0);
    T_Ok(cie.version == 0 && cie.ext[EH_CIE_Ext_AddrEnc] == EH_PtrEnc_Omit);
  }
}

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

TEST(eh_frame_index_termination)
{
  U8 bytes[88] = {16,0,0,0, 0,0,0,0, 1,'z','R',0, 1,0x78,16,1,4,0x0c,7,8,
                  28,0,0,0, 24,0,0,0, 0,0x10,0,0,0,0,0,0, 0,1,0,0,0,0,0,0,
                  0,0x0c,6,16,0x90,1,0x86,2};
  // A second valid-looking FDE after the terminator must not enter the index.
  MemoryCopy(bytes+56, bytes+20, 32);
  bytes[60] = 60;
  bytes[65] = 0x20;
  EH_PtrCtx ptr_ctx = {0};
  for(B32 require_terminator = 0; require_terminator <= 1; require_terminator += 1)
  {
    String8 data = d_eh_frame_hdr_from_eh_frame(arena, str8_array_fixed(bytes), 0x4000, Arch_x64, &ptr_ctx, require_terminator);
    U8 expected[] = {1,0xff,4,4, 1,0,0,0,0,0,0,0,
                     0,0x10,0,0,0,0,0,0, 0x14,0x40,0,0,0,0,0,0};
    T_Ok(str8_match(data, str8_array_fixed(expected), 0));
    EH_FrameHdr header = eh_parse_frame_hdr(data, 8, &ptr_ctx);
    T_Ok(header.fde_count == 1);
    U64 pc = 0, fde = 0;
    eh_read_ptr(header.table, 0, 0, &ptr_ctx, header.table_enc, &pc);
    eh_read_ptr(header.table, 8, 8, &ptr_ctx, header.table_enc, &fde);
    T_Ok(pc == 0x1000 && fde == 0x4014);
  }
  // Exact Mach-O section extents do not require an ELF-style terminator.
  T_Ok(d_eh_frame_hdr_from_eh_frame(arena, str8(bytes, 52), 0x4000, Arch_x64, &ptr_ctx, 0).size != 0);
  T_Ok(d_eh_frame_hdr_from_eh_frame(arena, str8(bytes, 52), 0x4000, Arch_x64, &ptr_ctx, 1).size == 0);
  T_Ok(d_eh_frame_hdr_from_eh_frame(arena, str8(bytes, 54), 0x4000, Arch_x64, &ptr_ctx, 1).size == 0);
  bytes[20] = 255; // FDE extends beyond available data.
  T_Ok(d_eh_frame_hdr_from_eh_frame(arena, str8_array_fixed(bytes), 0x4000, Arch_x64, &ptr_ctx, 1).size == 0);
  bytes[20] = 28;
  bytes[24] = 255; // CIE pointer precedes the section.
  T_Ok(d_eh_frame_hdr_from_eh_frame(arena, str8(bytes, 56), 0x4000, Arch_x64, &ptr_ctx, 1).size == 0);
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
    UWND_StepResult step = eh_uwnd_step(arch, &map, &module, 0, arch_ip_from_reg_block(arch_info_from_arch(arch), regs), regs, valid, &cfa);
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
        T_Ok(step.is_interrupted);
        T_Ok(uwnd_lookup_vaddr_from_pc(arch, arch_ip_from_reg_block(ai, regs), !step.is_interrupted) == 0x3000);
        T_Ok(got == 0 && uwnd_reg_range_is_valid(valid, r0));
        T_Ok(!uwnd_reg_range_is_valid(valid, rp));
        if(arch == Arch_arm64) {T_Ok(uwnd_reg_is_valid(arch, valid, ARM64_RegCode_lr));}
      }break;
    }
  }
}

TEST(eh_frame_signed_cfa_offset)
{
  Arch arches[] = {Arch_x64, Arch_arm64};
  for EachElement(arch_idx, arches) for(U64 scenario = 0; scenario < 4; scenario += 1)
  {
    Arch arch = arches[arch_idx];
    ARCH_Info *ai = arch_info_from_arch(arch);
    U8 ra = arch == Arch_arm64 ? 30 : 16, sp = arch == Arch_arm64 ? 31 : 7;
    S64 factor = scenario < 2 ? -8 : 8;
    S64 operand = scenario % 2 == 0 ? -2 : 2;
    U8 bytes[] = {16,0,0,0, 0,0,0,0, 1,'z','R',0, 1,(U8)(factor & 0x7f),ra,1,4,0x0c,sp,0,
                  28,0,0,0, 24,0,0,0, 0,0x10,0,0,0,0,0,0, 0,1,0,0,0,0,0,0,
                  0,0x0c,sp,0,(U8)(0x80|ra),1,0x13,(U8)(operand & 0x7f)};
    U64 expected_cfa = 0x2000 + factor*operand;
    U64 stack[8] = {0};
    stack[(expected_cfa + factor - 0x1fe0)/8] = 0x3000;
    MemoryMap map = {0};
    memory_map_push(arena, &map, r1u64(0x4000, 0x4000+sizeof(bytes)), bytes);
    memory_map_push(arena, &map, r1u64(0x1fe0, 0x2020), stack);
    EH_UWND_ModuleUnwindInfo info = {.override_fde_vaddr = 0x4014};
    UWND_ModuleInfo module = {.unwind_info = &info, .os = OperatingSystem_Linux};
    void *regs = push_array(arena, U8, ai->reg_block_size);
    U8 *valid = push_array(arena, U8, ai->reg_block_size);
    MemorySet(valid, 1, ai->reg_block_size);
    arch_reg_block_write_ip(ai, regs, 0x1004);
    arch_reg_block_write_sp(ai, regs, 0x2000);
    U64 cfa = 0;
    UWND_StepResult step = eh_uwnd_step(arch, &map, &module, 0, arch_ip_from_reg_block(arch_info_from_arch(arch), regs), regs, valid, &cfa);
    T_Ok(step.status == UWND_StepStatus_Good);
    T_Ok(cfa == expected_cfa && arch_sp_from_reg_block(ai, regs) == expected_cfa);
    T_Ok(arch_ip_from_reg_block(ai, regs) == 0x3000);
  }
}

TEST(eh_frame_undefined_return_ends_stack)
{
  Arch arches[] = {Arch_arm64, Arch_x64};
  for EachElement(arch_idx, arches)
  {
    Arch arch = arches[arch_idx];
    ARCH_Info *ai = arch_info_from_arch(arch);
    U8 ra = arch == Arch_arm64 ? 30 : 16, sp = arch == Arch_arm64 ? 31 : 7;
    // _start can explicitly end the chain without a recoverable caller CFA.
    U8 bytes[] = {16,0,0,0, 0,0,0,0, 1,'z','R',0, 1,0x78,ra,1,4,0x0c,sp,8,
                  23,0,0,0, 24,0,0,0, 0,0x10,0,0,0,0,0,0, 0,1,0,0,0,0,0,0,
                  0,0x07,ra};
    MemoryMap map = {0};
    memory_map_push(arena, &map, r1u64(0x4000, 0x4000+sizeof(bytes)), bytes);
    EH_UWND_ModuleUnwindInfo info = {.override_fde_vaddr = 0x4014};
    UWND_ModuleInfo module = {.unwind_info = &info};
    void *regs = push_array(arena, U8, ai->reg_block_size);
    U8 *valid = push_array(arena, U8, ai->reg_block_size);
    arch_reg_block_write_ip(ai, regs, 0x1004);
    arch_reg_block_write_sp(ai, regs, 0x2000);
    U64 cfa = 0;
    UWND_StepResult step = eh_uwnd_step(arch, &map, &module, 0, arch_ip_from_reg_block(arch_info_from_arch(arch), regs), regs, valid, &cfa);
    T_Ok(step.status == UWND_StepStatus_EndOfStack);
    T_Ok(arch_ip_from_reg_block(ai, regs) == 0x1004);
    T_Ok(arch_sp_from_reg_block(ai, regs) == 0x2000);
    T_Ok(cfa == 0);
    for(U64 i = 0; i < ai->reg_block_size; i += 1) {T_Ok(valid[i] == 0);}
    // A rule that needs unknown return-address bytes is an error, not an end.
    bytes[sizeof(bytes)-2] = 0x08;
    uwnd_reg_mark_valid(arch, valid, ai->stack_pointer_reg_code, 0);
    step = eh_uwnd_step(arch, &map, &module, 0, arch_ip_from_reg_block(arch_info_from_arch(arch), regs), regs, valid, &cfa);
    T_Ok(step.status == UWND_StepStatus_Error);
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
  UWND_StepResult step = pe_x64_uwnd_step(Arch_x64, &map, &module, 0, regs.rip, &regs, valid, &cfa);
  T_Ok(step.status == UWND_StepStatus_Good && regs.rip == 0x3000);
  T_Ok(!uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_rax));
  T_Ok(!uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_rbx));
  T_Ok(uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_rbp));
  T_Ok(uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_rdi));
  T_Ok(uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_xmm6));
  T_Ok(!uwnd_reg_is_valid(Arch_x64, valid, X64_RegCode_ymm6));
}

TEST(pe_caller_pc_not_epilog)
{
  PE_IntelPdata pdata = {.voff_first = 0x1000, .voff_one_past_last = 0x1100, .voff_unwind_info = 0x4000};
  PE_X64_UWND_ModuleUnwindInfo info = {.pdatas = &pdata, .pdatas_count = 1};
  UWND_ModuleInfo module = {.unwind_info = &info, .os = OperatingSystem_Windows};
  U8 xdata[] = {1,4,1,0, 4,(U8)(PE_UnwindOpCode_ALLOC_SMALL | (3<<4))};
  // The final byte of this call's displacement looks like RET, but is not an instruction.
  U8 code[] = {0xe8,0,0,0,0xc3,0x90,0x90,0x90};
  U64 stack[] = {0xdead,0,0,0,0x5000};
  MemoryMap map = {0};
  memory_map_push(arena, &map, r1u64(0x10fb, 0x10fb+sizeof(code)), code);
  memory_map_push(arena, &map, r1u64(0x4000, 0x4000+sizeof(xdata)), xdata);
  memory_map_push(arena, &map, r1u64(0x2000, 0x2000+sizeof(stack)), stack);
  for(B32 is_return_address = 0; is_return_address < 2; is_return_address += 1)
  {
    // The exact-PC case really is stopped at RET, with the allocation already undone.
    X64_RegBlock regs = {.rip = is_return_address ? 0x1100 : 0x10ff, .rsp = is_return_address ? 0x2000 : 0x2020};
    U8 valid[sizeof(regs)];
    MemorySet(valid, 1, sizeof(valid));
    U64 cfa = 0;
    U64 lookup_pc = uwnd_lookup_vaddr_from_pc(Arch_x64, regs.rip, is_return_address);
    UWND_StepResult step = pe_x64_uwnd_step(Arch_x64, &map, &module, 0, lookup_pc, &regs, valid, &cfa);
    T_Ok(step.status == UWND_StepStatus_Good && !step.is_interrupted);
    T_Ok(regs.rip == 0x5000 && regs.rsp == 0x2028);
  }
}

TEST(pe_machine_frame_pc)
{
  for(U64 has_error_code = 0; has_error_code < 2; has_error_code += 1)
  {
    PE_IntelPdata pdata = {.voff_first = 0x1000, .voff_one_past_last = 0x1100, .voff_unwind_info = 0x4000};
    PE_X64_UWND_ModuleUnwindInfo info = {.pdatas = &pdata, .pdatas_count = 1};
    UWND_ModuleInfo module = {.unwind_info = &info, .os = OperatingSystem_Windows};
    U8 xdata[] = {1,0,1,0, 0,(U8)(PE_UnwindOpCode_PUSH_MACHFRAME | (has_error_code<<4))};
    U8 code[] = {0x90};
    U64 stack[] = {1234,0x3100,0x33,0x202,0x5000,0x2b};
    MemoryMap map = {0};
    memory_map_push(arena, &map, r1u64(0x1004, 0x1004+sizeof(code)), code);
    memory_map_push(arena, &map, r1u64(0x4000, 0x4000+sizeof(xdata)), xdata);
    memory_map_push(arena, &map, r1u64(0x2000, 0x2000+sizeof(stack)), stack);
    X64_RegBlock regs = {.rip = 0x1004, .rsp = has_error_code ? 0x2000 : 0x2008};
    U8 valid[sizeof(regs)];
    MemorySet(valid, 1, sizeof(valid));
    U64 cfa = 0;
    UWND_StepResult step = pe_x64_uwnd_step(Arch_x64, &map, &module, 0, regs.rip, &regs, valid, &cfa);
    T_Ok(step.status == UWND_StepStatus_Good && step.is_interrupted);
    T_Ok(regs.rip == 0x3100 && regs.rsp == 0x5000);
    T_Ok(uwnd_lookup_vaddr_from_pc(Arch_x64, regs.rip, !step.is_interrupted) == regs.rip);
  }
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
    UWND_StepResult step = eh_uwnd_step(arch, &map, &module, 0, arch_ip_from_reg_block(arch_info_from_arch(arch), regs), regs, valid, &cfa);
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

TEST(eh_frame_caller_pc_boundary)
{
  Arch arches[] = {Arch_arm64, Arch_x64};
  for EachElement(arch_idx, arches)
  {
    Arch arch = arches[arch_idx];
    ARCH_Info *ai = arch_info_from_arch(arch);
    U8 ra = arch == Arch_arm64 ? 30 : 16;
    U8 fp = arch == Arch_arm64 ? 29 : 6;
    U8 sp = arch == Arch_arm64 ? 31 : 7;
    U8 saved = arch == Arch_arm64 ? 19 : 3;
    U8 pc_reg = arch == Arch_arm64 ? 32 : 16;
    U8 bytes[] = {16,0,0,0, 0,0,0,0, 1,'z','R',0, 1,0x78,ra,1,4,0x0c,sp,0,
                  31,0,0,0, 24,0,0,0, 0,0,0,0,0,0,0,0, 0,1,0,0,0,0,0,0,
                  0,0x0c,fp,16,(U8)(0x80|ra),1,(U8)(0x80|fp),2,0x09,saved,pc_reg};
    U64 function_pc = 0x1000;
    MemoryCopy(bytes+28, &function_pc, 8);
    U64 stack[] = {0x2010,0x3100, 0,0x5000};
    MemoryMap map = {0};
    memory_map_push(arena, &map, r1u64(0x4000, 0x4000+sizeof(bytes)), bytes);
    memory_map_push(arena, &map, r1u64(0x2000, 0x2000+sizeof(stack)), stack);
    EH_UWND_ModuleUnwindInfo info = {.override_fde_vaddr = 0x4014};
    UWND_ModuleInfo module = {.unwind_info = &info};
    void *regs = push_array(arena, U8, ai->reg_block_size);
    U8 *valid = push_array(arena, U8, ai->reg_block_size);
    MemorySet(valid, 1, ai->reg_block_size);
    arch_reg_block_write_ip(ai, regs, 0x1004);
    arch_reg_block_write_sp(ai, regs, 0x2000);
    U64 frame_base = 0x2000;
    arch_reg_block_write_range(ai, regs, ai->reg_code_rng_table[arch_reg_code_from_dw(arch, fp)], &frame_base);
    U64 cfa = 0;
    UWND_StepResult first = eh_uwnd_step(arch, &map, &module, 0, 0x1004, regs, valid, &cfa);
    T_Ok(first.status == UWND_StepStatus_Good && !first.is_interrupted);
    T_Ok(arch_ip_from_reg_block(ai, regs) == 0x3100);
    // The caller's final call ends exactly at its FDE's exclusive upper bound.
    function_pc = 0x3000;
    MemoryCopy(bytes+28, &function_pc, 8);
    U64 lookup_pc = uwnd_lookup_vaddr_from_pc(arch, arch_ip_from_reg_block(ai, regs), !first.is_interrupted);
    T_Ok(lookup_pc == (arch == Arch_arm64 ? 0x30fc : 0x30ff));
    T_Ok(arch_ip_from_reg_block(ai, regs) == 0x3100);
    UWND_StepResult second = eh_uwnd_step(arch, &map, &module, 0, lookup_pc, regs, valid, &cfa);
    T_Ok(second.status == UWND_StepStatus_Good && !second.is_interrupted);
    T_Ok(arch_ip_from_reg_block(ai, regs) == 0x5000);
    T_Ok(arch_sp_from_reg_block(ai, regs) == 0x2020);
    // A CFI register rule reads the recovered PC, not its lookup adjustment.
    U64 saved_pc = 0;
    arch_reg_block_read_range(ai, regs, ai->reg_code_rng_table[arch_reg_code_from_dw(arch, saved)], &saved_pc);
    T_Ok(saved_pc == 0x3100);
    T_Ok(uwnd_lookup_vaddr_from_pc(arch, 0, 1) == 0);
    T_Ok(uwnd_lookup_vaddr_from_pc(arch, 0x3100, 0) == 0x3100);
  }
}
