// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Helpers

internal PE_IntelPdata *
pe_x64_uwnd_intel_pdata_from_voff(PE_X64_UWND_ModuleUnwindInfo *unwind_info, U64 voff)
{
  PE_IntelPdata *first_pdata = 0;
  {
    PE_IntelPdata *pdatas = unwind_info->pdatas;
    U64 pdatas_count = unwind_info->pdatas_count;
    if(pdatas_count != 0 && voff >= pdatas[0].voff_first)
    {
      // NOTE(rjf):
      //
      // binary search:
      //  find max index s.t. pdata_array[index].voff_first <= voff
      //  we assume (i < j) -> (pdata_array[i].voff_first < pdata_array[j].voff_first)
      //
      U64 index = pdatas_count;
      U64 min = 0;
      U64 opl = pdatas_count;
      for(;;)
      {
        U64 mid = (min + opl)/2;
        PE_IntelPdata *pdata = pdatas + mid;
        if(voff < pdata->voff_first)
        {
          opl = mid;
        }
        else if(pdata->voff_first < voff)
        {
          min = mid;
        }
        else
        {
          index = mid;
          break;
        }
        if(min + 1 >= opl)
        {
          index = min;
          break;
        }
      }
      
      // rjf: if we are in range fill result
      {
        PE_IntelPdata *pdata = pdatas + index;
        if(pdata->voff_first <= voff && voff < pdata->voff_one_past_last)
        {
          first_pdata = pdata;
        }
      }
    }
  }
  return first_pdata;
}

internal X64_RegCode
pe_x64_uwnd_reg_code_from_pe_gpr_reg(PE_UnwindGprRegX64 gpr_reg)
{
  X64_RegCode result = X64_RegCode_nil;
  switch(gpr_reg)
  {
    case PE_UnwindGprRegX64_RAX:{result = X64_RegCode_rax;}break;
    case PE_UnwindGprRegX64_RCX:{result = X64_RegCode_rcx;}break;
    case PE_UnwindGprRegX64_RDX:{result = X64_RegCode_rdx;}break;
    case PE_UnwindGprRegX64_RBX:{result = X64_RegCode_rbx;}break;
    case PE_UnwindGprRegX64_RSP:{result = X64_RegCode_rsp;}break;
    case PE_UnwindGprRegX64_RBP:{result = X64_RegCode_rbp;}break;
    case PE_UnwindGprRegX64_RSI:{result = X64_RegCode_rsi;}break;
    case PE_UnwindGprRegX64_RDI:{result = X64_RegCode_rdi;}break;
    case PE_UnwindGprRegX64_R8 :{result = X64_RegCode_r8;}break;
    case PE_UnwindGprRegX64_R9 :{result = X64_RegCode_r9;}break;
    case PE_UnwindGprRegX64_R10:{result = X64_RegCode_r10;}break;
    case PE_UnwindGprRegX64_R11:{result = X64_RegCode_r11;}break;
    case PE_UnwindGprRegX64_R12:{result = X64_RegCode_r12;}break;
    case PE_UnwindGprRegX64_R13:{result = X64_RegCode_r13;}break;
    case PE_UnwindGprRegX64_R14:{result = X64_RegCode_r14;}break;
    case PE_UnwindGprRegX64_R15:{result = X64_RegCode_r15;}break;
  }
  return result;
}

////////////////////////////////
//~ rjf: Unwinding Abstraction Implementation

