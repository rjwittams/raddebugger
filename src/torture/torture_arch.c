// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#undef T_Group
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

TEST(call_return_address_storage)
{
  T_Ok(arch_call_return_address_storage_kind(Arch_x64) == ArchCallReturnAddressStorageKind_Stack);
  T_Ok(arch_call_return_address_storage_kind(Arch_x86) == ArchCallReturnAddressStorageKind_Stack);
  T_Ok(arch_call_return_address_storage_kind(Arch_arm64) == ArchCallReturnAddressStorageKind_LinkRegister);
  T_Ok(arch_call_return_address_storage_kind(Arch_arm32) == ArchCallReturnAddressStorageKind_LinkRegister);
}

TEST(software_breakpoint_pc_offsets)
{
  T_Ok(arch_software_breakpoint_pc_offset(OperatingSystem_Mac, Arch_x64) == arch_info_from_arch(Arch_x64)->trap_instruction.size);
  T_Ok(arch_software_breakpoint_pc_offset(OperatingSystem_Linux, Arch_x64) == arch_info_from_arch(Arch_x64)->trap_instruction.size);
  T_Ok(arch_software_breakpoint_pc_offset(OperatingSystem_Windows, Arch_x64) == arch_info_from_arch(Arch_x64)->trap_instruction.size);
  T_Ok(arch_software_breakpoint_pc_offset(OperatingSystem_Mac, Arch_arm64) == 0);
  T_Ok(arch_software_breakpoint_pc_offset(OperatingSystem_Linux, Arch_arm64) == 0);
  T_Ok(arch_software_breakpoint_pc_offset(OperatingSystem_Windows, Arch_arm64) == arch_info_from_arch(Arch_arm64)->trap_instruction.size);
}

TEST(arm64_ctrl_flow_point_extents)
{
  U8 bl_code[] = {0x02, 0x00, 0x00, 0x94};
  DASM_CtrlFlowInfo info = dasm_ctrl_flow_info_from_arch_vaddr_code(arena, DASM_InstFlag_Call, Arch_arm64, 0x1000, str8_array_fixed(bl_code));
  T_Ok(info.exit_points.count == 1);
  T_Ok(info.exit_points.first->v.vaddr == 0x1000);
  T_Ok(info.exit_points.first->v.vaddr_opl == 0x1004);
  T_Ok(info.exit_points.first->v.jump_dest_vaddr == 0x1008);
}

