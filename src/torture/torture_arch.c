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

TEST(trap_instruction_size_from_code)
{
  U8 x64_int3[] = {0xcc};
  U8 x64_nop[] = {0x90};
  T_Ok(arch_trap_instruction_size_from_code(Arch_x64, str8_array_fixed(x64_int3)) == 1);
  T_Ok(arch_trap_instruction_size_from_code(Arch_x64, str8_array_fixed(x64_nop)) == 0);
  T_Ok(arch_trap_instruction_size_from_code(Arch_x64, str8(0, 0)) == 0);

  U8 arm64_brk_0[] = {0x00, 0x00, 0x20, 0xd4};
  U8 arm64_brk_f000[] = {0x00, 0x00, 0x3e, 0xd4};
  U8 arm64_nop[] = {0x1f, 0x20, 0x03, 0xd5};
  U8 arm64_short_brk[] = {0x00, 0x00, 0x20};
  T_Ok(arch_trap_instruction_size_from_code(Arch_arm64, str8_array_fixed(arm64_brk_0)) == 4);
  T_Ok(arch_trap_instruction_size_from_code(Arch_arm64, str8_array_fixed(arm64_brk_f000)) == 4);
  T_Ok(arch_trap_instruction_size_from_code(Arch_arm64, str8_array_fixed(arm64_nop)) == 0);
  T_Ok(arch_trap_instruction_size_from_code(Arch_arm64, str8_array_fixed(arm64_short_brk)) == 0);
}

#if OS_MAC
TEST(mac_dyld_all_image_infos_ready)
{
  struct dyld_all_image_infos infos = {0};
  U64 task_addr = 0x12340000;
  infos.infoArrayCount = 4;
  infos.infoArray = (struct dyld_image_info *)0x20000000;
  infos.notification = (void *)0x30000000;
  infos.dyldAllImageInfosAddress = (struct dyld_all_image_infos *)task_addr;
  T_Ok(mac_dmn_dyld_all_image_infos_is_ready(task_addr, &infos));

  infos.dyldAllImageInfosAddress = (struct dyld_all_image_infos *)0x40000000;
  T_Ok(!mac_dmn_dyld_all_image_infos_is_ready(task_addr, &infos));

  infos.dyldAllImageInfosAddress = (struct dyld_all_image_infos *)task_addr;
  infos.notification = 0;
  T_Ok(!mac_dmn_dyld_all_image_infos_is_ready(task_addr, &infos));
}

TEST(mac_arm64_fix_code_vaddr)
{
  T_Ok(mac_dmn_fix_code_vaddr(Arch_arm64, 0xab00000123456789ull) == 0x0000000123456789ull);
  T_Ok(mac_dmn_fix_code_vaddr(Arch_arm64, 0x00c0000123456789ull) == 0xffc0000123456789ull);
  T_Ok(mac_dmn_fix_code_vaddr(Arch_x64,   0xab00000123456789ull) == 0xab00000123456789ull);
}

TEST(mac_exception_is_software_breakpoint)
{
  MAC_DMN_ExceptionMessage exception = {0};
  exception.is_valid = 1;
  exception.exception = EXC_BREAKPOINT;
  exception.code_count = 1;

  exception.code[0] = EXC_ARM_BREAKPOINT;
  T_Ok(mac_dmn_exception_is_software_breakpoint(Arch_arm64, &exception));

  exception.code[0] = EXC_ARM_DA_DEBUG;
  T_Ok(!mac_dmn_exception_is_software_breakpoint(Arch_arm64, &exception));

  exception.code[0] = EXC_I386_BPT;
  T_Ok(mac_dmn_exception_is_software_breakpoint(Arch_x64, &exception));

  exception.code[0] = EXC_I386_SGL;
  T_Ok(!mac_dmn_exception_is_software_breakpoint(Arch_x64, &exception));

  exception.is_valid = 0;
  exception.code[0] = EXC_ARM_BREAKPOINT;
  T_Ok(!mac_dmn_exception_is_software_breakpoint(Arch_arm64, &exception));
}
#endif

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