internal UWND_StepResult
pe_x64_uwnd_step(Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, void *regs_opaque, U8 *reg_valid, U64 *cfa_out)
{
  UWND_StepResult result = {UWND_StepStatus_Error};
  PE_X64_UWND_ModuleUnwindInfo *unwind_info = (PE_X64_UWND_ModuleUnwindInfo *)module_info->unwind_info;
  X64_RegBlock *regs = (X64_RegBlock *)regs_opaque;
  if(!uwnd_reg_is_valid(arch, reg_valid, X64_RegCode_rsp)) {return result;}
  U8 caller_valid[sizeof(*regs)];
  uwnd_reg_valid_from_abi(arch, OperatingSystem_Windows, reg_valid, caller_valid);
  if(unwind_info != 0)
  {
    B32 done = 0;
    Temp scratch = scratch_begin(0, 0);
    
    ////////////////////////////
    //- rjf: on PE/x64, the frame base address is always RSP
    //
    cfa_out[0] = regs->rsp;
    
    ////////////////////////////
    //- rjf: unpack context from rip
    //
    U64 rip_voff = regs->rip - module_info->base_vaddr;
    PE_IntelPdata *first_pdata = pe_x64_uwnd_intel_pdata_from_voff(unwind_info, rip_voff);
    
    ////////////////////////////
    //- rjf: pdata -> detect if in epilog
    //
    B32 has_pdata_and_in_epilog = 0;
    U64 instruction_data_size_cap = 256;
    Rng1U64 instruction_data_vaddr_range = r1u64(regs->rip, regs->rip+instruction_data_size_cap);
    String8 instruction_data = {0};
    if(!done && first_pdata)
    {
      // NOTE(allen): There are restrictions placed on how an epilog is allowed
      // to be formed (https://docs.microsoft.com/en-us/cpp/build/prolog-and-epilog?view=msvc-160)
      // Here we interpret machine code directly according to the rules
      // given there to determine if the code we're looking at looks like an epilog.
      
      //- rjf: read instruction memory
      U8 *instruction_data_buffer = push_array(scratch.arena, U8, instruction_data_size_cap);
      U64 instruction_data_size = memory_map_read(memory_map, instruction_data_vaddr_range, instruction_data_buffer);
      instruction_data = str8(instruction_data_buffer, instruction_data_size);
      if(instruction_data.size == 0)
      {
        done = 1;
        result.status = UWND_StepStatus_FailedMemoryRead;
        result.missed_read_vaddr_range = instruction_data_vaddr_range;
      }
      
      //- rjf: set up parsing state
      B32 is_epilog = 0;
      
      //- rjf: check first instruction, determine if we should keep parsing & from where
      U64 read_off = 0;
      B32 keep_parsing = 0;
      if(!done && instruction_data.size >= 4)
      {
        U8 inst[4] = {0};
        str8_deserial_read(instruction_data, read_off, &inst[0], sizeof(inst), sizeof(inst));
        keep_parsing = 1;
        if((inst[0] & 0xF8) == 0x48)
        {
          switch(inst[1])
          {
            // rjf: add $nnnn,%rsp
            case 0x81:
            {
              if(inst[0] == 0x48 && inst[2] == 0xC4)
              {
                read_off += 7;
              }
              else
              {
                keep_parsing = 0;
              }
            }break;
            
            // rjf: add $n,%rsp
            case 0x83:
            {
              if(inst[0] == 0x48 && inst[2] == 0xC4)
              {
                read_off += 4;
              }
              else
              {
                keep_parsing = 0;
              }
            }break;
            
            // rjf: lea n(reg),%rsp
            case 0x8D:
            {
              if((inst[0] & 0x06) == 0 &&
                 ((inst[2] >> 3) & 0x07) == 0x04 &&
                 (inst[2] & 0x07) != 0x04)
              {
                U8 imm_size = (inst[2] >> 6);
                
                // rjf: 1-byte immediate
                if(imm_size == 1)
                {
                  read_off += 4;
                }
                
                // rjf: 4-byte immediate
                else if(imm_size == 2)
                {
                  read_off += 7;
                }
                
                // rjf: other case
                else
                {
                  keep_parsing = 0;
                }
              }
              else
              {
                keep_parsing = 0;
              }
            }break;
          }
        }
      }
      
      //- rjf: continue parsing instructions
      for(;!done && keep_parsing;)
      {
        // rjf: read next instruction byte
        B32 inst_byte_good = 1;
        U8 inst_byte = 0;
        if(str8_deserial_read_struct(instruction_data, read_off, &inst_byte) == 0)
        {
          inst_byte_good = 0;
          keep_parsing = 0;
        }
        read_off += 1;
        
        // rjf: when (... I don't know ...) rely on the next byte
        B32 check_inst_byte_good = inst_byte_good;
        U8 check_inst_byte = inst_byte;
        if(inst_byte_good && (inst_byte & 0xF0) == 0x40)
        {
          if(str8_deserial_read_struct(instruction_data, read_off, &check_inst_byte) == 0)
          {
            check_inst_byte_good = 0;
            keep_parsing = 0;
          }
          read_off += 1;
        }
        
        // rjf: check instruction byte
        if(check_inst_byte_good)
        {
          switch(check_inst_byte)
          {
            default:{keep_parsing = 0;}break;
            
            // rjf: pop
            case 0x58:case 0x59:case 0x5A:case 0x5B:
            case 0x5C:case 0x5D:case 0x5E:case 0x5F:
            {
            }break;
            
            // rjf: ret
            case 0xC2:
            case 0xC3:
            { 
              is_epilog = 1;
              keep_parsing = 0;
            }break;
            
            // rjf: jmp nnnn
            case 0xE9:
            {
              //
              // TODO(rjf): old code here was reading the jump's immediate value, applying the delta,
              // and continued parsing if the jump destination address fell within the same pdata.
              // but from what I can tell, the only legal usage of a jump within an epilog is to end
              // the epilog, and jump to another function; if the jump stays in the function, it is
              // an illegal epilog sequence. so, I've instead just changed this to treat any jmps
              // which escape the pdata, and also end the pdata, as marking the epilog.
              //
              // I am not sure if this is correct w.r.t. chained pdatas, however, since presumably
              // a jump could also land elsewhere in the same function - we may need to also look for
              // stack pointer modifications etc. - those together with a jump in this context may
              // let us tell the difference.
              //
              S32 imm = 0;
              B32 imm_good = (str8_deserial_read_struct(instruction_data, read_off, &imm) == sizeof(imm));
              U64 next_vaddr = (instruction_data_vaddr_range.min + read_off + sizeof(imm) + imm);
              U64 next_voff = (next_vaddr - module_info->base_vaddr);
              if((next_voff < first_pdata->voff_first || first_pdata->voff_one_past_last <= next_voff) &&
                 read_off + sizeof(imm) == first_pdata->voff_one_past_last)
              {
                is_epilog = 1;
              }
              keep_parsing = 0;
            }break;
            
            // rjf: rep; ret (for amd64 prediction bug)
            case 0xF3:
            {
              U8 next_inst_byte = 0;
              B32 next_inst_byte_good = (str8_deserial_read_struct(instruction_data, read_off, &next_inst_byte) == sizeof(next_inst_byte));
              read_off += 1;
              keep_parsing = 0;
              if(next_inst_byte_good)
              {
                is_epilog = (next_inst_byte == 0xC3);
              }
            }break;
          }
        }
      }
      has_pdata_and_in_epilog = is_epilog;
    }
    
    ////////////////////////////
    //- rjf: pdata & in epilog -> epilog unwind
    //
    if(!done && first_pdata && has_pdata_and_in_epilog)
    {
      U64 read_off = 0;
      for(B32 keep_parsing = 1; !done && keep_parsing;)
      {
        //- rjf: assume no more parsing after this instruction
        keep_parsing = 0;
        
        //- rjf: read next instruction byte
        U8 inst_byte = 0;
        if(str8_deserial_read_struct(instruction_data, read_off, &inst_byte) == 0)
        {
          done = 1;
        }
        read_off += 1;
        
        //- rjf: extract rex from instruction byte
        U8 rex = 0;
        if((inst_byte & 0xF0) == 0x40)
        {
          rex = inst_byte & 0xF; // rex prefix
          if(str8_deserial_read_struct(instruction_data, read_off, &inst_byte) == 0)
          {
            done = 1;
          }
          read_off += 1;
        }
        
        //- rjf: parse remainder of instruction
        switch(inst_byte)
        {
          // rjf: pop
          case 0x58:case 0x59:case 0x5A:case 0x5B:
          case 0x5C:case 0x5D:case 0x5E:case 0x5F:
          {
            // rjf: read value at rsp
            U64 sp = regs->rsp;
            U64 value = 0;
            if(memory_map_read_struct(memory_map, sp, &value) != sizeof(value))
            {
              done = 1;
              result.status = UWND_StepStatus_FailedMemoryRead;
              result.missed_read_vaddr_range = r1u64(sp, sp+sizeof(value));
            }
            
            // rjf: modify registers
            if(!done)
            {
              PE_UnwindGprRegX64 gpr_reg = (inst_byte - 0x58) + (rex & 1)*8;
              X64_RegCode reg_code = pe_x64_uwnd_reg_code_from_pe_gpr_reg(gpr_reg);
              Rng1U16 reg_rng = x64_reg_code_rng_table[reg_code];
              U64 *reg_ptr = (U64 *)((U8 *)regs + reg_rng.min);
              reg_ptr[0] = value;
              uwnd_reg_set_valid(caller_valid, reg_rng, 1);
              regs->rsp = sp + 8;
            }
            
            // rjf: not a final instruction, so keep parsing
            keep_parsing = 1;
          }break;
          
          // rjf: add $nnnn,%rsp 
          case 0x81:
          {
            // rjf: skip one byte (we already know what it is in this scenario)
            read_off += 1;
            
            // rjf: read the 4-byte immediate
            S32 imm = 0;
            B32 imm_is_good = (str8_deserial_read_struct(instruction_data, read_off, &imm) == sizeof(imm));
            if(!imm_is_good)
            {
              done = 1;
            }
            read_off += sizeof(imm);
            
            // rjf: update stack pointer
            regs->rsp = (U64)(regs->rsp + imm);
            
            // rjf: not a final instruction; keep parsing
            keep_parsing = 1;
          }break;
          
          // rjf: add $n,%rsp
          case 0x83:
          {
            // rjf: skip one byte (we already know what it is in this scenario)
            read_off += 1;
            
            // rjf: read the 1-byte immediate
            S8 imm = 0;
            B32 imm_is_good = (str8_deserial_read_struct(instruction_data, read_off, &imm) == sizeof(imm));
            if(!imm_is_good)
            {
              done = 1;
            }
            read_off += sizeof(imm);
            
            // rjf: update stack pointer
            regs->rsp = (U64)(regs->rsp + imm);
            
            // rjf: not a final instruction; keep parsing
            keep_parsing = 1;
          }break;
          
          // rjf: lea imm8/imm32,$rsp
          case 0x8D:
          {
            // rjf: read source register
            U8 modrm = 0;
            B32 modrm_is_good = (str8_deserial_read_struct(instruction_data, read_off, &modrm) == sizeof(modrm));
            if(!modrm_is_good)
            {
              done = 1;
            }
            read_off += 1;
            
            // rjf: unpack register value
            PE_UnwindGprRegX64 gpr_reg = (modrm & 7) + (rex & 1)*8;
            X64_RegCode reg_code = pe_x64_uwnd_reg_code_from_pe_gpr_reg(gpr_reg);
            Rng1U16 reg_rng = x64_reg_code_rng_table[reg_code];
            U64 *reg_ptr = (U64 *)((U8 *)regs + reg_rng.min);
            U64 reg_value = reg_ptr[0];
            if(!uwnd_reg_range_is_valid(caller_valid, reg_rng)) {done = 1;}
            
            // rjf: read immediate
            S32 imm = 0;
            if(!done)
            {
              // rjf: read 1-byte immediate
              if((modrm >> 6) == 1)
              {
                S8 imm8 = 0;
                B32 imm8_is_good = (str8_deserial_read_struct(instruction_data, read_off, &imm8) == sizeof(imm8));
                if(!imm8_is_good)
                {
                  done = 1;
                }
                read_off += 1;
                imm = (S32)imm8;
              }
              
              // rjf: read 4-byte immediate
              else
              {
                B32 imm32_is_good = (str8_deserial_read_struct(instruction_data, read_off, &imm) == sizeof(imm));
                done = !imm32_is_good;
                read_off += sizeof(imm);
              }
            }
            
            // rjf: update stack pointer
            if(!done)
            {
              regs->rsp = (U64)(reg_value + imm);
            }
            
            // rjf: not a final instruction; keep parsing
            keep_parsing = 1;
          }break;
          
          // rjf: ret $nn
          case 0xC2:
          {
            // rjf: read new ip
            U64 sp = regs->rsp;
            U64 new_ip = 0;
            if(memory_map_read_struct(memory_map, sp, &new_ip) != sizeof(new_ip))
            {
              done = 1;
              result.status = UWND_StepStatus_FailedMemoryRead;
              result.missed_read_vaddr_range = r1u64(sp, sp+sizeof(new_ip));
            }
            
            // rjf: read 2-byte immediate & advance stack pointer
            U16 imm = 0;
            if(!done)
            {
              done = (str8_deserial_read_struct(instruction_data, read_off, &imm) < sizeof(imm));
            }
            U64 new_sp = sp + 8 + imm;
            
            // rjf: commit registers
            if(!done)
            {
              regs->rip = new_ip;
              regs->rsp = new_sp;
            }
          }break;
          
          // rjf: ret / rep; ret
          case 0xF3:
          {
            // Assert(!"Hit me!");
          }break;
          case 0xC3:
          {
            // rjf: read new ip
            U64 sp = regs->rsp;
            U64 new_ip = 0;
            if(memory_map_read_struct(memory_map, sp, &new_ip) != sizeof(new_ip))
            {
              done = 1;
              result.status = UWND_StepStatus_FailedMemoryRead;
              result.missed_read_vaddr_range = r1u64(sp, sp+sizeof(new_ip));
            }
            
            // rjf: advance stack pointer
            U64 new_sp = sp + 8;
            
            // rjf: commit registers
            if(!done)
            {
              regs->rip = new_ip;
              regs->rsp = new_sp;
            }
          }break;
          
          // rjf: jmp nnnn
          case 0xE9:
          {
            // Assert(!"Hit Me");
            // TODO(allen): general idea: read the immediate, move the ip, leave the sp, done
            // we don't have any cases to exercise this right now. no guess implementation!
          }break;
          
          // rjf: Sjmp n
          case 0xEB:
          {
            // Assert(!"Hit Me");
            // TODO(allen): general idea: read the immediate, move the ip, leave the sp, done
            // we don't have any cases to exercise this right now. no guess implementation!
          }break;
        }
      }
    }
    
    //////////////////////////////
    //- rjf: pdata & not in epilog -> xdata unwind
    //
    B32 xdata_unwind_did_machframe = 0;
    if(!done && first_pdata && !has_pdata_and_in_epilog)
    {
      //- rjf: get frame reg
      B32 bad_frame_reg_info = 0;
      X64_RegCode frame_reg_code = X64_RegCode_rsp;
      U64 frame_off = 0;
      {
        // rjf: read unwind info header
        U64 unwind_info_off = first_pdata->voff_unwind_info;
        PE_UnwindInfo unwind_info = {0};
        if(memory_map_read_struct(memory_map, module_info->base_vaddr+unwind_info_off, &unwind_info) != sizeof(unwind_info))
        {
          done = 1;
          result.status = UWND_StepStatus_FailedMemoryRead;
          result.missed_read_vaddr_range = r1u64(module_info->base_vaddr+unwind_info_off, module_info->base_vaddr+unwind_info_off+sizeof(unwind_info));;
        }
        
        // rjf: unpack header
        U32 frame_reg_id = PE_UNWIND_INFO_REG_FROM_FRAME(unwind_info.frame);
        U64 frame_off_val = PE_UNWIND_INFO_OFF_FROM_FRAME(unwind_info.frame);
        if(frame_reg_id != 0)
        {
          frame_reg_code = pe_x64_uwnd_reg_code_from_pe_gpr_reg(frame_reg_id);
          bad_frame_reg_info = (frame_reg_code == X64_RegCode_nil); // NOTE(rjf): frame_reg should never be 0 at this point, in valid exe
        }
        frame_off = frame_off_val;
      }
      
      //- rjf: iterate pdatas, apply opcodes
      PE_IntelPdata *last_pdata = 0;
      PE_IntelPdata *pdata = first_pdata;
      if(!done && !bad_frame_reg_info) for(B32 keep_parsing = 1; !done && keep_parsing && pdata != last_pdata;)
      {
        //- rjf: read unwind info
        U64 unwind_info_off = pdata->voff_unwind_info;
        PE_UnwindInfo unwind_info = {0};
        if(memory_map_read_struct(memory_map, module_info->base_vaddr+unwind_info_off, &unwind_info) != sizeof(unwind_info))
        {
          done = 1;
          result.status = UWND_StepStatus_FailedMemoryRead;
          result.missed_read_vaddr_range = r1u64(module_info->base_vaddr+unwind_info_off, module_info->base_vaddr+unwind_info_off+sizeof(unwind_info));;
        }
        
        //- rjf: read unwind codes
        PE_UnwindCode *unwind_codes = push_array(scratch.arena, PE_UnwindCode, unwind_info.codes_num);
        Rng1U64 unwind_codes_vaddr_range = r1u64(module_info->base_vaddr+unwind_info_off+sizeof(unwind_info),
                                                 module_info->base_vaddr+unwind_info_off+sizeof(unwind_info)+sizeof(PE_UnwindCode)*unwind_info.codes_num);
        if(memory_map_read(memory_map, unwind_codes_vaddr_range, unwind_codes) != dim_1u64(unwind_codes_vaddr_range))
        {
          done = 1;
          result.status = UWND_StepStatus_FailedMemoryRead;
          result.missed_read_vaddr_range = unwind_codes_vaddr_range;;
        }
        
        //- rjf: unpack frame base
        U64 frame_base = regs->rsp;
        if(frame_reg_code != X64_RegCode_nil)
        {
          Rng1U16 frame_reg_rng = x64_reg_code_rng_table[frame_reg_code];
          if(!uwnd_reg_range_is_valid(caller_valid, frame_reg_rng)) {done = 1; break;}
          U64 raw_frame_base = *(U64 *)((U8 *)regs + frame_reg_rng.min);
          U64 adjusted_frame_base = raw_frame_base - frame_off*16;
          frame_base = adjusted_frame_base;
        }
        
        //- rjf: apply opcodes
        PE_UnwindCode *code_ptr = unwind_codes;
        PE_UnwindCode *code_opl = unwind_codes + unwind_info.codes_num;
        for(PE_UnwindCode *next_code_ptr = 0; !done && code_ptr < code_opl; code_ptr = next_code_ptr)
        {
          // rjf: unpack opcode info
          U32 op_code = PE_UNWIND_OPCODE_FROM_FLAGS(code_ptr->flags);
          U32 op_info = PE_UNWIND_INFO_FROM_FLAGS(code_ptr->flags);
          U32 slot_count = pe_slot_count_from_unwind_op_code(op_code);
          if(op_code == PE_UnwindOpCode_ALLOC_LARGE && op_info == 1)
          {
            slot_count += 1;
          }
          
          // rjf: detect bad slot counts
          if(slot_count == 0 || code_ptr+slot_count > code_opl)
          {
            keep_parsing = 0;
            done = 1;
            break;
          }
          
          // rjf: set next op code pointer
          next_code_ptr = code_ptr + slot_count;
          
          // rjf: interpret this op code
          U64 code_voff = pdata->voff_first + code_ptr->off_in_prolog;
          if(code_voff <= rip_voff)
          {
            switch(op_code)
            {
              case PE_UnwindOpCode_PUSH_NONVOL:
              {
                // rjf: read value from stack pointer
                U64 rsp = regs->rsp;
                U64 value = 0;
                if(memory_map_read_struct(memory_map, rsp, &value) != sizeof(value))
                {
                  done = 1;
                  result.status = UWND_StepStatus_FailedMemoryRead;
                  result.missed_read_vaddr_range = r1u64(rsp, rsp+sizeof(value));
                }
                
                // rjf: commit registers
                if(!done)
                {
                  X64_RegCode reg_code = pe_x64_uwnd_reg_code_from_pe_gpr_reg(op_info);
                  Rng1U16 reg_rng = x64_reg_code_rng_table[reg_code];
                  *(U64 *)((U8 *)regs + reg_rng.min) = value;
                  uwnd_reg_set_valid(caller_valid, reg_rng, 1);
                  regs->rsp = rsp + 8;
                }
              }break;
              
              case PE_UnwindOpCode_ALLOC_LARGE:
              {
                // rjf: read alloc size
                U64 size = 0;
                if(op_info == 0)
                {
                  size = code_ptr[1].u16*8;
                }
                else if(op_info == 1)
                {
                  size = code_ptr[1].u16 + ((U32)code_ptr[2].u16 << 16);
                }
                else
                {
                  done = 1;
                }
                
                // rjf: advance stack pointer
                if(!done)
                {
                  regs->rsp += size;
                }
              }break;
              
              case PE_UnwindOpCode_ALLOC_SMALL:
              {
                // rjf: advance stack pointer
                regs->rsp += op_info*8 + 8;
              }break;
              
              case PE_UnwindOpCode_SET_FPREG:
              {
                // rjf: put stack pointer back to the frame base
                regs->rsp = frame_base;
              }break;
              
              case PE_UnwindOpCode_SAVE_NONVOL:
              {
                // rjf: read value from frame base
                U64 off = code_ptr[1].u16*8;
                U64 addr = frame_base + off;
                U64 value = 0;
                if(memory_map_read_struct(memory_map, addr, &value) != sizeof(value))
                {
                  done = 1;
                  result.status = UWND_StepStatus_FailedMemoryRead;
                  result.missed_read_vaddr_range = r1u64(addr, addr+sizeof(value));
                }
                
                // rjf: commit to register
                if(!done)
                {
                  X64_RegCode reg_code = pe_x64_uwnd_reg_code_from_pe_gpr_reg(op_info);
                  Rng1U16 reg_rng = x64_reg_code_rng_table[reg_code];
                  *(U64 *)((U8 *)regs + reg_rng.min) = value;
                  uwnd_reg_set_valid(caller_valid, reg_rng, 1);
                }
              }break;
              
              case PE_UnwindOpCode_SAVE_NONVOL_FAR:
              {
                // rjf: read value from frame base
                U64 off = code_ptr[1].u16 + ((U32)code_ptr[2].u16 << 16);
                U64 addr = frame_base + off;
                U64 value = 0;
                if(memory_map_read_struct(memory_map, addr, &value) != sizeof(value))
                {
                  done = 1;
                  result.status = UWND_StepStatus_FailedMemoryRead;
                  result.missed_read_vaddr_range = r1u64(addr, addr+sizeof(value));
                }
                
                // rjf: commit to register
                if(!done)
                {
                  X64_RegCode reg_code = pe_x64_uwnd_reg_code_from_pe_gpr_reg(op_info);
                  Rng1U16 reg_rng = x64_reg_code_rng_table[reg_code];
                  *(U64 *)((U8 *)regs + reg_rng.min) = value;
                  uwnd_reg_set_valid(caller_valid, reg_rng, 1);
                }
              }break;
              
              case PE_UnwindOpCode_EPILOG:
              {
                keep_parsing = 1;
              }break;
              
              case PE_UnwindOpCode_SPARE_CODE:
              {
                // TODO(rjf): ???
                keep_parsing = 0;
                done = 1;
              }break;
              
              case PE_UnwindOpCode_SAVE_XMM128:
              {
                // rjf: read new register values
                U8 buf[16];
                U64 off = code_ptr[1].u16*16;
                U64 addr = frame_base + off;
                Rng1U64 read_vaddr_range = r1u64(addr, addr+sizeof(buf));
                if(memory_map_read(memory_map, read_vaddr_range, buf) != sizeof(buf))
                {
                  done = 1;
                  result.status = UWND_StepStatus_FailedMemoryRead;
                  result.missed_read_vaddr_range = read_vaddr_range;
                }
                
                // rjf: commit to register
                if(!done)
                {
                  void *xmm_reg = (&regs->zmm0) + op_info;
                  MemoryCopy(xmm_reg, buf, sizeof(buf));
                  U64 off = (U8 *)xmm_reg - (U8 *)regs;
                  uwnd_reg_set_valid(caller_valid, r1u16(off, off+sizeof(buf)), 1);
                }
              }break;
              
              case PE_UnwindOpCode_SAVE_XMM128_FAR:
              {
                // rjf: read new register values
                U8 buf[16];
                U64 off = code_ptr[1].u16 + ((U32)code_ptr[2].u16 << 16);
                U64 addr = frame_base + off;
                Rng1U64 read_vaddr_range = r1u64(addr, addr+sizeof(buf));
                if(memory_map_read(memory_map, read_vaddr_range, buf) != sizeof(buf))
                {
                  done = 1;
                  result.status = UWND_StepStatus_FailedMemoryRead;
                  result.missed_read_vaddr_range = read_vaddr_range;
                }
                
                // rjf: commit to register
                if(!done)
                {
                  void *xmm_reg = (&regs->zmm0) + op_info;
                  MemoryCopy(xmm_reg, buf, sizeof(buf));
                  U64 off = (U8 *)xmm_reg - (U8 *)regs;
                  uwnd_reg_set_valid(caller_valid, r1u16(off, off+sizeof(buf)), 1);
                }
              }break;
              
              case PE_UnwindOpCode_PUSH_MACHFRAME:
              {
                // NOTE(rjf): this was found by stepping through kernel code after an exception was
                // thrown, encountered in the exception_stepping_tests (after the throw) in mule_main
                if(op_info > 1)
                {
                  done = 1;
                }
                
                // rjf: unpack stack pointer
                U64 sp_og = regs->rsp;
                U64 sp_adj = sp_og;
                if(op_info == 1)
                {
                  sp_adj += 8;
                }
                U64 sp_after_ip = sp_adj + 8;
                U64 sp_after_ss = sp_after_ip + 8;
                U64 sp_after_rflags = sp_after_ss + 8;
                
                // rjf: read new ip value
                U64 ip_value = 0;
                if(!done && memory_map_read_struct(memory_map, sp_adj, &ip_value) != sizeof(ip_value))
                {
                  done = 1;
                  result.status = UWND_StepStatus_FailedMemoryRead;
                  result.missed_read_vaddr_range = r1u64(sp_adj, sp_adj + sizeof(ip_value));
                }
                
                // rjf: read new ss value
                U16 ss_value = 0;
                if(!done && memory_map_read_struct(memory_map, sp_after_ip, &ss_value) != sizeof(ss_value))
                {
                  done = 1;
                  result.status = UWND_StepStatus_FailedMemoryRead;
                  result.missed_read_vaddr_range = r1u64(sp_after_ip, sp_after_ip + sizeof(ss_value));
                }
                
                // rjf: read new rflags value
                U64 rflags_value = 0;
                if(!done && memory_map_read_struct(memory_map, sp_after_ss, &rflags_value) != sizeof(rflags_value))
                {
                  done = 1;
                  result.status = UWND_StepStatus_FailedMemoryRead;
                  result.missed_read_vaddr_range = r1u64(sp_after_ss, sp_after_ss + sizeof(rflags_value));
                }
                
                // rjf: read new sp value
                U64 sp_value = 0;
                if(!done && memory_map_read_struct(memory_map, sp_after_rflags, &sp_value) != sizeof(sp_value))
                {
                  done = 1;
                  result.status = UWND_StepStatus_FailedMemoryRead;
                  result.missed_read_vaddr_range = r1u64(sp_after_rflags, sp_after_rflags + sizeof(sp_value));
                }
                
                // rjf: commit registers
                if(!done)
                {
                  regs->rip    = ip_value;
                  regs->ss     = ss_value;
                  regs->rflags = rflags_value;
                  regs->rsp    = sp_value;
                  uwnd_reg_mark_valid(arch, caller_valid, X64_RegCode_ss, 0);
                  uwnd_reg_mark_valid(arch, caller_valid, X64_RegCode_rflags, 0);
                }
                
                // rjf: mark machine frame
                xdata_unwind_did_machframe = !done;
              }break;
            }
          }
        }
        
        //- rjf: iterate to next pdata
        if(keep_parsing)
        {
          U32 flags = PE_UNWIND_INFO_FLAGS_FROM_HDR(unwind_info.header);
          if(!(flags & PE_UnwindInfoFlag_CHAINED))
          {
            break;
          }
          U64 code_count_rounded = AlignPow2(unwind_info.codes_num, sizeof(PE_UnwindCode));
          U64 code_size = code_count_rounded*sizeof(PE_UnwindCode);
          U64 chained_pdata_off = unwind_info_off + sizeof(PE_UnwindInfo) + code_size;
          last_pdata = pdata;
          pdata = push_array(scratch.arena, PE_IntelPdata, 1);
          if(memory_map_read_struct(memory_map, module_info->base_vaddr+chained_pdata_off, pdata) != sizeof(*pdata))
          {
            done = 1;
            result.status = UWND_StepStatus_FailedMemoryRead;
            result.missed_read_vaddr_range = r1u64(module_info->base_vaddr+chained_pdata_off, module_info->base_vaddr+chained_pdata_off+sizeof(*pdata));
          }
        }
      }
    }
    
    //////////////////////////////
    //- rjf: no pdata, or didn't do machframe in xdata unwind -> module-less unwind
    //
    if(!done && (!first_pdata || (!has_pdata_and_in_epilog && !xdata_unwind_did_machframe)))
    {
      result = uwnd_step_moduleless(arch, memory_map, regs, caller_valid);
      done = (result.status != UWND_StepStatus_Good);
    }
    
    //////////////////////////////
    //- rjf: if not done -> successful step
    //
    if(!done)
    {
      result.status = UWND_StepStatus_Good;
      uwnd_reg_mark_valid(arch, caller_valid, X64_RegCode_rip, 0);
      uwnd_reg_mark_valid(arch, caller_valid, X64_RegCode_rsp, 0);
      if(reg_valid) {MemoryCopy(reg_valid, caller_valid, sizeof(*regs));}
    }
    
    scratch_end(scratch);
  }
  return result;
}
