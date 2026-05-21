// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal DMN_Handle
mac_dmn_handle_from_entity(MAC_DMN_Entity *entity)
{
  DMN_Handle result = {0};
  result.u64[0] = (U64)entity;
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_entity_from_handle(DMN_Handle handle)
{
  MAC_DMN_Entity *result = (MAC_DMN_Entity *)handle.u64[0];
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_entity_alloc(MAC_DMN_EntityKind kind)
{
  MAC_DMN_Entity *result = mac_dmn_state->free_entity;
  if(result != 0)
  {
    SLLStackPop(mac_dmn_state->free_entity);
  }
  else
  {
    result = push_array_no_zero(mac_dmn_state->arena, MAC_DMN_Entity, 1);
  }
  U32 gen = result->gen;
  MemoryZeroStruct(result);
  result->gen = gen + 1;
  result->kind = kind;
  return result;
}

internal void
mac_dmn_entity_release(MAC_DMN_Entity *entity)
{
  if(entity != 0)
  {
    entity->kind = MAC_DMN_EntityKind_Null;
    SLLStackPush(mac_dmn_state->free_entity, entity);
  }
}

internal void
mac_dmn_process_entity_release(MAC_DMN_Entity *entity)
{
  if(entity != 0 && entity->kind == MAC_DMN_EntityKind_Process)
  {
    for(MAC_DMN_Entity **ptr = &mac_dmn_state->first_process_entity; *ptr != 0; ptr = &(*ptr)->next)
    {
      if(*ptr == entity)
      {
        *ptr = entity->next;
        if(mac_dmn_state->last_process_entity == entity)
        {
          mac_dmn_state->last_process_entity = 0;
          for(MAC_DMN_Entity *n = mac_dmn_state->first_process_entity; n != 0; n = n->next)
          {
            mac_dmn_state->last_process_entity = n;
          }
        }
        break;
      }
    }

    MAC_DMN_Process *process = &entity->process;
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity, *next = 0; thread_entity != 0; thread_entity = next)
    {
      next = thread_entity->next;
      MAC_DMN_Thread *thread = &thread_entity->thread;
      if(thread->thread != MACH_PORT_NULL)
      {
        mach_port_deallocate(mach_task_self(), thread->thread);
      }
      mac_dmn_entity_release(thread_entity);
    }
    if(process->task != MACH_PORT_NULL)
    {
      mach_port_deallocate(mach_task_self(), process->task);
    }
    mac_dmn_entity_release(entity);
  }
}

internal MAC_DMN_Process *
mac_dmn_process_from_handle(DMN_Handle handle)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(handle);
  MAC_DMN_Process *result = 0;
  if(entity != 0 && entity->kind == MAC_DMN_EntityKind_Process)
  {
    result = &entity->process;
  }
  return result;
}

internal MAC_DMN_Thread *
mac_dmn_thread_from_handle(DMN_Handle handle)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(handle);
  MAC_DMN_Thread *result = 0;
  if(entity != 0 && entity->kind == MAC_DMN_EntityKind_Thread)
  {
    result = &entity->thread;
  }
  return result;
}

internal Arch
mac_dmn_host_arch(void)
{
  Arch result = Arch_Null;
#if ARCH_X64
  result = Arch_x64;
#elif ARCH_ARM64
  result = Arch_arm64;
#endif
  return result;
}

internal U64
mac_dmn_thread_id_from_port(mach_port_t thread)
{
  U64 result = 0;
  thread_identifier_info_data_t info = {0};
  mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
  if(thread_info(thread, THREAD_IDENTIFIER_INFO, (thread_info_t)&info, &count) == KERN_SUCCESS)
  {
    result = info.thread_id;
  }
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_thread_entity_alloc(MAC_DMN_Process *process, mach_port_t thread, Arch arch)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_alloc(MAC_DMN_EntityKind_Thread);
  entity->thread.process = process;
  entity->thread.thread = thread;
  entity->thread.thread_id = mac_dmn_thread_id_from_port(thread);
  entity->thread.arch = arch;
  SLLQueuePush(process->first_thread_entity, process->last_thread_entity, entity);
  return entity;
}

