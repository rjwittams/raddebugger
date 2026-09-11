// Versioned, read-only observations for serialized IPC clients. This does not
// change the selected frame or borrow the GUI's expression evaluation context.

internal String8
rd_snapshot_json_string(Arena *arena, String8 value)
{
  U8 *out = push_array_no_zero(arena, U8, value.size*6+3);
  U64 size = 0;
  out[size++] = '"';
  for(U64 i = 0; i < value.size; i += 1)
  {
    U8 c = value.str[i];
    if(c == '"' || c == '\\') {out[size++] = '\\'; out[size++] = c;}
    else if(c < 32)
    {
      U8 hex[] = "0123456789abcdef";
      out[size++] = '\\'; out[size++] = 'u'; out[size++] = '0'; out[size++] = '0';
      out[size++] = hex[c >> 4]; out[size++] = hex[c & 15];
    }
    else {out[size++] = c;}
  }
  out[size++] = '"';
  return str8(out, size);
}

internal String8
rd_snapshot_stop_cause(D_EventCause cause)
{
  switch(cause)
  {
    case D_EventCause_Error:                  return str8_lit("error");
    case D_EventCause_Finished:               return str8_lit("finished");
    case D_EventCause_EntryPoint:             return str8_lit("entry_point");
    case D_EventCause_UserBreakpoint:         return str8_lit("breakpoint");
    case D_EventCause_InterruptedByTrap:      return str8_lit("trap");
    case D_EventCause_InterruptedByException: return str8_lit("exception");
    case D_EventCause_InterruptedByHalt:      return str8_lit("halt");
    default:                                 return str8_lit("none");
  }
}

internal D_Line
rd_snapshot_source_line(Arena *arena, RDI_Parsed *rdi, RDI_LineTable *table, String8 dbgi_path, U64 voff)
{
  D_Line result = {0};
  RDI_ParsedLineTable parsed = {0};
  rdi_parsed_from_line_table(rdi, table, &parsed);
  U64 idx = rdi_line_info_idx_from_voff(&parsed, voff);
  if(idx < parsed.count)
  {
    RDI_Line *line = &parsed.lines[idx];
    RDI_SourceFile *file = rdi_element_from_name_idx(rdi, SourceFiles, line->file_idx);
    // RDI's normal-path lookup string is case-folded. Reconstruct the display
    // path from its nodes so case-sensitive source paths survive this query.
    String8List parts = {0};
    for(RDI_FilePathNode *node = rdi_element_from_name_idx(rdi, FilePathNodes, file->file_path_node_idx);
        node != rdi_element_from_name_idx(rdi, FilePathNodes, 0);
        node = rdi_element_from_name_idx(rdi, FilePathNodes, node->parent_path_node))
    {
      String8 part = {0};
      part.str = rdi_string_from_idx(rdi, node->name_string_idx, &part.size);
      str8_list_push_front(arena, &parts, part);
    }
    StringJoin join = {0};
    join.sep = str8_lit("/");
    String8 path = str8_list_join(arena, &parts, &join);
    if(line->file_idx != 0 && path.size != 0)
    {
      result.file_path = path_absolute_dst_from_relative_dst_src(arena, path, str8_chop_last_slash(dbgi_path));
    }
    result.pt = txt_pt(line->line_num, idx < parsed.col_count ? parsed.cols[idx].col_first : 1);
    result.voff_range = r1u64(parsed.voffs[idx], parsed.voffs[idx+1]);
  }
  return result;
}