TEST(step_over_line_call_trap_storage)
{
  DASM_CtrlFlowPoint point = {0};
  point.vaddr = 0x1000;
  point.vaddr_opl = 0x1004;
  point.jump_dest_vaddr = 0x2000;
  point.inst_flags = DASM_InstFlag_Call;

  Rng1U64List same_line_ranges = {0};
  rng1u64_list_push(arena, &same_line_ranges, r1u64(0x1000, 0x1004));

  D_Trap x64_trap = d_trap_from_step_over_line_call(Arch_x64, &point, &same_line_ranges);
  T_Ok(x64_trap.vaddr == point.vaddr);
  T_Ok(x64_trap.flags == (D_TrapFlag_BeginSpoofMode|D_TrapFlag_SingleStepAfterHit));

  D_Trap arm64_line_end_trap = d_trap_from_step_over_line_call(Arch_arm64, &point, &same_line_ranges);
  T_Ok(arm64_line_end_trap.vaddr == point.vaddr_opl);
  T_Ok(arm64_line_end_trap.flags == D_TrapFlag_EndStepping);

  rng1u64_list_push(arena, &same_line_ranges, r1u64(0x1004, 0x1008));
  D_Trap arm64_same_line_trap = d_trap_from_step_over_line_call(Arch_arm64, &point, &same_line_ranges);
  T_Ok(arm64_same_line_trap.vaddr == point.vaddr_opl);
  T_Ok(arm64_same_line_trap.flags == D_TrapFlag_SingleStepAfterHit);
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

TEST(arm64_vector_register_metadata)
{
  ARCH_Info *info = arch_info_from_arch(Arch_arm64);

  T_Ok(arch_reg_code_from_name(info, str8_lit("q0")) == ARM64_RegCode_q0);
  T_Ok(arch_reg_code_from_name(info, str8_lit("q31")) == ARM64_RegCode_q31);
  T_Ok(info->reg_code_is_vector_table[ARM64_RegCode_q0] == 1);
  T_Ok(info->reg_code_is_vector_table[ARM64_RegCode_q31] == 1);
  T_Ok(info->reg_code_rng_table[ARM64_RegCode_q0].max - info->reg_code_rng_table[ARM64_RegCode_q0].min == 16);
  T_Ok(info->reg_code_rng_table[ARM64_RegCode_q31].max - info->reg_code_rng_table[ARM64_RegCode_q31].min == 16);

  T_Ok(arch_reg_code_from_rdi(Arch_arm64, RDI_RegCodeARM64_q0) == ARM64_RegCode_q0);
  T_Ok(arch_reg_code_from_rdi(Arch_arm64, RDI_RegCodeARM64_q31) == ARM64_RegCode_q31);
  T_Ok(str8_match(rdi_string_from_reg_code(arena, RDI_Arch_ARM64, RDI_RegCodeARM64_q0), str8_lit("q0"), 0));

  T_Ok(arch_reg_code_from_dw(Arch_arm64, DW_RegARM64_V0) == ARM64_RegCode_q0);
  T_Ok(arch_reg_code_from_dw(Arch_arm64, DW_RegARM64_V31) == ARM64_RegCode_q31);
  T_Ok(dw_reg_size_from_code(Arch_arm64, DW_RegARM64_V0) == 16);
  T_Ok(dw_reg_size_from_code(Arch_arm64, DW_RegARM64_V31) == 16);
  T_Ok(d2r_rdi_reg_code_from_dw_reg(Arch_arm64, DW_RegARM64_V0) == RDI_RegCodeARM64_q0);
}

TEST(arm64_macho_compact_frame_saved_reg_slots)
{
  U32 encoding = (MACHO_UNWIND_ARM64_MODE_FRAME|
                  MACHO_UNWIND_ARM64_FRAME_X19_X20_PAIR|
                  MACHO_UNWIND_ARM64_FRAME_X21_X22_PAIR|
                  MACHO_UNWIND_ARM64_FRAME_D8_D9_PAIR);
  D_MachOCompactArm64FrameRegSlot slots[8] = {0};
  U64 count = d_macho_compact_unwind_arm64_frame_saved_reg_slots_from_encoding(encoding, slots, ArrayCount(slots));

  T_Ok(count == 6);
  T_Ok(slots[0].reg_code == ARM64_RegCode_x19);
  T_Ok(slots[0].cfa_off == -24);
  T_Ok(slots[1].reg_code == ARM64_RegCode_x20);
  T_Ok(slots[1].cfa_off == -32);
  T_Ok(slots[2].reg_code == ARM64_RegCode_x21);
  T_Ok(slots[2].cfa_off == -40);
  T_Ok(slots[3].reg_code == ARM64_RegCode_x22);
  T_Ok(slots[3].cfa_off == -48);
  T_Ok(slots[4].reg_code == ARM64_RegCode_q8);
  T_Ok(slots[4].cfa_off == -56);
  T_Ok(slots[4].byte_size == 8);
  T_Ok(slots[5].reg_code == ARM64_RegCode_q9);
  T_Ok(slots[5].cfa_off == -64);
  T_Ok(slots[5].byte_size == 8);

  D_MachOCompactArm64FrameRegSlot short_slots[2] = {0};
  U64 short_count = d_macho_compact_unwind_arm64_frame_saved_reg_slots_from_encoding(encoding, short_slots, ArrayCount(short_slots));
  T_Ok(short_count == 6);
  T_Ok(short_slots[0].reg_code == ARM64_RegCode_x19);
  T_Ok(short_slots[1].reg_code == ARM64_RegCode_x20);

  ARM64_RegBlock regs = {0};
  regs.q8.u64[1] = 0x1122334455667788ull;
  T_Ok(d_macho_compact_unwind_arm64_write_reg_slot(&regs, &slots[0], 0x1919191919191919ull));
  T_Ok(d_macho_compact_unwind_arm64_write_reg_slot(&regs, &slots[4], 0x8888888888888888ull));
  T_Ok(regs.x19 == 0x1919191919191919ull);
  T_Ok(regs.q8.u64[0] == 0x8888888888888888ull);
  T_Ok(regs.q8.u64[1] == 0x1122334455667788ull);
}

#if OS_MAC && ARCH_ARM64
TEST(arm64_darwin_neon_state_conversion)
{
  arm_neon_state64_t neon = {0};
  ARM64_RegBlock regs = {0};
  U128 q0 = u128_make(0x0011223344556677ull, 0x8899aabbccddeeffull);
  U128 q31 = u128_make(0xfedcba9876543210ull, 0x7766554433221100ull);

  MemoryCopy(&neon.__v[0], &q0, sizeof(q0));
  MemoryCopy(&neon.__v[31], &q31, sizeof(q31));
  mac_dmn_arm64_reg_block_from_neon_state(&regs, &neon);
  T_Ok(u128_match(regs.q0, q0));
  T_Ok(u128_match(regs.q31, q31));

  U128 q7 = u128_make(0x1011121314151617ull, 0x18191a1b1c1d1e1full);
  U128 q30 = u128_make(0x2021222324252627ull, 0x28292a2b2c2d2e2full);
  MemoryZeroStruct(&neon);
  MemoryZeroStruct(&regs);
  regs.q7 = q7;
  regs.q30 = q30;
  mac_dmn_arm64_neon_state_from_reg_block(&neon, &regs);

  U128 got_q7 = {0};
  U128 got_q30 = {0};
  MemoryCopy(&got_q7, &neon.__v[7], sizeof(got_q7));
  MemoryCopy(&got_q30, &neon.__v[30], sizeof(got_q30));
  T_Ok(u128_match(got_q7, q7));
  T_Ok(u128_match(got_q30, q30));
}

TEST(arm64_darwin_debug_watchpoint_encoding)
{
  DMN_Trap trap = {0};
  U64 wvr = 0;
  U64 wcr = 0;

  trap.vaddr = 0x1005;
  trap.size = 3;
  trap.flags = DMN_TrapFlag_BreakOnWrite;
  T_Ok(mac_dmn_arm64_debug_state_regs_from_trap(&trap, &wvr, &wcr));
  T_Ok(wvr == 0x1000);
  T_Ok(wcr == 0x1c15);

  trap.vaddr = 0x2000;
  trap.size = 8;
  trap.flags = DMN_TrapFlag_BreakOnRead|DMN_TrapFlag_BreakOnWrite;
  T_Ok(mac_dmn_arm64_debug_state_regs_from_trap(&trap, &wvr, &wcr));
  T_Ok(wvr == 0x2000);
  T_Ok(wcr == 0x1ffd);

  trap.vaddr = 0x3000;
  trap.size = 16;
  trap.flags = DMN_TrapFlag_BreakOnRead;
  T_Ok(mac_dmn_arm64_debug_state_regs_from_trap(&trap, &wvr, &wcr));
  T_Ok(wvr == 0x3000);
  T_Ok(wcr == 0x4001fed);

  trap.vaddr = 0x4000;
  trap.size = 1;
  trap.flags = DMN_TrapFlag_BreakOnExecute;
  T_Ok(!mac_dmn_arm64_debug_state_regs_from_trap(&trap, &wvr, &wcr));
}

TEST(arm64_darwin_debug_watchpoint_hit_matching)
{
  arm_debug_state64_t state = {0};
  state.__wvr[0] = 0x1000;
  state.__wcr[0] = 0x1c15;
  state.__wvr[2] = 0x3000;
  state.__wcr[2] = 0x4001fed;

  T_Ok(mac_dmn_arm64_debug_trap_idx_from_state(&state, 0x1006, 4) == 0);
  T_Ok(mac_dmn_arm64_debug_trap_idx_from_state(&state, 0x1004, 4) == max_U64);
  T_Ok(mac_dmn_arm64_debug_trap_idx_from_state(&state, 0x300f, 4) == 2);
  T_Ok(mac_dmn_arm64_debug_trap_idx_from_state(&state, 0x3010, 4) == max_U64);
}
#endif

TEST(arm64_disasm_basic)
{
  U8 nop_code[] = {0x1f, 0x20, 0x03, 0xd5};
  DASM_Inst nop = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(nop_code), DASM_Syntax_Intel);
  T_Ok(nop.size == 4);
  T_Ok(nop.flags & DASM_InstFlag_NonFlow);
  T_Ok(str8_match(nop.string, str8_lit("nop"), 0));

  U8 brk_code[] = {0x00, 0x00, 0x20, 0xd4};
  DASM_Inst brk = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(brk_code), DASM_Syntax_Intel);
  T_Ok(brk.size == 4);
  T_Ok(str8_match(str8_prefix(brk.string, 3), str8_lit("brk"), 0));

  U8 bl_code[] = {0x02, 0x00, 0x00, 0x94};
  DASM_Inst bl = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(bl_code), DASM_Syntax_Intel);
  T_Ok(bl.size == 4);
  T_Ok(bl.flags & DASM_InstFlag_Call);
  T_Ok(bl.dst_vaddr == 0x1008);

  U8 b_code[] = {0x02, 0x00, 0x00, 0x14};
  DASM_Inst b = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(b_code), DASM_Syntax_Intel);
  T_Ok(b.size == 4);
  T_Ok(b.flags & DASM_InstFlag_UnconditionalJump);
  T_Ok(b.dst_vaddr == 0x1008);

  U8 b_eq_code[] = {0x40, 0x00, 0x00, 0x54};
  DASM_Inst b_eq = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(b_eq_code), DASM_Syntax_Intel);
  T_Ok(b_eq.size == 4);
  T_Ok(b_eq.flags & DASM_InstFlag_Branch);
  T_Ok(b_eq.dst_vaddr == 0x1008);

  U8 ret_code[] = {0xc0, 0x03, 0x5f, 0xd6};
  DASM_Inst ret = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(ret_code), DASM_Syntax_Intel);
  T_Ok(ret.size == 4);
  T_Ok(ret.flags & DASM_InstFlag_Return);

  U8 short_code[] = {0x1f, 0x20, 0x03};
  DASM_Inst short_inst = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(short_code), DASM_Syntax_Intel);
  T_Ok(short_inst.size == 0);

  U8 invalid_code[] = {0xcf, 0xfa, 0xed, 0xfe, 0x1f, 0x20, 0x03, 0xd5};
  T_Ok(dasm_invalid_inst_size_from_arch_vaddr_code(Arch_arm64, 0x1000, str8_array_fixed(invalid_code)) == 4);
  T_Ok(dasm_invalid_inst_size_from_arch_vaddr_code(Arch_arm64, 0x1001, str8_array_fixed(invalid_code)) == 3);
  T_Ok(dasm_invalid_inst_size_from_arch_vaddr_code(Arch_x64, 0x1000, str8_array_fixed(invalid_code)) == 0);
}