internal void
mac_dmn_refresh_threads(MAC_DMN_Process *process)
{
  if(process != 0 && process->task != MACH_PORT_NULL && process->first_thread_entity == 0)
  {
    thread_act_array_t threads = 0;
    mach_msg_type_number_t thread_count = 0;
    if(task_threads(process->task, &threads, &thread_count) == KERN_SUCCESS)
    {
      for(mach_msg_type_number_t idx = 0; idx < thread_count; idx += 1)
      {
        mac_dmn_thread_entity_alloc(process, threads[idx], process->arch);
      }
      vm_deallocate(mach_task_self(), (vm_address_t)threads, thread_count*sizeof(thread_t));
    }
  }
}

internal void
mac_dmn_push_event_create_process(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity)
{
  MAC_DMN_Process *process = &process_entity->process;
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_CreateProcess;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->arch = process->arch;
  e->code = (U32)process->pid;
}

internal void
mac_dmn_push_event_create_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity)
{
  MAC_DMN_Thread *thread = &thread_entity->thread;
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_CreateThread;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->thread = mac_dmn_handle_from_entity(thread_entity);
  e->arch = thread->arch;
  e->code = (U32)thread->thread_id;
}

internal void
mac_dmn_push_event_handshake_complete(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity)
{
  MAC_DMN_Process *process = &process_entity->process;
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_HandshakeComplete;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->arch = process->arch;
  if(process->first_thread_entity != 0)
  {
    e->thread = mac_dmn_handle_from_entity(process->first_thread_entity);
  }
}

internal vm_prot_t
mac_dmn_vm_prot_from_access_flags(AccessFlags flags)
{
  vm_prot_t result = VM_PROT_NONE;
  if(flags & AccessFlag_Read)    { result |= VM_PROT_READ; }
  if(flags & AccessFlag_Write)   { result |= VM_PROT_WRITE; }
  if(flags & AccessFlag_Execute) { result |= VM_PROT_EXECUTE; }
  return result;
}

#if ARCH_X64
internal void
mac_dmn_x64_reg_block_from_thread_state(X64_RegBlock *dst, x86_thread_state64_t *src)
{
  dst->rax = src->__rax;
  dst->rbx = src->__rbx;
  dst->rcx = src->__rcx;
  dst->rdx = src->__rdx;
  dst->rdi = src->__rdi;
  dst->rsi = src->__rsi;
  dst->rbp = src->__rbp;
  dst->rsp = src->__rsp;
  dst->r8 = src->__r8;
  dst->r9 = src->__r9;
  dst->r10 = src->__r10;
  dst->r11 = src->__r11;
  dst->r12 = src->__r12;
  dst->r13 = src->__r13;
  dst->r14 = src->__r14;
  dst->r15 = src->__r15;
  dst->rip = src->__rip;
  dst->rflags = src->__rflags;
  dst->cs = (U16)src->__cs;
  dst->fs = (U16)src->__fs;
  dst->gs = (U16)src->__gs;
}

internal void
mac_dmn_x64_thread_state_from_reg_block(x86_thread_state64_t *dst, X64_RegBlock *src)
{
  dst->__rax = src->rax;
  dst->__rbx = src->rbx;
  dst->__rcx = src->rcx;
  dst->__rdx = src->rdx;
  dst->__rdi = src->rdi;
  dst->__rsi = src->rsi;
  dst->__rbp = src->rbp;
  dst->__rsp = src->rsp;
  dst->__r8 = src->r8;
  dst->__r9 = src->r9;
  dst->__r10 = src->r10;
  dst->__r11 = src->r11;
  dst->__r12 = src->r12;
  dst->__r13 = src->r13;
  dst->__r14 = src->r14;
  dst->__r15 = src->r15;
  dst->__rip = src->rip;
  dst->__rflags = src->rflags;
  dst->__cs = src->cs;
  dst->__fs = src->fs;
  dst->__gs = src->gs;
}
#endif

internal void
dmn_init(void)
{
  Arena *arena = arena_alloc();
  mac_dmn_state = push_array(arena, MAC_DMN_State, 1);
  mac_dmn_state->arena = arena;
}

internal DMN_CtrlCtx *
dmn_ctrl_begin(void)
{
  local_persist DMN_CtrlCtx ctx = {0};
  return &ctx;
}

internal void
dmn_ctrl_exclusive_access_begin(void)
{
}

internal void
dmn_ctrl_exclusive_access_end(void)
{
}

internal U32
dmn_ctrl_launch(DMN_CtrlCtx *ctx, ProcessLaunchParams *params)
{
  Process process = process_launch(params);
  U32 result = (U32)process.u64[0];
  return result;
}

