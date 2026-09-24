// Read-only scalar evaluation for a stop/thread/frame named by the caller.
// This context owns its cache and never selects a GUI frame or display rule.

typedef struct RD_DebugEvalRead RD_DebugEvalRead;
struct RD_DebugEvalRead
{
  D_Entity *process;
  D_Entity *thread;
  void *regs;
  U8 *reg_valid;
  U64 endt_us;
  U64 read_bytes;
  B32 pending;
  B32 stale;
  B32 unavailable;
  B32 unsupported;
};

thread_static RD_DebugEvalRead *rd_debug_eval_read;

internal B32
rd_debug_eval_space_read(E_Space space, void *out, E_SpaceRangeInfo *info, Rng1U64 range)
{
  RD_DebugEvalRead *ctx = rd_debug_eval_read;
  B32 result = 0;
  U64 size = dim_1u64(range);
  if(range.max < range.min || size > KB(64) || ctx->read_bytes > KB(64)-size)
  {
    ctx->unsupported = 1;
    return 0;
  }
  ctx->read_bytes += size;
  if(space.kind == D_EvalSpaceKind_Entity && space.u64_0 == (U64)ctx->thread)
  {
    U64 regs_size = arch_info_from_arch(ctx->thread->arch)->reg_block_size;
    result = (range.max <= regs_size);
    for(U64 idx = range.min; result && ctx->reg_valid && idx < range.max; idx += 1)
    {
      result = ctx->reg_valid[idx] != 0;
    }
    if(result) {MemoryCopy(out, (U8 *)ctx->regs+range.min, size);}
    else {ctx->unavailable = 1;}
  }
  else if(space.kind == D_EvalSpaceKind_Entity && space.u64_0 == (U64)ctx->process)
  {
    Temp scratch = scratch_begin(0, 0);
    D_ProcessMemorySlice slice = d_process_memory_slice_from_vaddr_range(scratch.arena, ctx->process->handle, range, 1, ctx->endt_us);
    ctx->pending |= slice.stale;
    result = !slice.stale && !slice.any_byte_bad && slice.data.size == size;
    if(result) {MemoryCopy(out, slice.data.str, size);}
    else if(!slice.stale) {ctx->unavailable = 1;}
    scratch_end(scratch);
  }
  else {ctx->unsupported = 1;}
  if(!result && info)
  {
    info->flags |= ctx->pending ? E_SpaceRangeFlag_Stale : E_SpaceRangeFlag_AnyByteBad;
  }
  return result;
}