TEST(arm64_disasm_registers_and_stack)
{
  U8 add_code[] = {0x00, 0x04, 0x00, 0x91};
  DASM_Inst add = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(add_code), DASM_Syntax_Intel);
  T_Ok(add.size == 4);
  T_Ok(add.dst_reg_code == ARM64_RegCode_x0);
  T_Ok(add.src_reg_code == ARM64_RegCode_x0);
  T_Ok(!(add.flags & DASM_InstFlag_ChangesStackPointer));

  U8 mov_code[] = {0xe1, 0x03, 0x00, 0xaa};
  DASM_Inst mov = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(mov_code), DASM_Syntax_Intel);
  T_Ok(mov.size == 4);
  T_Ok(mov.dst_reg_code == ARM64_RegCode_x1);
  T_Ok(mov.src_reg_code == ARM64_RegCode_x0);

  U8 ldr_code[] = {0xe0, 0x07, 0x40, 0xf9};
  DASM_Inst ldr = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(ldr_code), DASM_Syntax_Intel);
  T_Ok(ldr.size == 4);
  T_Ok(ldr.dst_reg_code == ARM64_RegCode_x0);
  T_Ok(ldr.src_reg_code == ARM64_RegCode_sp);
  T_Ok(ldr.src_reg_off == 8);

  U8 str_code[] = {0xe0, 0x07, 0x00, 0xf9};
  DASM_Inst str = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(str_code), DASM_Syntax_Intel);
  T_Ok(str.size == 4);
  T_Ok(str.dst_reg_code == ARM64_RegCode_sp);
  T_Ok(str.dst_reg_off == 8);
  T_Ok(str.src_reg_code == ARM64_RegCode_x0);

  U8 add_sp_code[] = {0xff, 0x43, 0x00, 0x91};
  DASM_Inst add_sp = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(add_sp_code), DASM_Syntax_Intel);
  T_Ok(add_sp.size == 4);
  T_Ok(add_sp.flags & DASM_InstFlag_ChangesStackPointer);
  T_Ok(!(add_sp.flags & DASM_InstFlag_ChangesStackPointerVariably));

  U8 mov_sp_code[] = {0x1f, 0x00, 0x00, 0x91};
  DASM_Inst mov_sp = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(mov_sp_code), DASM_Syntax_Intel);
  T_Ok(mov_sp.size == 4);
  T_Ok(mov_sp.dst_reg_code == ARM64_RegCode_sp);
  T_Ok(mov_sp.src_reg_code == ARM64_RegCode_x0);
  T_Ok(mov_sp.flags & DASM_InstFlag_ChangesStackPointer);
  T_Ok(mov_sp.flags & DASM_InstFlag_ChangesStackPointerVariably);

  U8 stp_code[] = {0xfd, 0x7b, 0xbf, 0xa9};
  DASM_Inst stp = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(stp_code), DASM_Syntax_Intel);
  T_Ok(stp.size == 4);
  T_Ok(stp.flags & DASM_InstFlag_ChangesStackPointer);
  T_Ok(!(stp.flags & DASM_InstFlag_ChangesStackPointerVariably));

  U8 ldp_code[] = {0xfd, 0x7b, 0xc1, 0xa8};
  DASM_Inst ldp = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(ldp_code), DASM_Syntax_Intel);
  T_Ok(ldp.size == 4);
  T_Ok(ldp.dst_reg_code == ARM64_RegCode_x29);
  T_Ok(ldp.src_reg_code == ARM64_RegCode_sp);
  T_Ok(ldp.src_reg_off == 0);
  T_Ok(ldp.flags & DASM_InstFlag_ChangesStackPointer);
  T_Ok(!(ldp.flags & DASM_InstFlag_ChangesStackPointerVariably));
}