internal B32
dmn_ctrl_attach(DMN_CtrlCtx *ctx, U32 pid)
{
  B32 result = 0;
  mach_port_t task = MACH_PORT_NULL;
  if(task_for_pid(mach_task_self(), (int)pid, &task) == KERN_SUCCESS && task != MACH_PORT_NULL)
  {
    int ptrace_result = ptrace(PT_ATTACHEXC, (pid_t)pid, 0, 0);
    if(ptrace_result == 0 || errno == EBUSY)
    {
      MAC_DMN_Entity *entity = mac_dmn_entity_alloc(MAC_DMN_EntityKind_Process);
      entity->process.pid = (pid_t)pid;
      entity->process.task = task;
      entity->process.arch = mac_dmn_host_arch();
      entity->process.is_attached = (ptrace_result == 0);
      entity->process.needs_attach_events = 1;
      SLLQueuePush(mac_dmn_state->first_process_entity, mac_dmn_state->last_process_entity, entity);
      result = 1;
    }
    else
    {
      mach_port_deallocate(mach_task_self(), task);
    }
  }
  return result;
}

internal B32
dmn_ctrl_kill(DMN_CtrlCtx *ctx, DMN_Handle handle, U32 exit_code)
{
  MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
  B32 result = 0;
  if(process != 0)
  {
    result = (kill(process->pid, SIGKILL) == 0);
  }
  return result;
}

internal B32
dmn_ctrl_detach(DMN_CtrlCtx *ctx, DMN_Handle handle)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(handle);
  B32 result = 0;
  if(entity != 0 && entity->kind == MAC_DMN_EntityKind_Process)
  {
    MAC_DMN_Process *process = &entity->process;
    if(process->is_attached)
    {
      ptrace(PT_DETACH, process->pid, 0, 0);
    }
    mac_dmn_process_entity_release(entity);
    result = 1;
  }
  return result;
}

internal DMN_EventList
dmn_ctrl_run(Arena *arena, DMN_CtrlCtx *ctx, DMN_RunCtrls *ctrls)
{
  DMN_EventList result = {0};
  for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
  {
    if(entity->kind == MAC_DMN_EntityKind_Process && entity->process.needs_attach_events)
    {
      MAC_DMN_Process *process = &entity->process;
      mac_dmn_refresh_threads(process);
      mac_dmn_push_event_create_process(arena, &result, entity);
      for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
      {
        mac_dmn_push_event_create_thread(arena, &result, entity, thread_entity);
      }
      mac_dmn_push_event_handshake_complete(arena, &result, entity);
      process->needs_attach_events = 0;
      break;
    }
  }
  return result;
}

internal void
dmn_halt(U64 code, U64 user_data)
{
}

internal B32
dmn_access_open(void)
{
  return 1;
}

internal void
dmn_access_close(void)
{
}

internal U64
dmn_process_memory_reserve(DMN_Handle handle, U64 vaddr, U64 size)
{
  MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
  U64 result = 0;
  if(process != 0 && process->task != MACH_PORT_NULL)
  {
    mach_vm_address_t address = (mach_vm_address_t)vaddr;
    int flags = (vaddr == 0) ? VM_FLAGS_ANYWHERE : VM_FLAGS_FIXED;
    if(mach_vm_allocate(process->task, &address, size, flags) == KERN_SUCCESS)
    {
      result = address;
    }
  }
  return result;
}

internal void
dmn_process_memory_commit(DMN_Handle process, U64 vaddr, U64 size)
{
}

internal void
dmn_process_memory_decommit(DMN_Handle process, U64 vaddr, U64 size)
{
}

internal void
dmn_process_memory_release(DMN_Handle handle, U64 vaddr, U64 size)
{
  MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
  if(process != 0 && process->task != MACH_PORT_NULL)
  {
    mach_vm_deallocate(process->task, (mach_vm_address_t)vaddr, size);
  }
}

internal void
dmn_process_memory_protect(DMN_Handle handle, U64 vaddr, U64 size, AccessFlags flags)
{
  MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
  if(process != 0 && process->task != MACH_PORT_NULL)
  {
    mach_vm_protect(process->task, (mach_vm_address_t)vaddr, size, 0, mac_dmn_vm_prot_from_access_flags(flags));
  }
}