TEST(step_out_return_exit_points_do_not_have_static_traps)
{
  DASM_CtrlFlowPoint ret_point = {0};
  ret_point.vaddr = 0x1000;
  ret_point.vaddr_opl = 0x1004;
  ret_point.jump_dest_vaddr = 0;
  ret_point.inst_flags = DASM_InstFlag_Return;

  U64 trap_vaddr = 0xdeadbeef;
  T_Ok(!d_step_out_static_trap_vaddr_from_exit_point(&ret_point, &trap_vaddr));
  T_Ok(trap_vaddr == 0xdeadbeef);

  DASM_CtrlFlowPoint branch_point = {0};
  branch_point.vaddr = 0x2000;
  branch_point.vaddr_opl = 0x2004;
  branch_point.jump_dest_vaddr = 0x3000;
  branch_point.inst_flags = DASM_InstFlag_UnconditionalJump;

  T_Ok(d_step_out_static_trap_vaddr_from_exit_point(&branch_point, &trap_vaddr));
  T_Ok(trap_vaddr == 0x3000);
}

TEST(step_out_return_traps_use_link_register_continuation)
{
  X64_RegBlock x64_regs = {0};
  x64_regs.rip = 0x1000;
  T_Ok(d_step_out_trap_vaddr_from_return_frame(Arch_x64, &x64_regs) == 0x1000);

  ARM64_RegBlock arm64_regs = {0};
  arm64_regs.pc = 0x2000;
  T_Ok(d_step_out_trap_vaddr_from_return_frame(Arch_arm64, &arm64_regs) == 0x2004);
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

TEST(arm64_macho_entry_lr_preference)
{
  MachO_UnwindInfoLookupResult frame_lookup = {0};
  frame_lookup.voff_range = r1u64(0x840, 0x870);
  frame_lookup.encoding = MACHO_UNWIND_ARM64_MODE_FRAME;
  Rng1U64 function_range = r1u64(0x85c, 0x870);

  T_Ok(d_unwind_arm64_should_prefer_entry_lr(0x85c, function_range, 1, &frame_lookup));
  T_Ok(d_unwind_arm64_should_prefer_entry_lr(0x860, function_range, 1, &frame_lookup));
  T_Ok(!d_unwind_arm64_should_prefer_entry_lr(0x864, function_range, 1, &frame_lookup));
  T_Ok(!d_unwind_arm64_should_prefer_entry_lr(0x868, function_range, 1, &frame_lookup));

  MachO_UnwindInfoLookupResult frameless_lookup = {0};
  frameless_lookup.voff_range = r1u64(0x1000, 0x1030);
  frameless_lookup.encoding = MACHO_UNWIND_ARM64_MODE_FRAMELESS;
  T_Ok(d_unwind_arm64_should_prefer_entry_lr(0x100c, r1u64(0, 0), 1, &frameless_lookup));
}

TEST(macho_function_starts_from_lc_function_starts)
{
  U64 data_size = 0x300;
  U8 *data = push_array(arena, U8, data_size);
  String8 string = str8(data, data_size);

  U64 header_off = 0;
  U64 segment_off = sizeof(MachO_Header64);
  U64 function_starts_command_off = segment_off + sizeof(MachO_SegmentCommand64);
  U64 function_starts_data_off = 0x200;

  MachO_Header64 header = {0};
  header.magic = MACHO_MAGIC_64;
  header.cpu_type = MACHO_CPU_TYPE_ARM64;
  header.load_command_count = 2;
  header.load_commands_size = sizeof(MachO_SegmentCommand64) + sizeof(MachO_LinkeditDataCommand);
  MemoryCopy(data + header_off, &header, sizeof(header));

  MachO_SegmentCommand64 segment = {0};
  segment.cmd = MACHO_LC_SEGMENT_64;
  segment.cmd_size = sizeof(segment);
  MemoryCopy(segment.segment_name, "__TEXT", sizeof("__TEXT")-1);
  segment.vmaddr = 0x100000000ull;
  segment.vmsize = 0x1000;
  segment.fileoff = 0;
  segment.filesize = 0x1000;
  MemoryCopy(data + segment_off, &segment, sizeof(segment));

  MachO_LinkeditDataCommand function_starts_command = {0};
  function_starts_command.cmd = MACHO_LC_FUNCTION_STARTS;
  function_starts_command.cmd_size = sizeof(function_starts_command);
  function_starts_command.dataoff = (U32)function_starts_data_off;
  function_starts_command.datasize = 4;
  MemoryCopy(data + function_starts_command_off, &function_starts_command, sizeof(function_starts_command));

  data[function_starts_data_off + 0] = 0x20;
  data[function_starts_data_off + 1] = 0x18;
  data[function_starts_data_off + 2] = 0x80;
  data[function_starts_data_off + 3] = 0x01;

  MachO_Bin bin = macho_bin_from_data(arena, string);
  U64Array starts = macho_function_start_voffs_from_data(arena, string, &bin);
  T_Ok(starts.count == 3);
  T_Ok(starts.v[0] == 0x20);
  T_Ok(starts.v[1] == 0x38);
  T_Ok(starts.v[2] == 0xb8);
}

TEST(macho_function_starts_vrange_from_linkedit_data_command)
{
  U64 data_size = 0x200;
  U8 *data = push_array(arena, U8, data_size);
  String8 string = str8(data, data_size);

  U64 header_off = 0;
  U64 text_segment_off = sizeof(MachO_Header64);
  U64 linkedit_segment_off = text_segment_off + sizeof(MachO_SegmentCommand64);
  U64 function_starts_command_off = linkedit_segment_off + sizeof(MachO_SegmentCommand64);

  MachO_Header64 header = {0};
  header.magic = MACHO_MAGIC_64;
  header.cpu_type = MACHO_CPU_TYPE_ARM64;
  header.load_command_count = 3;
  header.load_commands_size = 2*sizeof(MachO_SegmentCommand64) + sizeof(MachO_LinkeditDataCommand);
  MemoryCopy(data + header_off, &header, sizeof(header));

  MachO_SegmentCommand64 text_segment = {0};
  text_segment.cmd = MACHO_LC_SEGMENT_64;
  text_segment.cmd_size = sizeof(text_segment);
  MemoryCopy(text_segment.segment_name, "__TEXT", sizeof("__TEXT")-1);
  text_segment.vmaddr = 0x100000000ull;
  text_segment.vmsize = 0x4000;
  text_segment.fileoff = 0;
  text_segment.filesize = 0x4000;
  MemoryCopy(data + text_segment_off, &text_segment, sizeof(text_segment));

  MachO_SegmentCommand64 linkedit_segment = {0};
  linkedit_segment.cmd = MACHO_LC_SEGMENT_64;
  linkedit_segment.cmd_size = sizeof(linkedit_segment);
  MemoryCopy(linkedit_segment.segment_name, "__LINKEDIT", sizeof("__LINKEDIT")-1);
  linkedit_segment.vmaddr = 0x100008000ull;
  linkedit_segment.vmsize = 0x4000;
  linkedit_segment.fileoff = 0x6000;
  linkedit_segment.filesize = 0x4000;
  MemoryCopy(data + linkedit_segment_off, &linkedit_segment, sizeof(linkedit_segment));

  MachO_LinkeditDataCommand function_starts_command = {0};
  function_starts_command.cmd = MACHO_LC_FUNCTION_STARTS;
  function_starts_command.cmd_size = sizeof(function_starts_command);
  function_starts_command.dataoff = 0x6120;
  function_starts_command.datasize = 0x30;
  MemoryCopy(data + function_starts_command_off, &function_starts_command, sizeof(function_starts_command));

  MachO_Bin bin = macho_bin_from_data(arena, string);
  Rng1U64 vrange = {0};
  T_Ok(macho_linkedit_data_vrange_from_bin(string, &bin, MACHO_LC_FUNCTION_STARTS, 0x50000000ull, &vrange));
  T_Ok(vrange.min == 0x150008120ull);
  T_Ok(vrange.max == 0x150008150ull);
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