TEST(arm64_disasm_flow_register_operands)
{
  U8 br_code[] = {0x00, 0x02, 0x1f, 0xd6};
  DASM_Inst br = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(br_code), DASM_Syntax_Intel);
  T_Ok(br.size == 4);
  T_Ok(br.flags & DASM_InstFlag_UnconditionalJump);
  T_Ok(br.dst_reg_code == ARM64_RegCode_nil);
  T_Ok(br.src_reg_code == ARM64_RegCode_x16);

  U8 blr_code[] = {0x00, 0x02, 0x3f, 0xd6};
  DASM_Inst blr = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(blr_code), DASM_Syntax_Intel);
  T_Ok(blr.size == 4);
  T_Ok(blr.flags & DASM_InstFlag_Call);
  T_Ok(blr.dst_reg_code == ARM64_RegCode_nil);
  T_Ok(blr.src_reg_code == ARM64_RegCode_x16);

  U8 cbz_code[] = {0x00, 0x00, 0x00, 0xb4};
  DASM_Inst cbz = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(cbz_code), DASM_Syntax_Intel);
  T_Ok(cbz.size == 4);
  T_Ok(cbz.flags & DASM_InstFlag_Branch);
  T_Ok(cbz.dst_reg_code == ARM64_RegCode_nil);
  T_Ok(cbz.src_reg_code == ARM64_RegCode_x0);
}