internal String8
rd_debug_snapshot(Arena *arena)
{
  Temp scratch = scratch_begin(&arena, 1);
  Access *access = access_open();
  U64 run_gen = d_run_gen(), stop_count = d_stop_count();
  U64 mem_gen = d_mem_gen(), reg_gen = d_reg_gen();
  B32 running = d_ctrl_targets_running();
  D_Event stop = d_ctrl_last_stop_event();
  D_Entity *thread = d_entity_from_handle(stop.entity);
  String8 selection = str8_lit("stop");
  if(thread->kind != D_EntityKind_Thread)
  {
    thread = d_entity_from_handle(rd_base_regs()->thread);
    selection = str8_lit("selected");
  }
  if(thread->kind != D_EntityKind_Thread) {thread = &d_entity_nil; selection = str8_lit("none");}
  D_Entity *process = d_entity_ancestor_from_kind(thread, D_EntityKind_Process);
  D_EntityArray processes = d_entity_array_from_kind(D_EntityKind_Process);
  D_CallStack stack = {0};
  B32 stack_stale = 0;
  String8 stack_status = str8_lit("none");
  if(!running && thread != &d_entity_nil)
  {
    AC_Artifact artifact = ac_artifact_from_key(access, str8_struct(&thread->handle),
                                               d_call_stack_artifact_create, d_call_stack_artifact_destroy,
                                               now_time_us()+2000,
                                               .gen = mem_gen+reg_gen, .evict_threshold_us = 10000000,
                                               .flags = AC_Flag_HighPriority|AC_Flag_WaitForFresh,
                                               .stale_out = &stack_stale);
    stack_status = str8_lit("pending");
    if(artifact.u64[1] != 0 && !stack_stale)
    {
      stack = *(D_CallStack *)artifact.u64[1];
      stack_status = (stack.unwind_flags & D_UnwindFlag_Error) ? str8_lit("error") : str8_lit("ready");
    }
  }
  String8List body = {0};
  str8_list_pushf(scratch.arena, &body,
                 "\"build\":{\"revision\":\"%s\",\"label\":\"%s\"},"
                 "\"running\":%s,\"run_gen\":%I64u,\"stop_count\":%I64u,"
                 "\"mem_gen\":%I64u,\"reg_gen\":%I64u,"
                 "\"process_count\":%I64u,\"process_id\":%I64u,\"process_handle\":%S,\"thread_id\":%I64u,\"thread_selection\":\"%S\","
                 "\"stop\":{\"cause\":\"%S\",\"pc\":\"0x%I64x\",\"breakpoint_id\":\"%I64x\",\"exception_code\":%u},",
                 BUILD_GIT_HASH_FULL, BUILD_GIT_HASH, running ? "true" : "false", run_gen, stop_count, mem_gen, reg_gen,
                 processes.count, process->id, rd_snapshot_json_string(scratch.arena, d_string_from_handle(scratch.arena, process->handle)),
                 thread->id, selection, rd_snapshot_stop_cause(stop.cause), stop.rip_vaddr,
                 stop.cause == D_EventCause_UserBreakpoint ? stop.u64_code : 0, stop.exception_code);
  String8 exit_code = rd_state->ipc_process_end_has_exit_code ?
    push_str8f(scratch.arena, "%I64u", rd_state->ipc_process_end_code) : str8_lit("null");
  str8_list_pushf(scratch.arena, &body,
                 "\"last_process_end\":{\"sequence\":%I64u,\"process_id\":%I64u,\"exit_code\":%S},"
                 "\"stack\":{\"status\":\"%S\",\"unwind_flags\":%u,\"unwind_frame_count\":%I64u,\"frames\":[",
                 rd_state->ipc_process_end_sequence, rd_state->ipc_process_end_id, exit_code,
                 stack_status, stack.unwind_flags, stack.unwind_frame_count);
  B32 symbols_pending = 0;
  for(U64 idx = 0; idx < stack.frames_count; idx += 1)
  {
    D_CallStackFrame *frame = &stack.frames[idx];
    ARCH_Info *arch = arch_info_from_arch(thread->arch);
    U64 pc = arch_ip_from_reg_block(arch, frame->regs);
    U64 sp = arch_sp_from_reg_block(arch, frame->regs);
    D_Entity *module = d_module_from_process_vaddr(process, pc);
    U64 voff = d_voff_from_vaddr(module, pc);
    D_Entity *dbgi = d_entity_child_from_kind(module, D_EntityKind_DebugInfoPath);
    DI_Key key = d_dbgi_key_from_module(module);
    RDI_Parsed *rdi = di_rdi_from_key(access, key, 1, now_time_us()+1000);
    RDI_LineTable *line_table = rdi_line_table_from_unit(rdi, rdi_unit_from_voff(rdi, voff));
    String8 name = fully_qualified_str8_from_rdi_symbol(scratch.arena, rdi, rdi_procedure_from_voff(rdi, voff));
    if(frame->inline_depth != 0)
    {
      RDI_Scope *scope = rdi_inline_scope_from_scope(rdi, rdi_scope_from_voff(rdi, voff));
      U64 depth = 0;
      for(RDI_Scope *s = scope; s->inline_site_idx != 0; s = rdi_inline_scope_from_scope(rdi, rdi_parent_from_scope(rdi, s))) {depth += 1;}
      for(RDI_Scope *s = scope; s->inline_site_idx != 0; s = rdi_inline_scope_from_scope(rdi, rdi_parent_from_scope(rdi, s)), depth -= 1)
      {
        if(depth == frame->inline_depth)
        {
          RDI_InlineSite *site = rdi_element_from_name_idx(rdi, InlineSites, s->inline_site_idx);
          name.str = rdi_string_from_idx(rdi, site->name_string_idx, &name.size);
          line_table = rdi_element_from_name_idx(rdi, LineTables, site->line_table_idx);
          break;
        }
      }
    }
    // Select this frame's table directly. Missing inline line tables must not
    // shift the remaining frames onto somebody else's source location.
    D_Line line = rd_snapshot_source_line(scratch.arena, rdi, line_table, dbgi->string, voff);
    if(dbgi->string.size != 0 && rdi->raw_data_size == 0) {symbols_pending = 1;}
    str8_list_pushf(scratch.arena, &body,
                   "%s{\"id\":%I64u,\"unwind_index\":%I64u,\"inline_depth\":%I64u,"
                   "\"pc\":\"0x%I64x\",\"sp\":\"0x%I64x\",\"cfa\":\"0x%I64x\","
                   "\"module_offset\":\"0x%I64x\",\"module\":%S,\"function\":%S,"
                   "\"source\":{\"path\":%S,\"line\":%I64d,\"column\":%I64d,\"is_source\":%s}}",
                   idx ? "," : "", idx, frame->unwind_count, frame->inline_depth, pc, sp, frame->cfa, voff,
                   rd_snapshot_json_string(scratch.arena, module->string), rd_snapshot_json_string(scratch.arena, name),
                   rd_snapshot_json_string(scratch.arena, line.file_path), line.pt.line, line.pt.column,
                   d_line_has_source(&line) ? "true" : "false");
  }
  str8_list_pushf(scratch.arena, &body, "]},\"symbols_pending\":%s", symbols_pending ? "true" : "false");
  B32 stale = (run_gen != d_run_gen() || stop_count != d_stop_count() ||
               mem_gen != d_mem_gen() || reg_gen != d_reg_gen() || running != d_ctrl_targets_running());
  String8 status = stale ? str8_lit("stale") :
    (stack_stale || str8_match(stack_status, str8_lit("pending"), 0)) ? str8_lit("pending") : str8_lit("ready");
  String8 result = {0};
  // Leave space for the transport header. Never return a truncated JSON object.
  if(body.total_size > KB(60))
  {
    result = str8_copy(arena, str8_lit("{\"version\":1,\"status\":\"too_large\"}\n"));
  }
  else
  {
    result = push_str8f(arena, "{\"version\":1,\"status\":\"%S\",%S}\n", status,
                        str8_list_join(scratch.arena, &body, 0));
  }
  access_close(access);
  scratch_end(scratch);
  return result;
}