internal U64
dmn_process_read(DMN_Handle handle, Rng1U64 range, void *dst)
{
  MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
  U64 result = 0;
  if(process != 0 && process->task != MACH_PORT_NULL)
  {
    mach_vm_size_t size = dim_1u64(range);
    mach_vm_size_t bytes_read = 0;
    kern_return_t code = mach_vm_read_overwrite(process->task, range.min, size, (mach_vm_address_t)dst, &bytes_read);
    if(code == KERN_SUCCESS)
    {
      result = bytes_read;
    }
  }
  return result;
}

internal B32
dmn_process_write(DMN_Handle handle, Rng1U64 range, void *src)
{
  MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
  B32 result = 0;
  if(process != 0 && process->task != MACH_PORT_NULL)
  {
    mach_msg_type_number_t size = (mach_msg_type_number_t)dim_1u64(range);
    result = (mach_vm_write(process->task, range.min, (vm_offset_t)src, size) == KERN_SUCCESS);
  }
  return result;
}

internal Arch
dmn_arch_from_thread(DMN_Handle handle)
{
  MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(handle);
  Arch result = Arch_Null;
  if(thread != 0)
  {
    result = thread->arch;
  }
  return result;
}

internal U64
dmn_stack_base_vaddr_from_thread(DMN_Handle handle)
{
  return 0;
}

internal U64
dmn_tls_root_vaddr_from_thread(DMN_Handle handle)
{
  return 0;
}

internal B32
dmn_thread_read_reg_block(DMN_Handle handle, void *reg_block)
{
  MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(handle);
  B32 result = 0;
  if(thread != 0)
  {
    switch(thread->arch)
    {
      default:{}break;
#if ARCH_X64
      case Arch_x64:
      {
        x86_thread_state64_t state = {0};
        mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
        if(thread_get_state(thread->thread, x86_THREAD_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
        {
          mac_dmn_x64_reg_block_from_thread_state((X64_RegBlock *)reg_block, &state);
          result = 1;
        }
      }break;
#endif
    }
  }
  return result;
}

internal B32
dmn_thread_write_reg_block(DMN_Handle handle, void *reg_block)
{
  MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(handle);
  B32 result = 0;
  if(thread != 0)
  {
    switch(thread->arch)
    {
      default:{}break;
#if ARCH_X64
      case Arch_x64:
      {
        x86_thread_state64_t state = {0};
        mac_dmn_x64_thread_state_from_reg_block(&state, (X64_RegBlock *)reg_block);
        result = (thread_set_state(thread->thread, x86_THREAD_STATE64, (thread_state_t)&state, x86_THREAD_STATE64_COUNT) == KERN_SUCCESS);
      }break;
#endif
    }
  }
  return result;
}

internal void
dmn_process_iter_begin(DMN_ProcessIter *iter)
{
  MemoryZeroStruct(iter);
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
  size_t bytes = 0;
  if(sysctl(mib, ArrayCount(mib), 0, &bytes, 0, 0) == 0 && bytes != 0)
  {
    struct kinfo_proc *processes = (struct kinfo_proc *)malloc(bytes);
    if(processes != 0)
    {
      if(sysctl(mib, ArrayCount(mib), processes, &bytes, 0, 0) == 0)
      {
        MAC_DMN_ProcessIterState *state = (MAC_DMN_ProcessIterState *)malloc(sizeof(*state));
        if(state != 0)
        {
          state->v = processes;
          state->count = bytes/sizeof(*processes);
          state->idx = 0;
          iter->v[0] = (U64)state;
        }
        else
        {
          free(processes);
        }
      }
      else
      {
        free(processes);
      }
    }
  }
}

internal B32
dmn_process_iter_next(Arena *arena, DMN_ProcessIter *iter, DMN_ProcessInfo *info_out)
{
  MAC_DMN_ProcessIterState *state = (MAC_DMN_ProcessIterState *)iter->v[0];
  B32 result = 0;
  if(state != 0 && state->idx < state->count)
  {
    struct kinfo_proc *info = &state->v[state->idx];
    state->idx += 1;
    info_out->pid = (U32)info->kp_proc.p_pid;
    info_out->name = push_str8_copy(arena, str8_cstring(info->kp_proc.p_comm));
    result = 1;
  }
  return result;
}

internal void
dmn_process_iter_end(DMN_ProcessIter *iter)
{
  MAC_DMN_ProcessIterState *state = (MAC_DMN_ProcessIterState *)iter->v[0];
  if(state != 0)
  {
    free(state->v);
    free(state);
  }
  MemoryZeroStruct(iter);
}