TEST(arm64_disasm_compare_operands)
{
  U8 cmp_code[] = {0x1f, 0x00, 0x1f, 0xeb};
  DASM_Inst cmp = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(cmp_code), DASM_Syntax_Intel);
  T_Ok(cmp.size == 4);
  T_Ok(cmp.flags & DASM_InstFlag_NonFlow);
  T_Ok(cmp.dst_reg_code == ARM64_RegCode_nil);
  T_Ok(cmp.src_reg_code == ARM64_RegCode_x0);
}

TEST(arm64_disasm_variable_memory_operands)
{
  U8 str_reg_off_code[] = {0xe1, 0x6b, 0x20, 0xf8};
  DASM_Inst str_reg_off = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(str_reg_off_code), DASM_Syntax_Intel);
  T_Ok(str_reg_off.size == 4);
  T_Ok(str_reg_off.dst_reg_code == ARM64_RegCode_nil);
  T_Ok(str_reg_off.src_reg_code == ARM64_RegCode_x1);

  U8 ldr_reg_off_code[] = {0xe1, 0x6b, 0x60, 0xf8};
  DASM_Inst ldr_reg_off = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(ldr_reg_off_code), DASM_Syntax_Intel);
  T_Ok(ldr_reg_off.size == 4);
  T_Ok(ldr_reg_off.dst_reg_code == ARM64_RegCode_x1);
  T_Ok(ldr_reg_off.src_reg_code == ARM64_RegCode_nil);
}

TEST(arm64_disasm_data_labels_are_not_jump_targets)
{
  U8 adr_code[] = {0x00, 0x00, 0x00, 0x10};
  DASM_Inst adr = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_array_fixed(adr_code), DASM_Syntax_Intel);
  T_Ok(adr.size == 4);
  T_Ok(adr.flags & DASM_InstFlag_NonFlow);
  T_Ok(adr.dst_reg_code == ARM64_RegCode_x0);
  T_Ok(adr.dst_vaddr == 0);

  U8 ldr_literal_code[] = {0xc2, 0xff, 0xff, 0x58};
  DASM_Inst ldr_literal = dasm_inst_from_code(arena, Arch_arm64, 0x1008, str8_array_fixed(ldr_literal_code), DASM_Syntax_Intel);
  T_Ok(ldr_literal.size == 4);
  T_Ok(ldr_literal.flags & DASM_InstFlag_NonFlow);
  T_Ok(ldr_literal.dst_reg_code == ARM64_RegCode_x2);
  T_Ok(ldr_literal.dst_vaddr == 0);
}