internal String8
rd_debug_eval(Arena *arena, String8 request)
{
  Temp scratch = scratch_begin(&arena, 1);
  Access *access = access_open();
  U64 endt_us = now_time_us()+10000;
  U64 debug_info_gen = di_load_gen();
  String8 status = str8_lit("invalid_request");
  String8 reason = str8_lit("Expected stop generations, process_handle, thread_id, frame, frame_pc, unwind_index, inline_depth and expr.");
  String8 value = str8_lit("null");
  String8 type = {0};
  String8 expression = {0};
  String8 process_string = {0};
  U64 nums[9] = {0};
  B32 valid_request = 0;
  RD_DebugEvalRead read = {0};
  E_Cache *cache = 0;
  E_Cache *saved_cache = e_cache;
  E_BaseCtx *saved_base = e_base_ctx;
  E_IRCtx *saved_ir = e_ir_ctx;
  E_InterpretCtx *saved_interpret = e_interpret_ctx;
  RD_DebugEvalRead *saved_read = rd_debug_eval_read;

  // Parse a flat Metadesk request, rejecting unknown, missing or repeated fields.
  MD_ParseResult parsed = md_parse_from_text(scratch.arena, str8_zero(), request.size <= KB(8) ? request : str8_zero());
  String8 names[] = {str8_lit("run_gen"), str8_lit("stop_count"), str8_lit("mem_gen"), str8_lit("reg_gen"),
                    str8_lit("thread_id"), str8_lit("frame"), str8_lit("frame_pc"), str8_lit("unwind_index"),
                    str8_lit("inline_depth"), str8_lit("process_handle"), str8_lit("expr")};
  U32 seen = 0;
  B32 malformed = request.size > KB(8) || parsed.msgs.worst_message_kind >= MD_MsgKind_Error;
  for(MD_Node *n = parsed.root->first; !md_node_is_nil(n); n = n->next)
  {
    U64 idx = 0;
    for(; idx < ArrayCount(names); idx += 1) {if(str8_match(n->string, names[idx], 0)) {break;}}
    if(idx == ArrayCount(names) || (seen & (1u << idx)) || md_node_is_nil(n->first) || !md_node_is_nil(n->first->next))
    {
      malformed = 1;
      break;
    }
    seen |= 1u << idx;
    if(idx < ArrayCount(nums))
    {
      if(!try_u64_from_str8_c_rules(n->first->string, &nums[idx])) {malformed = 1;}
    }
    else if(idx == 9) {process_string = n->first->string;}
    else {expression = raw_from_escaped_str8(scratch.arena, n->first->string);}
  }
  valid_request = !malformed && seen == (1u << ArrayCount(names))-1 && expression.size != 0 && expression.size <= KB(4);
  if(valid_request)
  {
    U64 run_gen = nums[0], stop_count = nums[1], mem_gen = nums[2], reg_gen = nums[3];
    U64 thread_id = nums[4], frame_idx = nums[5];
    D_Entity *process = d_entity_from_handle(d_handle_from_string(process_string));
    D_Entity *thread = &d_entity_nil;
    for(D_Entity *t = process->first; t != &d_entity_nil; t = t->next)
    {
      if(t->kind == D_EntityKind_Thread && t->id == thread_id) {thread = t; break;}
    }
    if(run_gen != d_run_gen() || stop_count != d_stop_count() || mem_gen != d_mem_gen() || reg_gen != d_reg_gen())
    {
      status = str8_lit("stale"); reason = str8_lit("The requested stop or memory/register generation has changed.");
    }
    else if(d_ctrl_targets_running())
    {
      status = str8_lit("running"); reason = str8_lit("Evaluation requires stopped targets.");
    }
    else if(process->kind != D_EntityKind_Process || thread == &d_entity_nil)
    {
      reason = str8_lit("The requested process/thread does not exist.");
    }
    else
    {
      B32 stack_stale = 0;
      AC_Artifact artifact = ac_artifact_from_key(access, str8_struct(&thread->handle),
                                                 d_call_stack_artifact_create, d_call_stack_artifact_destroy, endt_us,
                                                 .gen = mem_gen+reg_gen, .evict_threshold_us = 10000000,
                                                 .flags = AC_Flag_HighPriority|AC_Flag_WaitForFresh, .stale_out = &stack_stale);
      D_CallStack *stack = (D_CallStack *)artifact.u64[1];
      status = str8_lit("pending"); reason = str8_lit("Waiting for a fresh stack.");
      if(stack && !stack_stale)
      {
        if(stack->unwind_flags & D_UnwindFlag_Error)
        {
          status = str8_lit("unavailable"); reason = str8_lit("Stack reconstruction failed.");
        }
        else if(frame_idx >= stack->frames_count)
        {
          status = str8_lit("invalid_request"); reason = str8_lit("Frame index is outside this stack.");
        }
        else if(arch_ip_from_reg_block(arch_info_from_arch(thread->arch), stack->frames[frame_idx].regs) != nums[6] ||
                stack->frames[frame_idx].unwind_count != nums[7] || stack->frames[frame_idx].inline_depth != nums[8])
        {
          status = str8_lit("stale"); reason = str8_lit("The frame no longer matches the supplied stack observation.");
        }
        else
        {
          D_CallStackFrame *frame = &stack->frames[frame_idx];
          ARCH_Info *arch = arch_info_from_arch(thread->arch);
          U64 pc = frame->lookup_vaddr;
          D_Entity *module = d_module_from_process_vaddr(process, pc);
          U64 voff = d_voff_from_vaddr(module, pc);
          RDI_Parsed *rdi = di_rdi_from_key(access, d_dbgi_key_from_module(module), 1, endt_us);
          status = str8_lit("unavailable"); reason = str8_lit("No debug info for this frame.");
          if(rdi->raw_data_size == 0)
          {
            DI_LoadStatus load_status = di_load_status_from_key(d_dbgi_key_from_module(module));
            if(load_status == DI_LoadStatus_Pending || load_status == DI_LoadStatus_Ready)
            {
              status = str8_lit("pending"); reason = str8_lit("Waiting for debug info.");
            }
          }
          else if(frame->inline_depth || rdi_inline_scope_from_scope(rdi, rdi_scope_from_voff(rdi, voff))->inline_site_idx)
          {
            status = str8_lit("unsupported"); reason = str8_lit("Inline-scope evaluation is not implemented by this command.");
          }
          else
          {
            // All evaluator pointers are restored before this scratch/cache is released.
            cache = e_cache_alloc();
            e_select_cache(cache);
            E_BaseCtx base = {0};
            E_IRCtx ir = {0};
            E_InterpretCtx interpret = {0};
            E_Space process_space = e_space_make(D_EvalSpaceKind_Entity); process_space.u64_0 = (U64)process;
            E_Space reg_space = e_space_make(D_EvalSpaceKind_Entity); reg_space.u64_0 = (U64)thread;
            read.process = process; read.thread = thread; read.endt_us = endt_us;
            read.regs = frame->regs; read.reg_valid = frame->reg_valid;
            void *live_regs = push_array(scratch.arena, U8, arch->reg_block_size);
            if(!d_thread_read_reg_block(thread->handle, live_regs)) {read.unavailable = 1;}
            else if(!MemoryMatch(live_regs, stack->frames[0].regs, arch->reg_block_size)) {read.stale = 1;}
            if(frame->unwind_count == 0)
            {
              read.regs = live_regs;
            }
            rd_debug_eval_read = &read;
            base.thread_ip_voff = voff;
            base.thread_reg_space = reg_space;
            base.thread_process_space = process_space;
            base.thread_arch = thread->arch;
            base.thread_unwind_count = frame->unwind_count;
            base.debug_info_match_endt_us = endt_us;
            base.space_read = rd_debug_eval_space_read;
            D_EntityArray modules = d_entity_array_from_kind(D_EntityKind_Module);
            base.modules = push_array(scratch.arena, E_Module, modules.count);
            base.dbg_infos = push_array(scratch.arena, E_DbgInfo, modules.count);
            E_String2NumMap module_names = e_string2num_map_make(scratch.arena, 64);
            E_String2NumMap info_names = e_string2num_map_make(scratch.arena, 64);
            base.module_from_name_map = &module_names; base.dbg_info_from_name_map = &info_names;
            for(U64 idx = 0; idx < modules.count; idx += 1)
            {
              D_Entity *m = modules.v[idx];
              if(d_entity_ancestor_from_kind(m, D_EntityKind_Process) != process) {continue;}
              U64 num = base.modules_count++;
              base.dbg_infos_count += 1;
              E_Module *em = &base.modules[num];
              E_DbgInfo *ed = &base.dbg_infos[num];
              em->arch = m->arch; em->vaddr_range = m->vaddr_range; em->space = process_space;
              em->name = m->string; em->dbg_info_num = num+1;
              ed->dbgi_key = d_dbgi_key_from_module(m);
              ed->rdi = m == module ? rdi : di_rdi_from_key(access, ed->dbgi_key, 1, endt_us);
              ed->name = d_entity_child_from_kind(m, D_EntityKind_DebugInfoPath)->string;
              if(ed->rdi->raw_data_size == 0 && di_load_status_from_key(ed->dbgi_key) == DI_LoadStatus_Pending) {read.pending = 1;}
              e_string2num_map_insert(scratch.arena, &module_names, m->string, num+1);
              e_string2num_map_insert(scratch.arena, &module_names, str8_skip_last_slash(m->string), num+1);
              e_string2num_map_insert(scratch.arena, &info_names, ed->name, num+1);
              e_string2num_map_insert(scratch.arena, &info_names, str8_skip_last_slash(ed->name), num+1);
              if(m == module) {base.primary_module = em; base.primary_dbg_info = ed;}
            }
            e_select_base_ctx(&base);
            ir.regs_map = d_string2reg_from_arch(thread->arch);
            ir.locals_map = e_push_locals_map_from_rdi_voff(scratch.arena, rdi, voff);
            ir.member_map = e_push_member_map_from_rdi_voff(scratch.arena, rdi, voff);
            e_select_ir_ctx(&ir);
            U64 frame_base = 0, module_base = module->vaddr_range.min;
            interpret.primary_space = process_space; interpret.reg_space = reg_space;
            interpret.reg_arch = thread->arch; interpret.reg_unwind_count = frame->unwind_count;
            interpret.module_base = &module_base; interpret.frame_base = &frame_base; interpret.cfa = frame->cfa;
            e_select_interpret_ctx(&interpret, rdi, voff);
            E_Eval eval = e_eval_from_string(expression);
            E_TypeKey key = e_type_key_unwrap(eval.irtree.type_key, E_TypeUnwrapFlag_AllDecorative);
            E_TypeKind kind = e_type_kind_from_key(key);
            U64 size = e_type_byte_size_from_key(key);
            type = e_type_string_from_key(scratch.arena, eval.irtree.type_key);
            B32 scalar = size != 0 && size <= sizeof(E_Value) &&
              (e_type_kind_is_basic_or_enum(kind) || kind == E_TypeKind_Ptr || kind == E_TypeKind_Bitfield);
            if(scalar && eval.code == E_InterpretationCode_Good && eval.msgs.count == 0) {eval = e_value_eval_from_eval(eval);}
            status = str8_lit("error"); reason = str8_lit("Expression did not produce a value.");
            if(eval.msgs.first) {reason = eval.msgs.first->text;}
            if(eval.code != E_InterpretationCode_Good) {reason = e_interpretation_code_display_strings[eval.code];}
            if(read.stale) {status = str8_lit("stale"); reason = str8_lit("Live registers do not match the reconstructed stack.");}
            else if(read.pending) {status = str8_lit("pending"); reason = str8_lit("Waiting for fresh debug info or memory.");}
            else if(read.unsupported || eval.code == E_InterpretationCode_BadTLSBase)
            {
              status = str8_lit("unsupported"); reason = str8_lit("Unsupported space, TLS access or read budget exceeded.");
            }
            else if(read.unavailable || eval.code == E_InterpretationCode_BadMemRead || eval.code == E_InterpretationCode_BadRegRead || eval.code == E_InterpretationCode_BadFrameBase)
            {
              status = str8_lit("unavailable"); reason = str8_lit("Required memory or register data is unavailable.");
            }
            else if(eval.code == E_InterpretationCode_Good && eval.msgs.count == 0)
            {
              status = str8_lit("unsupported"); reason = str8_lit("Only scalar values are supported.");
              if(scalar && eval.irtree.mode == E_Mode_Value)
              {
                U8 *hex = push_array_no_zero(scratch.arena, U8, size*2);
                U8 digits[] = "0123456789abcdef";
                for(U64 idx = 0; idx < size; idx += 1)
                {
                  U8 byte = ((U8 *)&eval.value)[idx]; hex[2*idx] = digits[byte >> 4]; hex[2*idx+1] = digits[byte & 15];
                }
                value = push_str8f(scratch.arena, "{\"bytes\":\"%S\",\"byte_size\":%I64u,\"byte_order\":\"little\"}", str8(hex, size*2), size);
                status = str8_lit("ready"); reason = str8_zero();
              }
            }
          }
        }
      }
    }
    // No successfully read value survives a target-state change during the query.
    if(run_gen != d_run_gen() || stop_count != d_stop_count() || mem_gen != d_mem_gen() || reg_gen != d_reg_gen())
    {
      status = str8_lit("stale"); reason = str8_lit("Target state changed during evaluation.");
    }
    else if(d_ctrl_targets_running()) {status = str8_lit("running"); reason = str8_lit("Targets are running.");}
    else if(debug_info_gen != di_load_gen() && (str8_match(status, str8_lit("ready"), 0) || str8_match(status, str8_lit("error"), 0)))
    {
      status = str8_lit("pending"); reason = str8_lit("Debug info changed during evaluation.");
    }
  }
  e_cache = saved_cache; e_base_ctx = saved_base; e_ir_ctx = saved_ir; e_interpret_ctx = saved_interpret;
  rd_debug_eval_read = saved_read;
  if(!str8_match(status, str8_lit("ready"), 0)) {value = str8_lit("null");}
  String8 result = push_str8f(arena,
    "{\"version\":1,\"status\":\"%S\",\"complete\":%s,\"reason\":%S,\"type\":%S,\"value\":%S,"
    "\"context\":{\"run_gen\":%I64u,\"stop_count\":%I64u,\"mem_gen\":%I64u,\"reg_gen\":%I64u,"
    "\"thread_id\":%I64u,\"frame\":%I64u,\"frame_pc\":\"0x%I64x\",\"unwind_index\":%I64u,\"inline_depth\":%I64u,\"process_handle\":%S}}\n",
    status, str8_match(status, str8_lit("pending"), 0) ? "false" : "true", rd_snapshot_json_string(scratch.arena, reason),
    rd_snapshot_json_string(scratch.arena, type), value, nums[0], nums[1], nums[2], nums[3], nums[4], nums[5],
    nums[6], nums[7], nums[8], rd_snapshot_json_string(scratch.arena, process_string));
  if(result.size > KB(60)) {result = str8_copy(arena, str8_lit("{\"version\":1,\"status\":\"too_large\",\"complete\":true,\"value\":null}\n"));}
  if(cache) {e_cache_release(cache);}
  access_close(access);
  scratch_end(scratch);
  return result;
}
