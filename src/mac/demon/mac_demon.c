// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#include <Security/Authorization.h>

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
mac_dmn_process_monitor_thread__entry_point(void *p)
{
  ThreadNameF("mac_dmn_process_monitor_thread");
  (void)p;
  for(;;)
  {
    struct kevent event = {0};
    int count = kevent(mac_dmn_state->process_monitor_kq, 0, 0, &event, 1, 0);
    if(count < 0 && errno == EINTR)
    {
      continue;
    }
    if(count <= 0)
    {
      continue;
    }
    if((event.flags & EV_ERROR) != 0)
    {
      continue;
    }
    if(event.filter == EVFILT_PROC && (event.fflags & NOTE_EXIT) != 0)
    {
      pid_t pid = (pid_t)event.ident;
      int status = 0;
      if(mac_dmn_process_wait_for_exit(pid, &status))
      {
        mac_dmn_process_monitor_push_exit_event(pid, status);
      }
    }
  }
}

internal void
mac_dmn_process_monitor_register_pid(pid_t pid)
{
  if(mac_dmn_state->process_monitor_kq >= 0 && pid > 0)
  {
    struct kevent event = {0};
    EV_SET(&event, (uintptr_t)pid, EVFILT_PROC, EV_ADD|EV_ONESHOT, NOTE_EXIT, 0, 0);
    kevent(mac_dmn_state->process_monitor_kq, &event, 1, 0, 0, 0);
  }
}

internal void
mac_dmn_process_monitor_push_exit_event(pid_t pid, int status)
{
  MAC_DMN_ExitEvent *event = (MAC_DMN_ExitEvent *)malloc(sizeof(*event));
  if(event != 0)
  {
    MemoryZeroStruct(event);
    event->pid = pid;
    event->status = status;
    MutexScope(mac_dmn_state->process_monitor_mutex)
    {
      SLLQueuePush(mac_dmn_state->first_exit_event, mac_dmn_state->last_exit_event, event);
    }
  }
}

internal B32
mac_dmn_process_monitor_pop_exit_event(pid_t *pid_out, int *status_out)
{
  B32 result = 0;
  MAC_DMN_ExitEvent *event = 0;
  MutexScope(mac_dmn_state->process_monitor_mutex)
  {
    event = mac_dmn_state->first_exit_event;
    if(event != 0)
    {
      SLLQueuePop(mac_dmn_state->first_exit_event, mac_dmn_state->last_exit_event);
    }
  }
  if(event != 0)
  {
    if(pid_out != 0)
    {
      *pid_out = event->pid;
    }
    if(status_out != 0)
    {
      *status_out = event->status;
    }
    free(event);
    result = 1;
  }
  return result;
}

internal B32
mac_dmn_process_wait_for_exit(pid_t pid, int *status_out)
{
  B32 result = 0;
  for(;;)
  {
    int status = 0;
    pid_t wait_id = waitpid(pid, &status, 0);
    if(wait_id < 0 && errno == EINTR)
    {
      continue;
    }
    if(wait_id != pid)
    {
      break;
    }
    if(WIFSTOPPED(status))
    {
      continue;
    }
    if(status_out != 0)
    {
      *status_out = status;
    }
    result = 1;
    break;
  }
  return result;
}

internal B32
mac_dmn_ctrl_consume_exit_status(Arena *arena, DMN_EventList *events, pid_t pid, int status)
{
  B32 result = 0;
  MAC_DMN_Entity *process_entity = mac_dmn_process_entity_from_pid(pid);
  if(process_entity != 0)
  {
    MAC_DMN_Process *process = &process_entity->process;
    process->is_running = 0;
    mac_dmn_process_resume_suspended_threads(process);
    mac_dmn_refresh_module_events(arena, events, process_entity);
    mac_dmn_refresh_thread_events(arena, events, process_entity);
    if(WIFEXITED(status))
    {
      mac_dmn_push_event_exit_process(arena, events, process_entity, (U32)WEXITSTATUS(status));
      mac_dmn_process_entity_release(process_entity);
      result = 1;
    }
    else if(WIFSIGNALED(status))
    {
      mac_dmn_push_event_exit_process(arena, events, process_entity, (U32)WTERMSIG(status));
      mac_dmn_process_entity_release(process_entity);
      result = 1;
    }
  }
  return result;
}

internal B32
mac_dmn_ctrl_consume_monitor_exit_event(Arena *arena, DMN_EventList *events)
{
  B32 result = 0;
  pid_t pid = 0;
  int status = 0;
  if(mac_dmn_process_monitor_pop_exit_event(&pid, &status))
  {
    result = mac_dmn_ctrl_consume_exit_status(arena, events, pid, status);
  }
  return result;
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
    mac_dmn_process_end_mach_exceptions(process);
    mac_dmn_process_resume_suspended_threads(process);
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
    for(MAC_DMN_Entity *module_entity = process->first_module_entity, *next = 0; module_entity != 0; module_entity = next)
    {
      next = module_entity->next;
      mac_dmn_module_entity_release(module_entity);
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

internal MAC_DMN_Entity *
mac_dmn_process_entity_from_exception_port(mach_port_t exception_port)
{
  MAC_DMN_Entity *result = 0;
  for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
  {
    if(entity->kind == MAC_DMN_EntityKind_Process && entity->process.exception_port == exception_port)
    {
      result = entity;
      break;
    }
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

internal MAC_DMN_Module *
mac_dmn_module_from_handle(DMN_Handle handle)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_from_handle(handle);
  MAC_DMN_Module *result = 0;
  if(entity != 0 && entity->kind == MAC_DMN_EntityKind_Module)
  {
    result = &entity->module;
  }
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_process_entity_from_pid(pid_t pid)
{
  MAC_DMN_Entity *result = 0;
  for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
  {
    if(entity->kind == MAC_DMN_EntityKind_Process && entity->process.pid == pid)
    {
      result = entity;
      break;
    }
  }
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_thread_entity_from_thread_id(MAC_DMN_Process *process, U64 thread_id)
{
  MAC_DMN_Entity *result = 0;
  if(process != 0)
  {
    for(MAC_DMN_Entity *entity = process->first_thread_entity; entity != 0; entity = entity->next)
    {
      if(entity->kind == MAC_DMN_EntityKind_Thread && entity->thread.thread_id == thread_id)
      {
        result = entity;
        break;
      }
    }
  }
  return result;
}

internal B32
mac_dmn_thread_port_is_valid(MAC_DMN_Thread *thread)
{
  B32 result = 0;
  if(thread != 0 && thread->thread != MACH_PORT_NULL)
  {
    switch(thread->arch)
    {
      default:{}break;
#if ARCH_X64
      case Arch_x64:
      {
        x86_thread_state64_t state = {0};
        mach_msg_type_number_t count = x86_THREAD_STATE64_COUNT;
        result = (thread_get_state(thread->thread, x86_THREAD_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS);
      }break;
#endif
    }
  }
  return result;
}

internal char **
mac_dmn_argv_from_launch_params(Arena *arena, ProcessLaunchParams *params)
{
  char **argv = push_array(arena, char *, params->cmd_line.node_count + 1);
  if(params->cmd_line.first != 0)
  {
    String8 path_to_exe = path_absolute_dst_from_relative_dst_src(arena, params->cmd_line.first->string, params->path);
    path_to_exe = str8_copy(arena, path_to_exe);
    argv[0] = (char *)path_to_exe.str;
    U64 arg_idx = 1;
    for EachNode(n, String8Node, params->cmd_line.first->next)
    {
      argv[arg_idx] = (char *)str8_copy(arena, n->string).str;
      arg_idx += 1;
    }
  }
  return argv;
}

internal char **
mac_dmn_envp_from_launch_params(Arena *arena, ProcessLaunchParams *params)
{
  char **envp = 0;
  if(params->inherit_env)
  {
    envp = mac_state.default_env;
  }
  else
  {
    envp = push_array(arena, char *, params->env.node_count + 1);
    U64 env_idx = 0;
    for EachNode(n, String8Node, params->env.first)
    {
      envp[env_idx] = (char *)str8_copy(arena, n->string).str;
      env_idx += 1;
    }
  }
  return envp;
}

internal void
mac_dmn_apply_child_stdio(ProcessLaunchParams *params)
{
  if(!file_match(params->stdout_file, file_zero()))
  {
    dup2((int)params->stdout_file.u64[0], STDOUT_FILENO);
  }
  if(!file_match(params->stderr_file, file_zero()))
  {
    dup2((int)params->stderr_file.u64[0], STDERR_FILENO);
  }
  if(!file_match(params->stdin_file, file_zero()))
  {
    dup2((int)params->stdin_file.u64[0], STDIN_FILENO);
  }
}

internal void
mac_dmn_kill_launched_child(pid_t pid)
{
  if(pid > 0)
  {
    ptrace(PT_KILL, pid, 0, 0);
    kill(pid, SIGKILL);
    kill(pid, SIGCONT);
    for(;;)
    {
      pid_t wait_id = waitpid(pid, 0, 0);
      if(wait_id == pid || (wait_id < 0 && errno != EINTR))
      {
        break;
      }
    }
  }
}

internal pid_t
mac_dmn_launch_traced_process(ProcessLaunchParams *params)
{
  Temp scratch = scratch_begin(0, 0);
  char **argv = mac_dmn_argv_from_launch_params(scratch.arena, params);
  char **envp = mac_dmn_envp_from_launch_params(scratch.arena, params);
  char *work_dir_path = (char *)str8_copy(scratch.arena, params->path).str;
  pid_t pid = fork();
  if(pid == 0)
  {
    if(ptrace(PT_TRACE_ME, 0, 0, 0) != 0) { _exit(1); }
    mac_dmn_apply_child_stdio(params);
    if(chdir(work_dir_path) != 0) { _exit(1); }
    execve(argv[0], argv, envp);
    _exit(1);
  }
  else if(pid > 0)
  {
    int status = 0;
    pid_t wait_id = 0;
    do
    {
      wait_id = waitpid(pid, &status, 0);
    }
    while(wait_id < 0 && errno == EINTR);
    if(wait_id != pid || !WIFSTOPPED(status))
    {
      String8 exe = params->cmd_line.first ? params->cmd_line.first->string : str8_zero();
      if(wait_id != pid)
      {
        log_user_errorf("Could not launch `%S`: waitpid failed for traced child.", exe);
      }
      else if(WIFEXITED(status))
      {
        log_user_errorf("Could not launch `%S`: child exited before debugger attach with status %u.", exe, (U32)WEXITSTATUS(status));
      }
      else if(WIFSIGNALED(status))
      {
        log_user_errorf("Could not launch `%S`: child exited before debugger attach from signal %u.", exe, (U32)WTERMSIG(status));
      }
      else
      {
        log_user_errorf("Could not launch `%S`: child did not stop for debugger attach.", exe);
      }
      if(wait_id != pid)
      {
        kill(pid, SIGKILL);
        waitpid(pid, 0, WNOHANG);
      }
      pid = 0;
    }
  }
  else
  {
    pid = 0;
  }
  scratch_end(scratch);
  return pid;
}

internal S32
mac_dmn_taskport_authorization_status(B32 interaction_allowed)
{
  OSStatus result = errAuthorizationInternal;
  AuthorizationRef authorization = 0;
  OSStatus create_status = AuthorizationCreate(0, kAuthorizationEmptyEnvironment,
                                               kAuthorizationFlagDefaults,
                                               &authorization);
  if(create_status == errAuthorizationSuccess)
  {
    AuthorizationItem item = { "system.privilege.taskport", 0, 0, 0 };
    AuthorizationRights rights = { 1, &item };
    AuthorizationFlags flags = (kAuthorizationFlagDefaults |
                                kAuthorizationFlagExtendRights |
                                kAuthorizationFlagPreAuthorize);
    if(interaction_allowed)
    {
      flags |= kAuthorizationFlagInteractionAllowed;
    }
    result = AuthorizationCopyRights(authorization, &rights,
                                     kAuthorizationEmptyEnvironment,
                                     flags, 0);
    AuthorizationFree(authorization, kAuthorizationFlagDefaults);
  }
  else
  {
    result = create_status;
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

internal kern_return_t
mac_dmn_exception_port_info_save(MAC_DMN_ExceptionPortInfo *info, task_t task)
{
  MemoryZeroStruct(info);
  info->mask = EXC_MASK_ALL;
  info->count = ArrayCount(info->ports);
  kern_return_t result = task_get_exception_ports(task, info->mask, info->masks, &info->count,
                                                  info->ports, info->behaviors, info->flavors);
  if(result != KERN_SUCCESS)
  {
    MemoryZeroStruct(info);
  }
  return result;
}

internal kern_return_t
mac_dmn_exception_port_info_restore(MAC_DMN_ExceptionPortInfo *info, task_t task)
{
  kern_return_t result = KERN_SUCCESS;
  for(mach_msg_type_number_t idx = 0; idx < info->count; idx += 1)
  {
    result = task_set_exception_ports(task, info->masks[idx], info->ports[idx],
                                      info->behaviors[idx], info->flavors[idx]);
    if(result != KERN_SUCCESS)
    {
      break;
    }
  }
  MemoryZeroStruct(info);
  return result;
}

internal B32
mac_dmn_process_begin_mach_exceptions(MAC_DMN_Process *process)
{
  B32 result = 0;
  if(process != 0 && process->task != MACH_PORT_NULL)
  {
    mach_port_t self = mach_task_self();
    if(mac_dmn_state->exception_port_set == MACH_PORT_NULL)
    {
      mach_port_allocate(self, MACH_PORT_RIGHT_PORT_SET, &mac_dmn_state->exception_port_set);
    }
    if(mac_dmn_state->exception_port_set != MACH_PORT_NULL &&
       mach_port_allocate(self, MACH_PORT_RIGHT_RECEIVE, &process->exception_port) == KERN_SUCCESS &&
       mach_port_insert_right(self, process->exception_port, process->exception_port, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS &&
       mach_port_move_member(self, process->exception_port, mac_dmn_state->exception_port_set) == KERN_SUCCESS &&
       mac_dmn_exception_port_info_save(&process->exception_port_info, process->task) == KERN_SUCCESS &&
       process->exception_port_info.mask != 0 &&
       task_set_exception_ports(process->task, process->exception_port_info.mask, process->exception_port,
                                EXCEPTION_DEFAULT|MACH_EXCEPTION_CODES, THREAD_STATE_NONE) == KERN_SUCCESS)
    {
      process->uses_mach_exceptions = 1;
      result = 1;
    }
    else
    {
      mac_dmn_process_end_mach_exceptions(process);
    }
  }
  return result;
}

internal void
mac_dmn_process_end_mach_exceptions(MAC_DMN_Process *process)
{
  if(process != 0)
  {
    if(process->pending_exception.is_valid)
    {
      mac_dmn_process_reply_pending_exception(process, 0);
    }
    if(process->uses_mach_exceptions)
    {
      mac_dmn_exception_port_info_restore(&process->exception_port_info, process->task);
    }
    if(process->exception_port != MACH_PORT_NULL)
    {
      mach_port_t self = mach_task_self();
      mach_port_move_member(self, process->exception_port, MACH_PORT_NULL);
      mach_port_deallocate(self, process->exception_port);
      process->exception_port = MACH_PORT_NULL;
    }
    process->uses_mach_exceptions = 0;
  }
}

internal B32
mac_dmn_process_reacquire_task_after_exec(MAC_DMN_Process *process)
{
  B32 result = 0;
  if(process != 0)
  {
    mac_dmn_process_end_mach_exceptions(process);
    if(process->task != MACH_PORT_NULL)
    {
      mach_port_deallocate(mach_task_self(), process->task);
      process->task = MACH_PORT_NULL;
    }
    mach_port_t task = MACH_PORT_NULL;
    kern_return_t task_result = task_for_pid(mach_task_self(), (int)process->pid, &task);
    if(task_result == KERN_SUCCESS && task != MACH_PORT_NULL)
    {
      process->task = task;
      result = mac_dmn_process_begin_mach_exceptions(process);
    }
  }
  return result;
}

internal S32
mac_dmn_soft_signal_from_exception_message(MAC_DMN_ExceptionMessage *message)
{
  S32 result = 0;
  if(message != 0 &&
     message->exception == EXC_SOFTWARE &&
     message->code_count == 2 &&
     message->code[0] == EXC_SOFT_SIGNAL)
  {
    result = (S32)message->code[1];
  }
  return result;
}

internal B32
mac_dmn_process_reply_pending_exception(MAC_DMN_Process *process, S32 signal)
{
  B32 result = 1;
  if(process != 0 && process->pending_exception.is_valid)
  {
    MAC_DMN_ExceptionMessage *message = &process->pending_exception;
    S32 soft_signal = mac_dmn_soft_signal_from_exception_message(message);
    if(soft_signal != 0)
    {
      ptrace(PT_THUPDATE, process->pid, (caddr_t)((uintptr_t)message->thread), signal);
    }
    kern_return_t send_result = mach_msg(&message->reply.Head,
                                         MACH_SEND_MSG|MACH_SEND_INTERRUPT,
                                         message->reply.Head.msgh_size,
                                         0,
                                         MACH_PORT_NULL,
                                         MACH_MSG_TIMEOUT_NONE,
                                         MACH_PORT_NULL);
    result = (send_result == KERN_SUCCESS);
    MemoryZeroStruct(message);
  }
  return result;
}

internal B32
mac_dmn_process_stop_for_detach(MAC_DMN_Process *process)
{
  B32 result = 1;
  if(process != 0)
  {
    S32 pending_signal = mac_dmn_soft_signal_from_exception_message(&process->pending_exception);
    if(process->pending_exception.is_valid && pending_signal != SIGSTOP)
    {
      mac_dmn_process_reply_pending_exception(process, 0);
      ptrace(PT_CONTINUE, process->pid, (caddr_t)1, 0);
      process->is_running = 1;
    }
    if(process->is_running)
    {
      result = 0;
      kill(process->pid, SIGSTOP);
      if(process->uses_mach_exceptions)
      {
        for(U64 attempt = 0; attempt < 500; attempt += 1)
        {
          MAC_DMN_ExceptionMessage exception_message = {0};
          if(mac_dmn_exception_message_receive(&exception_message, 10))
          {
            MAC_DMN_Entity *entity = mac_dmn_process_entity_from_exception_port(exception_message.request.hdr.msgh_local_port);
            if(entity != 0 && &entity->process == process)
            {
              process->pending_exception = exception_message;
              S32 signo = mac_dmn_soft_signal_from_exception_message(&process->pending_exception);
              result = (signo == SIGSTOP || signo == 0);
              break;
            }
            else if(entity != 0 && !entity->process.pending_exception.is_valid)
            {
              entity->process.pending_exception = exception_message;
            }
          }
        }
      }
      else
      {
        int status = 0;
        pid_t wait_id = 0;
        do
        {
          wait_id = waitpid(process->pid, &status, 0);
        }
        while(wait_id < 0 && errno == EINTR);
        result = (wait_id == process->pid && WIFSTOPPED(status));
      }
      if(result)
      {
        process->is_running = 0;
      }
    }
  }
  return result;
}

internal void
mac_dmn_process_request_halt(MAC_DMN_Process *process)
{
  if(process != 0 && process->is_running && !process->halt_expected)
  {
    if(kill(process->pid, SIGSTOP) == 0)
    {
      process->halt_expected = 1;
    }
  }
}

#pragma pack(push, 4)
typedef struct MAC_DMN_RequestMachExceptionRaise
{
  mach_msg_header_t Head;
  mach_msg_body_t msgh_body;
  mach_msg_port_descriptor_t thread;
  mach_msg_port_descriptor_t task;
  NDR_record_t NDR;
  exception_type_t exception;
  mach_msg_type_number_t codeCnt;
  mach_exception_data_type_t code[2];
}
MAC_DMN_RequestMachExceptionRaise;
#pragma pack(pop)

internal B32
mac_dmn_exception_message_receive(MAC_DMN_ExceptionMessage *message, mach_msg_timeout_t timeout)
{
  B32 result = 0;
  if(message != 0 && mac_dmn_state->exception_port_set != MACH_PORT_NULL)
  {
    MemoryZeroStruct(message);
    mach_msg_option_t options = MACH_RCV_MSG|MACH_RCV_INTERRUPT;
    if(timeout != MACH_MSG_TIMEOUT_NONE)
    {
      options |= MACH_RCV_TIMEOUT;
    }
    kern_return_t receive_result = mach_msg(&message->request.hdr, options, 0, sizeof(message->request.data),
                                            mac_dmn_state->exception_port_set, timeout, MACH_PORT_NULL);
    if(receive_result == KERN_SUCCESS)
    {
      mach_msg_header_t *head = &message->request.hdr;
      if(head->msgh_id == 2405 && head->msgh_size >= sizeof(MAC_DMN_RequestMachExceptionRaise))
      {
        MAC_DMN_RequestMachExceptionRaise *request = (MAC_DMN_RequestMachExceptionRaise *)head;
        message->thread = request->thread.name;
        message->task = request->task.name;
        message->exception = request->exception;
        message->code_count = Min(request->codeCnt, ArrayCount(message->code));
        for(mach_msg_type_number_t idx = 0; idx < message->code_count; idx += 1)
        {
          message->code[idx] = request->code[idx];
        }
        message->reply.Head.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(head->msgh_bits), 0);
        message->reply.Head.msgh_size = sizeof(message->reply);
        message->reply.Head.msgh_remote_port = head->msgh_remote_port;
        message->reply.Head.msgh_local_port = MACH_PORT_NULL;
        message->reply.Head.msgh_reserved = 0;
        message->reply.Head.msgh_id = head->msgh_id + 100;
        message->reply.NDR = NDR_record;
        message->reply.RetCode = KERN_SUCCESS;
        message->is_valid = 1;
        result = 1;
      }
    }
  }
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_process_entity_alloc(pid_t pid, mach_port_t task, B32 is_attached, B32 needs_attach_events)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_alloc(MAC_DMN_EntityKind_Process);
  entity->process.pid = pid;
  entity->process.task = task;
  entity->process.arch = mac_dmn_host_arch();
  entity->process.is_attached = is_attached;
  entity->process.needs_attach_events = needs_attach_events;
  SLLQueuePush(mac_dmn_state->first_process_entity, mac_dmn_state->last_process_entity, entity);
  mac_dmn_process_monitor_register_pid(pid);
  return entity;
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
mac_dmn_thread_entity_release(MAC_DMN_Entity *entity)
{
  if(entity != 0 && entity->kind == MAC_DMN_EntityKind_Thread)
  {
    if(entity->thread.is_suspended_for_run)
    {
      thread_resume(entity->thread.thread);
      entity->thread.is_suspended_for_run = 0;
    }
    MAC_DMN_Process *process = entity->thread.process;
    for(MAC_DMN_Entity **ptr = &process->first_thread_entity; *ptr != 0; ptr = &(*ptr)->next)
    {
      if(*ptr == entity)
      {
        *ptr = entity->next;
        if(process->last_thread_entity == entity)
        {
          process->last_thread_entity = 0;
          for(MAC_DMN_Entity *n = process->first_thread_entity; n != 0; n = n->next)
          {
            process->last_thread_entity = n;
          }
        }
        break;
      }
    }
    if(entity->thread.thread != MACH_PORT_NULL)
    {
      mach_port_deallocate(mach_task_self(), entity->thread.thread);
    }
    mac_dmn_entity_release(entity);
  }
}

internal void
mac_dmn_process_clear_thread_entities(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity)
{
  MAC_DMN_Process *process = process_entity != 0 && process_entity->kind == MAC_DMN_EntityKind_Process ? &process_entity->process : 0;
  if(process != 0)
  {
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity, *next = 0; thread_entity != 0; thread_entity = next)
    {
      next = thread_entity->next;
      if(arena != 0 && events != 0)
      {
        mac_dmn_push_event_exit_thread(arena, events, process_entity, thread_entity);
      }
      mac_dmn_thread_entity_release(thread_entity);
    }
  }
}

internal MAC_DMN_Entity *
mac_dmn_module_entity_alloc(MAC_DMN_Process *process, U64 base_vaddr, U64 size, String8 path, Arch arch)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_alloc(MAC_DMN_EntityKind_Module);
  entity->module.process = process;
  entity->module.base_vaddr = base_vaddr;
  entity->module.size = size;
  entity->module.path = push_str8_copy(mac_dmn_state->arena, path);
  entity->module.arch = arch;
  entity->module.is_live = 1;
  SLLQueuePush(process->first_module_entity, process->last_module_entity, entity);
  return entity;
}

internal MAC_DMN_Entity *
mac_dmn_module_entity_from_base_vaddr(MAC_DMN_Process *process, U64 base_vaddr)
{
  MAC_DMN_Entity *result = 0;
  if(process != 0)
  {
    for(MAC_DMN_Entity *entity = process->first_module_entity; entity != 0; entity = entity->next)
    {
      if(entity->kind == MAC_DMN_EntityKind_Module && entity->module.base_vaddr == base_vaddr)
      {
        result = entity;
        break;
      }
    }
  }
  return result;
}

internal void
mac_dmn_module_entity_release(MAC_DMN_Entity *entity)
{
  if(entity != 0 && entity->kind == MAC_DMN_EntityKind_Module)
  {
    MAC_DMN_Process *process = entity->module.process;
    if(process != 0)
    {
      for(MAC_DMN_Entity **ptr = &process->first_module_entity; *ptr != 0; ptr = &(*ptr)->next)
      {
        if(*ptr == entity)
        {
          *ptr = entity->next;
          if(process->last_module_entity == entity)
          {
            process->last_module_entity = 0;
            for(MAC_DMN_Entity *n = process->first_module_entity; n != 0; n = n->next)
            {
              process->last_module_entity = n;
            }
          }
          break;
        }
      }
    }
    mac_dmn_entity_release(entity);
  }
}

internal void
mac_dmn_process_clear_module_entities(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity)
{
  MAC_DMN_Process *process = process_entity != 0 && process_entity->kind == MAC_DMN_EntityKind_Process ? &process_entity->process : 0;
  if(process != 0)
  {
    for(MAC_DMN_Entity *module_entity = process->first_module_entity, *next = 0; module_entity != 0; module_entity = next)
    {
      next = module_entity->next;
      if(arena != 0 && events != 0)
      {
        mac_dmn_push_event_unload_module(arena, events, process_entity, module_entity);
      }
      mac_dmn_module_entity_release(module_entity);
    }
  }
}

internal void
mac_dmn_refresh_threads(MAC_DMN_Process *process)
{
  if(process != 0)
  {
    for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
    {
      if(entity->kind == MAC_DMN_EntityKind_Process && &entity->process == process)
      {
        mac_dmn_refresh_thread_events(0, 0, entity);
        break;
      }
    }
  }
}

internal void
mac_dmn_refresh_thread_events(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity)
{
  MAC_DMN_Process *process = process_entity != 0 && process_entity->kind == MAC_DMN_EntityKind_Process ? &process_entity->process : 0;
  if(process != 0 && process->task != MACH_PORT_NULL)
  {
    thread_act_array_t threads = 0;
    mach_msg_type_number_t thread_count = 0;
    if(task_threads(process->task, &threads, &thread_count) == KERN_SUCCESS)
    {
      Temp scratch = scratch_begin(0, 0);
      U64 *live_thread_ids = push_array(scratch.arena, U64, thread_count);
      for(mach_msg_type_number_t idx = 0; idx < thread_count; idx += 1)
      {
        U64 thread_id = mac_dmn_thread_id_from_port(threads[idx]);
        live_thread_ids[idx] = thread_id;
        MAC_DMN_Entity *thread_entity = mac_dmn_thread_entity_from_thread_id(process, thread_id);
        if(thread_entity == 0)
        {
          thread_entity = mac_dmn_thread_entity_alloc(process, threads[idx], process->arch);
          if(arena != 0 && events != 0)
          {
            mac_dmn_push_event_create_thread(arena, events, process_entity, thread_entity);
          }
        }
        else if(!mac_dmn_thread_port_is_valid(&thread_entity->thread))
        {
          if(thread_entity->thread.thread != MACH_PORT_NULL)
          {
            mach_port_deallocate(mach_task_self(), thread_entity->thread.thread);
          }
          thread_entity->thread.thread = threads[idx];
          thread_entity->thread.thread_id = thread_id;
          thread_entity->thread.arch = process->arch;
          thread_entity->thread.is_suspended_for_run = 0;
          thread_entity->thread.is_stepping_over_dyld_notification = 0;
          thread_entity->thread.dyld_notification_step_vaddr = 0;
        }
        else
        {
          mach_port_deallocate(mach_task_self(), threads[idx]);
        }
      }
      for(MAC_DMN_Entity *thread_entity = process->first_thread_entity, *next = 0; thread_entity != 0; thread_entity = next)
      {
        next = thread_entity->next;
        B32 is_live = 0;
        for(mach_msg_type_number_t idx = 0; idx < thread_count; idx += 1)
        {
          if(thread_entity->thread.thread_id == live_thread_ids[idx])
          {
            is_live = 1;
            break;
          }
        }
        if(!is_live)
        {
          if(arena != 0 && events != 0)
          {
            mac_dmn_push_event_exit_thread(arena, events, process_entity, thread_entity);
          }
          mac_dmn_thread_entity_release(thread_entity);
        }
      }
      scratch_end(scratch);
      vm_deallocate(mach_task_self(), (vm_address_t)threads, thread_count*sizeof(thread_t));
    }
  }
}

internal String8
mac_dmn_executable_path_from_pid(Arena *arena, pid_t pid)
{
  String8 result = {0};
  char path[PROC_PIDPATHINFO_MAXSIZE] = {0};
  int size = proc_pidpath(pid, path, sizeof(path));
  if(size > 0)
  {
    result = push_str8_copy(arena, str8_cstring(path));
  }
  return result;
}

internal U64
mac_dmn_main_module_base_vaddr_from_process(MAC_DMN_Process *process, MachO_UUID expected_uuid)
{
  U64 result = 0;
  if(process != 0 && process->task != MACH_PORT_NULL)
  {
    mach_vm_address_t address = 0;
    natural_t depth = 0;
    for(;;)
    {
      mach_vm_size_t size = 0;
      vm_region_submap_info_data_64_t info = {0};
      mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
      kern_return_t code = mach_vm_region_recurse(process->task, &address, &size, &depth, (vm_region_recurse_info_t)&info, &count);
      if(code != KERN_SUCCESS)
      {
        break;
      }
      if(info.is_submap)
      {
        depth += 1;
        continue;
      }

      U32 magic = 0;
      mach_vm_size_t bytes_read = 0;
      if(mach_vm_read_overwrite(process->task, address, sizeof(magic), (mach_vm_address_t)&magic, &bytes_read) == KERN_SUCCESS &&
         bytes_read == sizeof(magic) &&
         macho_magic_is_supported(magic))
      {
        U64 header_size = macho_header_size_from_magic(magic);
        MachO_Header64 header = {0};
        if(header_size != 0 &&
           mach_vm_read_overwrite(process->task, address, header_size, (mach_vm_address_t)&header, &bytes_read) == KERN_SUCCESS &&
           bytes_read == header_size)
        {
          Temp scratch = scratch_begin(0, 0);
          U64 data_size = header_size + header.load_commands_size;
          U8 *data_bytes = push_array(scratch.arena, U8, data_size);
          if(mach_vm_read_overwrite(process->task, address, data_size, (mach_vm_address_t)data_bytes, &bytes_read) == KERN_SUCCESS &&
             bytes_read == data_size)
          {
            String8 data = str8(data_bytes, data_size);
            MachO_Bin bin = macho_bin_from_data(scratch.arena, data);
            MachO_UUID uuid = macho_uuid_from_bin(data, &bin);
            if(MemoryMatch(uuid.v, expected_uuid.v, sizeof(uuid.v)))
            {
              result = address;
            }
          }
          scratch_end(scratch);
          if(result != 0)
          {
            break;
          }
        }
      }
      if(address + size <= address)
      {
        break;
      }
      address += size;
    }
  }
  return result;
}

internal void
mac_dmn_refresh_initial_module(MAC_DMN_Process *process)
{
  if(process != 0 && process->first_module_entity == 0)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 path = mac_dmn_executable_path_from_pid(scratch.arena, process->pid);
    String8 data = data_from_file_path(scratch.arena, path);
    MachO_Bin bin = macho_bin_from_data(scratch.arena, data);
    MachO_UUID uuid = macho_uuid_from_bin(data, &bin);
    U64 base_vaddr = mac_dmn_main_module_base_vaddr_from_process(process, uuid);
    U64 image_size = macho_image_size_from_bin(data, &bin);
    Arch arch = arch_from_macho_cpu_type(bin.header.cpu_type);
    if(path.size != 0 && base_vaddr != 0 && image_size != 0)
    {
      process->arch = arch;
      for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
      {
        if(thread_entity->kind == MAC_DMN_EntityKind_Thread)
        {
          thread_entity->thread.arch = arch;
        }
      }
      mac_dmn_module_entity_alloc(process, base_vaddr, image_size, path, arch);
    }
    scratch_end(scratch);
  }
}

internal String8
mac_dmn_read_string(Arena *arena, MAC_DMN_Process *process, U64 vaddr, U64 max_size)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8 result = {0};
  if(process != 0 && process->task != MACH_PORT_NULL && vaddr != 0 && max_size != 0)
  {
    U8 *buffer = push_array_no_zero(scratch.arena, U8, max_size);
    U64 size = 0;
    for(; size < max_size;)
    {
      U8 byte = 0;
      mach_vm_size_t bytes_read = 0;
      if(mach_vm_read_overwrite(process->task, (mach_vm_address_t)(vaddr + size), sizeof(byte), (mach_vm_address_t)&byte, &bytes_read) != KERN_SUCCESS ||
         bytes_read != sizeof(byte) ||
         byte == 0)
      {
        break;
      }
      buffer[size] = byte;
      size += 1;
    }
    if(size != 0)
    {
      result = push_str8_copy(arena, str8(buffer, size));
    }
  }
  scratch_end(scratch);
  return result;
}

internal B32
mac_dmn_macho_image_info_from_process(MAC_DMN_Process *process, U64 base_vaddr, U64 *size_out, Arch *arch_out)
{
  B32 result = 0;
  if(size_out != 0) { *size_out = 0; }
  if(arch_out != 0) { *arch_out = Arch_Null; }
  if(process != 0 && process->task != MACH_PORT_NULL && base_vaddr != 0)
  {
    mach_vm_size_t bytes_read = 0;
    U32 magic = 0;
    if(mach_vm_read_overwrite(process->task, (mach_vm_address_t)base_vaddr, sizeof(magic), (mach_vm_address_t)&magic, &bytes_read) == KERN_SUCCESS &&
       bytes_read == sizeof(magic) &&
       macho_magic_is_supported(magic))
    {
      U64 header_size = macho_header_size_from_magic(magic);
      MachO_Header64 header = {0};
      if(header_size != 0 &&
         mach_vm_read_overwrite(process->task, (mach_vm_address_t)base_vaddr, header_size, (mach_vm_address_t)&header, &bytes_read) == KERN_SUCCESS &&
         bytes_read == header_size &&
         header.load_commands_size < MB(16))
      {
        Temp scratch = scratch_begin(0, 0);
        U64 data_size = header_size + header.load_commands_size;
        U8 *data_bytes = push_array(scratch.arena, U8, data_size);
        if(mach_vm_read_overwrite(process->task, (mach_vm_address_t)base_vaddr, data_size, (mach_vm_address_t)data_bytes, &bytes_read) == KERN_SUCCESS &&
           bytes_read == data_size)
        {
          String8 data = str8(data_bytes, data_size);
          MachO_Bin bin = macho_bin_from_data(scratch.arena, data);
          U64 min_vaddr = max_U64;
          U64 max_vaddr = 0;
          for EachIndex(idx, bin.load_commands.count)
          {
            MachO_LoadCommandInfo *info = &bin.load_commands.v[idx];
            if(info->cmd == MACHO_LC_SEGMENT_64 && info->offset + sizeof(MachO_SegmentCommand64) <= data.size)
            {
              MachO_SegmentCommand64 segment = {0};
              str8_deserial_read_struct(data, info->offset, &segment);
              if(segment.vmsize != 0)
              {
                min_vaddr = Min(min_vaddr, segment.vmaddr);
                max_vaddr = Max(max_vaddr, segment.vmaddr + segment.vmsize);
              }
            }
          }
          if(min_vaddr < max_vaddr)
          {
            if(size_out != 0) { *size_out = max_vaddr - min_vaddr; }
            if(arch_out != 0) { *arch_out = arch_from_macho_cpu_type(bin.header.cpu_type); }
            result = 1;
          }
        }
        scratch_end(scratch);
      }
    }
  }
  return result;
}

internal B32
mac_dmn_read_dyld_all_image_infos(MAC_DMN_Process *process, struct dyld_all_image_infos *all_images_out)
{
  B32 result = 0;
  if(all_images_out != 0)
  {
    MemoryZeroStruct(all_images_out);
  }
  if(process != 0 && process->task != MACH_PORT_NULL && all_images_out != 0)
  {
    task_dyld_info_data_t dyld_info = {0};
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    if(task_info(process->task, TASK_DYLD_INFO, (task_info_t)&dyld_info, &count) == KERN_SUCCESS &&
       dyld_info.all_image_info_addr != 0 &&
       dyld_info.all_image_info_format == TASK_DYLD_ALL_IMAGE_INFO_64)
    {
      mach_vm_size_t bytes_to_read = Min((mach_vm_size_t)sizeof(*all_images_out), dyld_info.all_image_info_size);
      mach_vm_size_t bytes_read = 0;
      if(bytes_to_read >= OffsetOf(struct dyld_all_image_infos, notification) + sizeof(all_images_out->notification) &&
         mach_vm_read_overwrite(process->task, dyld_info.all_image_info_addr, bytes_to_read, (mach_vm_address_t)all_images_out, &bytes_read) == KERN_SUCCESS &&
         bytes_read == bytes_to_read)
      {
        result = 1;
      }
    }
  }
  return result;
}

internal B32
mac_dmn_read_dyld_image_infos(Arena *arena, MAC_DMN_Process *process, struct dyld_image_info **images_out, U32 *count_out)
{
  B32 result = 0;
  if(images_out != 0) { *images_out = 0; }
  if(count_out != 0) { *count_out = 0; }
  if(process != 0 && process->task != MACH_PORT_NULL && images_out != 0 && count_out != 0)
  {
    struct dyld_all_image_infos all_images = {0};
    if(mac_dmn_read_dyld_all_image_infos(process, &all_images) &&
       all_images.infoArray != 0 &&
       all_images.infoArrayCount != 0 &&
       all_images.infoArrayCount < 16384)
    {
      mach_vm_size_t bytes_read = 0;
      U64 images_size = (U64)all_images.infoArrayCount*sizeof(struct dyld_image_info);
      struct dyld_image_info *images = push_array_no_zero(arena, struct dyld_image_info, all_images.infoArrayCount);
      if(mach_vm_read_overwrite(process->task, (mach_vm_address_t)all_images.infoArray, images_size, (mach_vm_address_t)images, &bytes_read) == KERN_SUCCESS &&
         bytes_read == images_size)
      {
        *images_out = images;
        *count_out = all_images.infoArrayCount;
        result = 1;
      }
    }
  }
  return result;
}

internal U64
mac_dmn_dyld_notification_vaddr_from_process(MAC_DMN_Process *process)
{
  U64 result = 0;
  struct dyld_all_image_infos all_images = {0};
  if(mac_dmn_read_dyld_all_image_infos(process, &all_images))
  {
    result = (U64)all_images.notification;
  }
  return result;
}

internal void
mac_dmn_refresh_module_events(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity)
{
  if(process_entity != 0 && process_entity->kind == MAC_DMN_EntityKind_Process)
  {
    Temp scratch = scratch_begin(&arena, 1);
    MAC_DMN_Process *process = &process_entity->process;
    String8 executable_path = mac_dmn_executable_path_from_pid(scratch.arena, process->pid);
    MAC_DMN_Entity *first_module = process->first_module_entity;
    if(first_module != 0 &&
       executable_path.size != 0 &&
       first_module->kind == MAC_DMN_EntityKind_Module &&
       !path_match_normalized(first_module->module.path, executable_path))
    {
      mac_dmn_process_reacquire_task_after_exec(process);
      mac_dmn_process_clear_thread_entities(arena, events, process_entity);
      mac_dmn_process_clear_module_entities(arena, events, process_entity);
      mac_dmn_refresh_initial_module(process);
      for(MAC_DMN_Entity *module_entity = process->first_module_entity; module_entity != 0; module_entity = module_entity->next)
      {
        if(events != 0)
        {
          mac_dmn_push_event_load_module(arena, events, process_entity, module_entity);
        }
      }
    }
    for(MAC_DMN_Entity *module_entity = process->first_module_entity; module_entity != 0; module_entity = module_entity->next)
    {
      module_entity->module.is_live = 0;
    }

    struct dyld_image_info *images = 0;
    U32 image_count = 0;
    if(mac_dmn_read_dyld_image_infos(scratch.arena, process, &images, &image_count))
    {
      for(U32 idx = 0; idx < image_count; idx += 1)
      {
        U64 base_vaddr = (U64)images[idx].imageLoadAddress;
        if(base_vaddr == 0)
        {
          continue;
        }

        U64 image_size = 0;
        Arch arch = Arch_Null;
        if(!mac_dmn_macho_image_info_from_process(process, base_vaddr, &image_size, &arch))
        {
          continue;
        }

        MAC_DMN_Entity *module_entity = mac_dmn_module_entity_from_base_vaddr(process, base_vaddr);
        if(module_entity != 0)
        {
          module_entity->module.is_live = 1;
        }
        else
        {
          String8 path = mac_dmn_read_string(scratch.arena, process, (U64)images[idx].imageFilePath, KB(16));
          module_entity = mac_dmn_module_entity_alloc(process, base_vaddr, image_size, path, arch);
          if(events != 0)
          {
            mac_dmn_push_event_load_module(arena, events, process_entity, module_entity);
          }
        }
      }

      for(MAC_DMN_Entity *module_entity = process->first_module_entity, *next = 0; module_entity != 0; module_entity = next)
      {
        next = module_entity->next;
        if(executable_path.size != 0 && path_match_normalized(module_entity->module.path, executable_path))
        {
          module_entity->module.is_live = 1;
        }
        if(!module_entity->module.is_live)
        {
          if(events != 0)
          {
            mac_dmn_push_event_unload_module(arena, events, process_entity, module_entity);
          }
          mac_dmn_module_entity_release(module_entity);
        }
      }
      if(process->first_module_entity == 0)
      {
        mac_dmn_refresh_initial_module(process);
        for(MAC_DMN_Entity *module_entity = process->first_module_entity; module_entity != 0; module_entity = module_entity->next)
        {
          if(events != 0)
          {
            mac_dmn_push_event_load_module(arena, events, process_entity, module_entity);
          }
        }
      }
    }
    else
    {
      mac_dmn_refresh_initial_module(process);
    }
    scratch_end(scratch);
  }
}

internal B32
mac_dmn_thread_should_run(MAC_DMN_Entity *thread_entity, DMN_RunCtrls *ctrls)
{
  B32 result = 1;
  if(ctrls != 0 && thread_entity != 0 && thread_entity->kind == MAC_DMN_EntityKind_Thread)
  {
    DMN_Handle thread_handle = mac_dmn_handle_from_entity(thread_entity);
    MAC_DMN_Process *process = thread_entity->thread.process;
    if(thread_entity->thread.is_stepping_over_dyld_notification)
    {
      result = 1;
    }
    else if(!dmn_handle_match(ctrls->single_step_thread, dmn_handle_zero()))
    {
      result = dmn_handle_match(ctrls->single_step_thread, thread_handle);
    }
    else if(ctrls->run_entity_count != 0)
    {
      B32 is_listed = 0;
      if(ctrls->run_entities_are_processes)
      {
        for(MAC_DMN_Entity *process_entity = mac_dmn_state->first_process_entity; process_entity != 0; process_entity = process_entity->next)
        {
          if(process_entity->kind == MAC_DMN_EntityKind_Process && &process_entity->process == process)
          {
            DMN_Handle process_handle = mac_dmn_handle_from_entity(process_entity);
            for EachIndex(idx, ctrls->run_entity_count)
            {
              if(dmn_handle_match(ctrls->run_entities[idx], process_handle))
              {
                is_listed = 1;
                break;
              }
            }
            break;
          }
        }
      }
      else
      {
        for EachIndex(idx, ctrls->run_entity_count)
        {
          if(dmn_handle_match(ctrls->run_entities[idx], thread_handle))
          {
            is_listed = 1;
            break;
          }
        }
      }
      result = ctrls->run_entities_are_unfrozen ? is_listed : !is_listed;
    }
  }
  return result;
}

internal B32
mac_dmn_process_should_run(MAC_DMN_Entity *process_entity, DMN_RunCtrls *ctrls)
{
  B32 result = 1;
  if(ctrls != 0 && process_entity != 0 && process_entity->kind == MAC_DMN_EntityKind_Process)
  {
    result = 0;
    MAC_DMN_Process *process = &process_entity->process;
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
    {
      if(mac_dmn_thread_should_run(thread_entity, ctrls))
      {
        result = 1;
        break;
      }
    }
  }
  return result;
}

internal void
mac_dmn_process_suspend_frozen_threads(MAC_DMN_Process *process, DMN_RunCtrls *ctrls)
{
  if(process != 0)
  {
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
    {
      B32 should_run = mac_dmn_thread_should_run(thread_entity, ctrls);
      if(thread_entity->thread.is_suspended_for_run)
      {
        if(should_run && thread_resume(thread_entity->thread.thread) == KERN_SUCCESS)
        {
          thread_entity->thread.is_suspended_for_run = 0;
        }
      }
      else if(!should_run && thread_suspend(thread_entity->thread.thread) == KERN_SUCCESS)
      {
        thread_entity->thread.is_suspended_for_run = 1;
      }
    }
  }
}

internal void
mac_dmn_process_resume_suspended_threads(MAC_DMN_Process *process)
{
  if(process != 0)
  {
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
    {
      if(thread_entity->thread.is_suspended_for_run)
      {
        thread_resume(thread_entity->thread.thread);
        thread_entity->thread.is_suspended_for_run = 0;
      }
    }
  }
}

internal B32
mac_dmn_process_is_stepping_over_dyld_notification(MAC_DMN_Process *process, U64 vaddr)
{
  B32 result = 0;
  if(process != 0)
  {
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
    {
      if(thread_entity->kind == MAC_DMN_EntityKind_Thread &&
         thread_entity->thread.is_stepping_over_dyld_notification &&
         thread_entity->thread.dyld_notification_step_vaddr == vaddr)
      {
        result = 1;
        break;
      }
    }
  }
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_thread_entity_stepping_over_dyld_notification(MAC_DMN_Process *process)
{
  MAC_DMN_Entity *result = 0;
  if(process != 0)
  {
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
    {
      if(thread_entity->kind == MAC_DMN_EntityKind_Thread &&
         thread_entity->thread.is_stepping_over_dyld_notification)
      {
        result = thread_entity;
        break;
      }
    }
  }
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_thread_entity_stepping_over_debug_trap(MAC_DMN_Process *process)
{
  MAC_DMN_Entity *result = 0;
  if(process != 0)
  {
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
    {
      if(thread_entity->kind == MAC_DMN_EntityKind_Thread &&
         thread_entity->thread.is_stepping_over_debug_trap)
      {
        result = thread_entity;
        break;
      }
    }
  }
  return result;
}

internal void
mac_dmn_process_set_dyld_notification_single_step_flags(MAC_DMN_Process *process, B32 is_on)
{
  if(process != 0)
  {
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
    {
      if(thread_entity->kind == MAC_DMN_EntityKind_Thread &&
         thread_entity->thread.is_stepping_over_dyld_notification)
      {
        mac_dmn_set_single_step_flag(&thread_entity->thread, is_on);
      }
    }
  }
}

internal MAC_DMN_ActiveTrap *
mac_dmn_set_trap(Arena *arena, DMN_Trap *trap, MAC_DMN_ActiveTrapKind kind)
{
  MAC_DMN_Process *process = mac_dmn_process_from_handle(trap->process);
  Arch arch = process != 0 ? process->arch : Arch_CURRENT;
  ARCH_Info *arch_info = arch_info_from_arch(arch);
  String8 trap_inst = arch_info->trap_instruction;
  U8 *swap_bytes = push_array(arena, U8, trap_inst.size);
  B32 good_read = (dmn_process_read(trap->process, r1u64(trap->vaddr, trap->vaddr + trap_inst.size), swap_bytes) == trap_inst.size);
  B32 good_write = 0;
  if(good_read)
  {
    U8 *readback_bytes = push_array(arena, U8, trap_inst.size);
    Rng1U64 trap_range = r1u64(trap->vaddr, trap->vaddr + trap_inst.size);
    good_write = dmn_process_write(trap->process, trap_range, trap_inst.str);
    if(good_write && arch == Arch_arm64)
    {
      good_write = mac_dmn_process_sync_instruction_cache(process, trap_range);
    }
    if(good_write)
    {
      B32 good_readback = (dmn_process_read(trap->process, trap_range, readback_bytes) == trap_inst.size);
      good_write = (good_readback && MemoryMatch(readback_bytes, trap_inst.str, trap_inst.size));
    }
  }
  MAC_DMN_ActiveTrap *result = push_array(arena, MAC_DMN_ActiveTrap, 1);
  result->kind = kind;
  result->good = good_read && good_write;
  result->trap = trap;
  result->swap_bytes = str8(swap_bytes, trap_inst.size);
  return result;
}

internal MAC_DMN_ActiveTrap *
mac_dmn_push_active_trap(Arena *arena, MAC_DMN_ActiveTrap *first, DMN_Trap *trap, MAC_DMN_ActiveTrapKind kind)
{
  MAC_DMN_ActiveTrap *result = 0;
  MAC_DMN_ActiveTrap *existing = mac_dmn_active_trap_from_process_vaddr(first, trap->process, trap->vaddr);
  if(existing != 0)
  {
    result = push_array(arena, MAC_DMN_ActiveTrap, 1);
    result->kind = kind;
    result->good = existing->good;
    result->trap = trap;
    result->swap_bytes = str8_copy(arena, existing->swap_bytes);
  }
  else
  {
    result = mac_dmn_set_trap(arena, trap, kind);
  }
  return result;
}

internal void
mac_dmn_unset_trap(MAC_DMN_ActiveTrap *active_trap)
{
  if(active_trap->good)
  {
    MAC_DMN_Process *process = mac_dmn_process_from_handle(active_trap->trap->process);
    Arch arch = process != 0 ? process->arch : Arch_CURRENT;
    Rng1U64 range = r1u64(active_trap->trap->vaddr, active_trap->trap->vaddr + active_trap->swap_bytes.size);
    B32 good_write = dmn_process_write(active_trap->trap->process, range, active_trap->swap_bytes.str);
    if(good_write && arch == Arch_arm64)
    {
      mac_dmn_process_sync_instruction_cache(process, range);
    }
  }
}

internal B32
mac_dmn_process_sync_instruction_cache(MAC_DMN_Process *process, Rng1U64 range)
{
  B32 result = 0;
  if(process != 0 && process->task != MACH_PORT_NULL)
  {
    U64 page_size = get_system_info()->page_size;
    U64 min = AlignDownPow2(range.min, page_size);
    U64 max = AlignPow2(range.max, page_size);
    vm_machine_attribute_val_t value = MATTR_VAL_CACHE_FLUSH;
    kern_return_t code = mach_vm_machine_attribute(process->task, min, max - min, MATTR_CACHE, &value);
    result = (code == KERN_SUCCESS);
  }
  return result;
}

internal U64
mac_dmn_thread_read_ip(MAC_DMN_Thread *thread)
{
  U64 result = 0;
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
          result = state.__rip;
        }
      }break;
#endif
#if ARCH_ARM64
      case Arch_arm64:
      {
        arm_thread_state64_t state = {0};
        mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
        if(thread_get_state(thread->thread, ARM_THREAD_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
        {
          result = state.__pc;
        }
      }break;
#endif
    }
  }
  return result;
}

internal U64
mac_dmn_thread_read_sp(MAC_DMN_Thread *thread)
{
  U64 result = 0;
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
          result = state.__rsp;
        }
      }break;
#endif
#if ARCH_ARM64
      case Arch_arm64:
      {
        arm_thread_state64_t state = {0};
        mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
        if(thread_get_state(thread->thread, ARM_THREAD_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
        {
          result = state.__sp;
        }
      }break;
#endif
    }
  }
  return result;
}

internal U64
mac_dmn_stack_base_vaddr_from_thread(MAC_DMN_Thread *thread)
{
  U64 result = 0;
  U64 sp = mac_dmn_thread_read_sp(thread);
  MAC_DMN_Process *process = thread != 0 ? thread->process : 0;
  if(process != 0 && process->task != MACH_PORT_NULL && sp != 0)
  {
    mach_vm_address_t address = (mach_vm_address_t)sp;
    mach_vm_size_t size = 0;
    natural_t depth = 0;
    vm_region_submap_info_data_64_t info = {0};
    mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
    if(mach_vm_region_recurse(process->task, &address, &size, &depth, (vm_region_recurse_info_t)&info, &count) == KERN_SUCCESS &&
       address <= sp && sp < address + size)
    {
      result = address + size;
    }
  }
  return result;
}

internal B32
mac_dmn_thread_write_ip(MAC_DMN_Thread *thread, U64 ip)
{
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
          state.__rip = ip;
          result = (thread_set_state(thread->thread, x86_THREAD_STATE64, (thread_state_t)&state, x86_THREAD_STATE64_COUNT) == KERN_SUCCESS);
        }
      }break;
#endif
#if ARCH_ARM64
      case Arch_arm64:
      {
        arm_thread_state64_t state = {0};
        mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
        if(thread_get_state(thread->thread, ARM_THREAD_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
        {
          state.__pc = ip;
          result = (thread_set_state(thread->thread, ARM_THREAD_STATE64, (thread_state_t)&state, ARM_THREAD_STATE64_COUNT) == KERN_SUCCESS);
        }
      }break;
#endif
    }
  }
  return result;
}

internal B32
mac_dmn_set_single_step_flag(MAC_DMN_Thread *thread, B32 is_on)
{
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
          if(is_on) { state.__rflags |= X64_RFlag_Trap; }
          else      { state.__rflags &= ~X64_RFlag_Trap; }
          result = (thread_set_state(thread->thread, x86_THREAD_STATE64, (thread_state_t)&state, x86_THREAD_STATE64_COUNT) == KERN_SUCCESS);
        }
      }break;
#endif
#if ARCH_ARM64
      case Arch_arm64:
      {
        arm_debug_state64_t state = {0};
        mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
        if(thread_get_state(thread->thread, ARM_DEBUG_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
        {
          if(is_on) { state.__mdscr_el1 |= bit1; }
          else      { state.__mdscr_el1 &= ~bit1; }
          result = (thread_set_state(thread->thread, ARM_DEBUG_STATE64, (thread_state_t)&state, ARM_DEBUG_STATE64_COUNT) == KERN_SUCCESS);
        }
      }break;
#endif
    }
  }
  return result;
}

#if ARCH_ARM64
internal B32
mac_dmn_arm64_debug_state_regs_from_trap(DMN_Trap *trap, U64 *wvr_out, U64 *wcr_out)
{
  B32 result = 0;
  if(trap != 0 && wvr_out != 0 && wcr_out != 0)
  {
    B32 read = !!(trap->flags & DMN_TrapFlag_BreakOnRead);
    B32 write = !!(trap->flags & DMN_TrapFlag_BreakOnWrite);
    if((read || write) && trap->size != 0)
    {
      U64 aligned_size = u64_up_to_pow2(Max((U64)trap->size, 8));
      U64 aligned_start = 0;
      for(;;)
      {
        aligned_start = trap->vaddr & ~(aligned_size - 1);
        if(aligned_start + aligned_size >= trap->vaddr + trap->size)
        {
          break;
        }
        aligned_size <<= 1;
      }

      U64 wvr = aligned_start;
      U64 wcr = bit1|((U64)2 << 1)|(read ? bit4 : 0)|(write ? bit5 : 0);
      if(aligned_size <= 8)
      {
        U64 offset = trap->vaddr - aligned_start;
        U64 bas = ((1ull << trap->size) - 1) << offset;
        wcr |= bas << 5;
      }
      else
      {
        U64 mask = count_bits_set64(aligned_size - 1);
        if(mask > 31)
        {
          goto end;
        }
        wcr |= (0xffull << 5);
        wcr |= mask << 24;
      }

      *wvr_out = wvr;
      *wcr_out = wcr;
      result = 1;
    }
  }
  end:;
  return result;
}

internal U64
mac_dmn_arm64_debug_trap_idx_from_state(arm_debug_state64_t *state, U64 fault_vaddr, U64 slot_count)
{
  U64 result = max_U64;
  if(state != 0)
  {
    U64 count = Min(slot_count, ArrayCount(state->__wcr));
    for(U64 idx = 0; idx < count; idx += 1)
    {
      U64 wcr = state->__wcr[idx];
      U64 wvr = state->__wvr[idx];
      if(!(wcr & bit1))
      {
        continue;
      }

      U64 mask = (wcr >> 24) & 0x1f;
      if(mask == 0)
      {
        U64 bas = (wcr >> 5) & 0xff;
        if(((wvr >> 3) == (fault_vaddr >> 3)) &&
           (bas & (1ull << (fault_vaddr & 7))))
        {
          result = idx;
          break;
        }
      }
      else
      {
        U64 compare_mask = ~((1ull << mask) - 1);
        if((wvr & compare_mask) == (fault_vaddr & compare_mask))
        {
          result = idx;
          break;
        }
      }
    }
  }
  return result;
}

internal U64
mac_dmn_arm64_supported_watchpoint_count(void)
{
  local_persist B32 inited = 0;
  local_persist U64 result = 0;
  if(!inited)
  {
    U32 n = 0;
    size_t len = sizeof(n);
    if(sysctlbyname("hw.optional.watchpoint", &n, &len, 0, 0) == 0)
    {
      result = n;
    }
    else
    {
      result = ArrayCount(((arm_debug_state64_t *)0)->__wcr);
    }
    inited = 1;
  }
  return result;
}
#endif

internal B32
mac_dmn_thread_set_debug_traps(MAC_DMN_Thread *thread, DMN_TrapChunkList *traps)
{
  B32 result = 0;
  if(thread != 0 && traps != 0)
  {
    switch(thread->arch)
    {
      default:{}break;
#if ARCH_X64
      case Arch_x64:
      {
        x86_debug_state64_t state = {0};
        mach_msg_type_number_t count = x86_DEBUG_STATE64_COUNT;
        if(thread_get_state(thread->thread, x86_DEBUG_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
        {
          U64 *dr = &state.__dr0;
          state.__dr6 = 0;
          state.__dr7 = bit9|bit10|bit11;
          U64 trap_idx = 0;
          for(DMN_TrapChunkNode *n = traps->first; n != 0 && trap_idx < 4; n = n->next)
          {
            for(U64 n_idx = 0; n_idx < n->count && trap_idx < 4; n_idx += 1, trap_idx += 1)
            {
              DMN_Trap *trap = n->v + n_idx;
              dr[trap_idx] = trap->vaddr;
              state.__dr7 |= (1ull << (trap_idx*2));
              state.__dr7 &= ~((U64)(bit17|bit18|bit19|bit20) << (trap_idx*4));
              state.__dr7 &= ~((U64)(bit15|bit16));
              switch(trap->flags)
              {
                case DMN_TrapFlag_BreakOnExecute:
                default:{}break;
                case DMN_TrapFlag_BreakOnWrite:
                case DMN_TrapFlag_BreakOnWrite|DMN_TrapFlag_BreakOnExecute:
                {
                  state.__dr7 |= ((U64)bit17) << (trap_idx*4);
                }break;
                case DMN_TrapFlag_BreakOnRead|DMN_TrapFlag_BreakOnWrite|DMN_TrapFlag_BreakOnExecute:
                case DMN_TrapFlag_BreakOnRead|DMN_TrapFlag_BreakOnWrite:
                case DMN_TrapFlag_BreakOnRead:
                {
                  state.__dr7 |= (((U64)bit17) << (trap_idx*4));
                  state.__dr7 |= (((U64)bit18) << (trap_idx*4));
                }break;
              }
              switch(trap->size)
              {
                case 1:
                default:{}break;
                case 2:
                {
                  state.__dr7 |= (((U64)bit19) << (trap_idx*4));
                }break;
                case 4:
                {
                  state.__dr7 |= (((U64)bit19) << (trap_idx*4));
                  state.__dr7 |= (((U64)bit20) << (trap_idx*4));
                }break;
                case 8:
                {
                  state.__dr7 |= (((U64)bit20) << (trap_idx*4));
                }break;
              }
            }
          }
          result = (thread_set_state(thread->thread, x86_DEBUG_STATE64, (thread_state_t)&state, x86_DEBUG_STATE64_COUNT) == KERN_SUCCESS);
        }
      }break;
#endif
#if ARCH_ARM64
      case Arch_arm64:
      {
        arm_debug_state64_t state = {0};
        mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
        if(thread_get_state(thread->thread, ARM_DEBUG_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
        {
          U64 trap_idx = 0;
          U64 trap_count = mac_dmn_arm64_supported_watchpoint_count();
          trap_count = Min(trap_count, ArrayCount(state.__wcr));
          for(DMN_TrapChunkNode *n = traps->first; n != 0 && trap_idx < trap_count; n = n->next)
          {
            for(U64 n_idx = 0; n_idx < n->count && trap_idx < trap_count; n_idx += 1)
            {
              DMN_Trap *trap = n->v + n_idx;
              U64 wvr = 0;
              U64 wcr = 0;
              if(mac_dmn_arm64_debug_state_regs_from_trap(trap, &wvr, &wcr))
              {
                state.__wvr[trap_idx] = wvr;
                state.__wcr[trap_idx] = wcr;
                trap_idx += 1;
              }
            }
          }
          result = (thread_set_state(thread->thread, ARM_DEBUG_STATE64, (thread_state_t)&state, ARM_DEBUG_STATE64_COUNT) == KERN_SUCCESS);
        }
      }break;
#endif
    }
  }
  return result;
}

internal B32
mac_dmn_thread_clear_debug_traps(MAC_DMN_Thread *thread)
{
  B32 result = 0;
  if(thread != 0)
  {
    switch(thread->arch)
    {
      default:{}break;
#if ARCH_X64
      case Arch_x64:
      {
        x86_debug_state64_t state = {0};
        mach_msg_type_number_t count = x86_DEBUG_STATE64_COUNT;
        if(thread_get_state(thread->thread, x86_DEBUG_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
        {
          state.__dr0 = 0;
          state.__dr1 = 0;
          state.__dr2 = 0;
          state.__dr3 = 0;
          state.__dr6 = 0;
          state.__dr7 = 0;
          result = (thread_set_state(thread->thread, x86_DEBUG_STATE64, (thread_state_t)&state, x86_DEBUG_STATE64_COUNT) == KERN_SUCCESS);
        }
      }break;
#endif
#if ARCH_ARM64
      case Arch_arm64:
      {
        arm_debug_state64_t state = {0};
        mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
        if(thread_get_state(thread->thread, ARM_DEBUG_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
        {
          for(U64 idx = 0; idx < ArrayCount(state.__wvr); idx += 1)
          {
            state.__wvr[idx] = 0;
            state.__wcr[idx] = 0;
          }
          result = (thread_set_state(thread->thread, ARM_DEBUG_STATE64, (thread_state_t)&state, ARM_DEBUG_STATE64_COUNT) == KERN_SUCCESS);
        }
      }break;
#endif
    }
  }
  return result;
}

internal B32
mac_dmn_process_set_debug_traps(MAC_DMN_Process *process, DMN_TrapChunkList *traps)
{
  B32 result = 0;
  if(process != 0 && traps != 0 && traps->trap_count != 0)
  {
    B32 did_set_task_state = 0;
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
    {
      if(thread_entity->kind == MAC_DMN_EntityKind_Thread)
      {
        if(mac_dmn_thread_set_debug_traps(&thread_entity->thread, traps))
        {
#if ARCH_X64
          if(!did_set_task_state && process->arch == Arch_x64)
          {
            x86_debug_state64_t state = {0};
            mach_msg_type_number_t count = x86_DEBUG_STATE64_COUNT;
            if(thread_get_state(thread_entity->thread.thread, x86_DEBUG_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS &&
               task_set_state(process->task, x86_DEBUG_STATE64, (thread_state_t)&state, x86_DEBUG_STATE64_COUNT) == KERN_SUCCESS)
            {
              did_set_task_state = 1;
            }
          }
#endif
#if ARCH_ARM64
          if(!did_set_task_state && process->arch == Arch_arm64)
          {
            arm_debug_state64_t state = {0};
            mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
            if(thread_get_state(thread_entity->thread.thread, ARM_DEBUG_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS &&
               task_set_state(process->task, ARM_DEBUG_STATE64, (thread_state_t)&state, ARM_DEBUG_STATE64_COUNT) == KERN_SUCCESS)
            {
              did_set_task_state = 1;
            }
          }
#endif
          result = 1;
        }
      }
    }
  }
  return result;
}

internal void
mac_dmn_process_clear_debug_traps(MAC_DMN_Process *process)
{
  if(process != 0)
  {
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
    {
      if(thread_entity->kind == MAC_DMN_EntityKind_Thread)
      {
        mac_dmn_thread_clear_debug_traps(&thread_entity->thread);
      }
    }
#if ARCH_X64
    if(process->arch == Arch_x64 && process->task != MACH_PORT_NULL)
    {
      x86_debug_state64_t state = {0};
      task_set_state(process->task, x86_DEBUG_STATE64, (thread_state_t)&state, x86_DEBUG_STATE64_COUNT);
    }
#endif
#if ARCH_ARM64
    if(process->arch == Arch_arm64 && process->task != MACH_PORT_NULL)
    {
      arm_debug_state64_t state = {0};
      task_set_state(process->task, ARM_DEBUG_STATE64, (thread_state_t)&state, ARM_DEBUG_STATE64_COUNT);
    }
#endif
  }
}

internal DMN_Trap *
mac_dmn_thread_hit_debug_trap(MAC_DMN_Thread *thread, MAC_DMN_FlaggedTrapTask *first_task)
{
  DMN_Trap *result = 0;
  if(thread != 0)
  {
    switch(thread->arch)
    {
      default:{}break;
#if ARCH_X64
      case Arch_x64:
      {
        x86_debug_state64_t state = {0};
        mach_msg_type_number_t count = x86_DEBUG_STATE64_COUNT;
        if(thread_get_state(thread->thread, x86_DEBUG_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS &&
           (state.__dr6 & 0xF) != 0)
        {
          U64 flagged_trap_idx = max_U64;
          if((state.__dr7 & (1ull<<0)) && (state.__dr6 & (1ull<<0))) { flagged_trap_idx = 0; }
          else if((state.__dr7 & (1ull<<2)) && (state.__dr6 & (1ull<<1))) { flagged_trap_idx = 1; }
          else if((state.__dr7 & (1ull<<4)) && (state.__dr6 & (1ull<<2))) { flagged_trap_idx = 2; }
          else if((state.__dr7 & (1ull<<6)) && (state.__dr6 & (1ull<<3))) { flagged_trap_idx = 3; }
          if(flagged_trap_idx != max_U64)
          {
            MAC_DMN_Process *process = thread->process;
            DMN_Handle process_handle = {0};
            for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
            {
              if(entity->kind == MAC_DMN_EntityKind_Process && &entity->process == process)
              {
                process_handle = mac_dmn_handle_from_entity(entity);
                break;
              }
            }
            for(MAC_DMN_FlaggedTrapTask *task = first_task; task != 0 && result == 0; task = task->next)
            {
              if(dmn_handle_match(task->process, process_handle))
              {
                U64 trap_idx = 0;
                for(DMN_TrapChunkNode *n = task->traps.first; n != 0 && result == 0; n = n->next)
                {
                  for(U64 n_idx = 0; n_idx < n->count; n_idx += 1, trap_idx += 1)
                  {
                    if(trap_idx == flagged_trap_idx)
                    {
                      result = n->v + n_idx;
                      break;
                    }
                  }
                }
              }
            }
          }
        }
      }break;
#endif
#if ARCH_ARM64
      case Arch_arm64:
      {
        arm_debug_state64_t state = {0};
        mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
        MAC_DMN_ExceptionMessage *exception = &thread->process->pending_exception;
        B32 is_watchpoint = (exception->is_valid &&
                             exception->exception == EXC_BREAKPOINT &&
                             exception->code_count >= 2 &&
                             exception->code[0] == EXC_ARM_DA_DEBUG);
        if(is_watchpoint &&
           thread_get_state(thread->thread, ARM_DEBUG_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
        {
          U64 trap_count = mac_dmn_arm64_supported_watchpoint_count();
          U64 flagged_trap_idx = max_U64;
          flagged_trap_idx = mac_dmn_arm64_debug_trap_idx_from_state(&state, exception->code[1], trap_count);
          if(flagged_trap_idx != max_U64)
          {
            MAC_DMN_Process *process = thread->process;
            DMN_Handle process_handle = {0};
            for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
            {
              if(entity->kind == MAC_DMN_EntityKind_Process && &entity->process == process)
              {
                process_handle = mac_dmn_handle_from_entity(entity);
                break;
              }
            }
            U64 trap_idx = 0;
            for(MAC_DMN_FlaggedTrapTask *task = first_task; task != 0 && result == 0; task = task->next)
            {
              if(dmn_handle_match(task->process, process_handle))
              {
                for(DMN_TrapChunkNode *n = task->traps.first; n != 0 && result == 0; n = n->next)
                {
                  for(U64 n_idx = 0; n_idx < n->count; n_idx += 1)
                  {
                    DMN_Trap *trap = n->v + n_idx;
                    U64 wvr = 0;
                    U64 wcr = 0;
                    if(mac_dmn_arm64_debug_state_regs_from_trap(trap, &wvr, &wcr))
                    {
                      if(trap_idx == flagged_trap_idx)
                      {
                        result = trap;
                        break;
                      }
                      trap_idx += 1;
                    }
                  }
                }
              }
            }
          }
        }
      }break;
#endif
    }
  }
  return result;
}

internal MAC_DMN_ActiveTrap *
mac_dmn_active_trap_from_process_vaddr(MAC_DMN_ActiveTrap *first, DMN_Handle process, U64 vaddr)
{
  MAC_DMN_ActiveTrap *result = 0;
  for(MAC_DMN_ActiveTrap *active_trap = first; active_trap != 0; active_trap = active_trap->next)
  {
    if(active_trap->good &&
       dmn_handle_match(active_trap->trap->process, process) &&
       active_trap->trap->vaddr == vaddr)
    {
      result = active_trap;
      break;
    }
  }
  return result;
}

internal MAC_DMN_Entity *
mac_dmn_thread_entity_from_active_trap(MAC_DMN_Process *process, MAC_DMN_ActiveTrap *first, MAC_DMN_ActiveTrap **active_trap_out)
{
  MAC_DMN_Entity *result = 0;
  MAC_DMN_ActiveTrap *result_trap = 0;
  if(process != 0)
  {
    for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
    {
      if(thread_entity->kind == MAC_DMN_EntityKind_Thread)
      {
        U64 ip = mac_dmn_thread_read_ip(&thread_entity->thread);
        for(MAC_DMN_ActiveTrap *active_trap = first; active_trap != 0; active_trap = active_trap->next)
        {
          MAC_DMN_Process *trap_process = mac_dmn_process_from_handle(active_trap->trap->process);
          U64 trap_size = active_trap->swap_bytes.size;
          B32 ip_matches_trap = 0;
          switch(thread_entity->thread.arch)
          {
            default:{}break;
            case Arch_x64:
            {
              ip_matches_trap = (trap_size <= ip && active_trap->trap->vaddr == ip - trap_size);
            }break;
            case Arch_arm64:
            {
              ip_matches_trap = (active_trap->trap->vaddr == ip ||
                                 (trap_size <= ip && active_trap->trap->vaddr == ip - trap_size));
            }break;
          }
          if(active_trap->good &&
             trap_process == process &&
             ip_matches_trap)
          {
            result = thread_entity;
            result_trap = active_trap;
            goto done;
          }
        }
      }
    }
  }
  done:;
  if(active_trap_out != 0)
  {
    *active_trap_out = result_trap;
  }
  return result;
}

internal B32
mac_dmn_process_write_with_protect(MAC_DMN_Process *process, Rng1U64 range, void *src)
{
  B32 result = 0;
  if(process != 0 && process->task != MACH_PORT_NULL)
  {
    U64 size = dim_1u64(range);
    result = 1;
    for(U64 write_off = 0; write_off < size;)
    {
      mach_vm_address_t write_address = range.min + write_off;
      mach_vm_address_t region_address = write_address;
      mach_vm_size_t region_size = 0;
      natural_t depth = 0;
      vm_region_submap_info_data_64_t info = {0};
      mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
      kern_return_t region_code = mach_vm_region_recurse(process->task, &region_address, &region_size, &depth, (vm_region_recurse_info_t)&info, &count);
      if(region_code != KERN_SUCCESS || region_address > write_address || region_size == 0)
      {
        result = 0;
        break;
      }

      U64 region_off = write_address - region_address;
      if(region_off >= region_size)
      {
        result = 0;
        break;
      }
      U64 region_write_size = Min(size - write_off, region_size - region_off);
      region_write_size = Min(region_write_size, max_U32);
      if(region_write_size == 0)
      {
        result = 0;
        break;
      }

      U64 page_size = get_system_info()->page_size;
      mach_vm_address_t protect_address = AlignDownPow2(write_address, page_size);
      mach_vm_address_t protect_opl = AlignPow2(write_address + region_write_size, page_size);
      protect_opl = Min(protect_opl, region_address + region_size);
      mach_vm_size_t protect_size = protect_opl - protect_address;
      vm_prot_t old_protection = info.protection;
      vm_prot_t write_protection = VM_PROT_READ|VM_PROT_WRITE;
      kern_return_t protect_code = mach_vm_protect(process->task, protect_address, protect_size, 0, write_protection);
      if(protect_code != KERN_SUCCESS)
      {
        write_protection |= VM_PROT_COPY;
        protect_code = mach_vm_protect(process->task, protect_address, protect_size, 0, write_protection);
      }
      if(protect_code != KERN_SUCCESS)
      {
        result = 0;
        break;
      }

      mach_msg_type_number_t write_size = (mach_msg_type_number_t)region_write_size;
      kern_return_t write_code = mach_vm_write(process->task, write_address, (vm_offset_t)((U8 *)src + write_off), write_size);
      mach_vm_protect(process->task, protect_address, protect_size, 0, old_protection);
      if(write_code != KERN_SUCCESS)
      {
        result = 0;
        break;
      }
      write_off += region_write_size;
    }
  }
  return result;
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
mac_dmn_push_event_exit_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity)
{
  MAC_DMN_Thread *thread = &thread_entity->thread;
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_ExitThread;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->thread = mac_dmn_handle_from_entity(thread_entity);
  e->arch = thread->arch;
  e->code = (U32)thread->thread_id;
}

internal void
mac_dmn_push_event_load_module(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *module_entity)
{
  MAC_DMN_Module *module = &module_entity->module;
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_LoadModule;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->module = mac_dmn_handle_from_entity(module_entity);
  e->arch = module->arch;
  e->address = module->base_vaddr;
  e->size = module->size;
  e->string = module->path;
  if(module->process->first_thread_entity != 0)
  {
    e->thread = mac_dmn_handle_from_entity(module->process->first_thread_entity);
  }
}

internal void
mac_dmn_push_event_unload_module(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *module_entity)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_UnloadModule;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->module = mac_dmn_handle_from_entity(module_entity);
}

internal void
mac_dmn_push_event_exit_process(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, U32 exit_code)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_ExitProcess;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->arch = process_entity->process.arch;
  e->code = exit_code;
}

internal void
mac_dmn_push_detach_events(Arena *arena, DMN_EventList *events)
{
  for(DMN_HandleNode *n = mac_dmn_state->detach_processes.first; n != 0; n = n->next)
  {
    MAC_DMN_Entity *process_entity = mac_dmn_entity_from_handle(n->v);
    if(process_entity != 0 && process_entity->kind == MAC_DMN_EntityKind_Process)
    {
      MAC_DMN_Process *process = &process_entity->process;
      mac_dmn_process_resume_suspended_threads(process);
      mac_dmn_process_clear_thread_entities(arena, events, process_entity);
      mac_dmn_process_clear_module_entities(arena, events, process_entity);
      mac_dmn_push_event_exit_process(arena, events, process_entity, 0);
      mac_dmn_process_entity_release(process_entity);
    }
  }
  MemoryZeroStruct(&mac_dmn_state->detach_processes);
  arena_clear(mac_dmn_state->detach_arena);
}

internal void
mac_dmn_push_event_breakpoint(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity, U64 instruction_pointer, U64 user_data)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_Breakpoint;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->thread = mac_dmn_handle_from_entity(thread_entity);
  e->arch = thread_entity->thread.arch;
  e->instruction_pointer = instruction_pointer;
  e->stack_pointer = mac_dmn_thread_read_sp(&thread_entity->thread);
  e->user_data = user_data;
}

internal void
mac_dmn_push_event_data_breakpoint(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity, DMN_Trap *trap)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_Breakpoint;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->thread = mac_dmn_handle_from_entity(thread_entity);
  e->arch = thread_entity->thread.arch;
  e->instruction_pointer = mac_dmn_thread_read_ip(&thread_entity->thread);
  e->stack_pointer = mac_dmn_thread_read_sp(&thread_entity->thread);
  e->address = trap->vaddr;
  e->size = trap->size;
  e->flags = trap->flags;
  e->user_data = trap->id;
}

internal void
mac_dmn_push_event_single_step(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_SingleStep;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->thread = mac_dmn_handle_from_entity(thread_entity);
  e->arch = thread_entity->thread.arch;
  e->instruction_pointer = mac_dmn_thread_read_ip(&thread_entity->thread);
  e->stack_pointer = mac_dmn_thread_read_sp(&thread_entity->thread);
}

internal void
mac_dmn_push_event_exception(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity, S32 signo)
{
  MAC_DMN_Process *process = &process_entity->process;
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_Exception;
  e->process = mac_dmn_handle_from_entity(process_entity);
  e->arch = process->arch;
  e->signo = signo;
  e->exception_repeated = 1;
  if(thread_entity != 0)
  {
    e->thread = mac_dmn_handle_from_entity(thread_entity);
    e->instruction_pointer = mac_dmn_thread_read_ip(&thread_entity->thread);
    e->stack_pointer = mac_dmn_thread_read_sp(&thread_entity->thread);
  }
}

internal void
mac_dmn_push_event_halt(Arena *arena, DMN_EventList *events)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_Halt;
  e->code = (U32)mac_dmn_state->halt_code;
  e->user_data = mac_dmn_state->halt_user_data;
}

internal void
mac_dmn_push_event_not_attached(Arena *arena, DMN_EventList *events)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_Error;
  e->error_kind = DMN_ErrorKind_NotAttached;
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

#if ARCH_ARM64
internal void
mac_dmn_arm64_reg_block_from_thread_state(ARM64_RegBlock *dst, arm_thread_state64_t *src)
{
  dst->x0 = src->__x[0];
  dst->x1 = src->__x[1];
  dst->x2 = src->__x[2];
  dst->x3 = src->__x[3];
  dst->x4 = src->__x[4];
  dst->x5 = src->__x[5];
  dst->x6 = src->__x[6];
  dst->x7 = src->__x[7];
  dst->x8 = src->__x[8];
  dst->x9 = src->__x[9];
  dst->x10 = src->__x[10];
  dst->x11 = src->__x[11];
  dst->x12 = src->__x[12];
  dst->x13 = src->__x[13];
  dst->x14 = src->__x[14];
  dst->x15 = src->__x[15];
  dst->x16 = src->__x[16];
  dst->x17 = src->__x[17];
  dst->x18 = src->__x[18];
  dst->x19 = src->__x[19];
  dst->x20 = src->__x[20];
  dst->x21 = src->__x[21];
  dst->x22 = src->__x[22];
  dst->x23 = src->__x[23];
  dst->x24 = src->__x[24];
  dst->x25 = src->__x[25];
  dst->x26 = src->__x[26];
  dst->x27 = src->__x[27];
  dst->x28 = src->__x[28];
  dst->fp = src->__fp;
  dst->lr = src->__lr;
  dst->sp = src->__sp;
  dst->pc = src->__pc;
  dst->cpsr = src->__cpsr;
}

internal void
mac_dmn_arm64_thread_state_from_reg_block(arm_thread_state64_t *dst, ARM64_RegBlock *src)
{
  dst->__x[0] = src->x0;
  dst->__x[1] = src->x1;
  dst->__x[2] = src->x2;
  dst->__x[3] = src->x3;
  dst->__x[4] = src->x4;
  dst->__x[5] = src->x5;
  dst->__x[6] = src->x6;
  dst->__x[7] = src->x7;
  dst->__x[8] = src->x8;
  dst->__x[9] = src->x9;
  dst->__x[10] = src->x10;
  dst->__x[11] = src->x11;
  dst->__x[12] = src->x12;
  dst->__x[13] = src->x13;
  dst->__x[14] = src->x14;
  dst->__x[15] = src->x15;
  dst->__x[16] = src->x16;
  dst->__x[17] = src->x17;
  dst->__x[18] = src->x18;
  dst->__x[19] = src->x19;
  dst->__x[20] = src->x20;
  dst->__x[21] = src->x21;
  dst->__x[22] = src->x22;
  dst->__x[23] = src->x23;
  dst->__x[24] = src->x24;
  dst->__x[25] = src->x25;
  dst->__x[26] = src->x26;
  dst->__x[27] = src->x27;
  dst->__x[28] = src->x28;
  dst->__fp = src->fp;
  dst->__lr = src->lr;
  dst->__sp = src->sp;
  dst->__pc = src->pc;
  dst->__cpsr = src->cpsr;
}

internal void
mac_dmn_arm64_reg_block_from_neon_state(ARM64_RegBlock *dst, arm_neon_state64_t *src)
{
  MemoryCopy(&dst->q0,  &src->__v[0],  sizeof(dst->q0));
  MemoryCopy(&dst->q1,  &src->__v[1],  sizeof(dst->q1));
  MemoryCopy(&dst->q2,  &src->__v[2],  sizeof(dst->q2));
  MemoryCopy(&dst->q3,  &src->__v[3],  sizeof(dst->q3));
  MemoryCopy(&dst->q4,  &src->__v[4],  sizeof(dst->q4));
  MemoryCopy(&dst->q5,  &src->__v[5],  sizeof(dst->q5));
  MemoryCopy(&dst->q6,  &src->__v[6],  sizeof(dst->q6));
  MemoryCopy(&dst->q7,  &src->__v[7],  sizeof(dst->q7));
  MemoryCopy(&dst->q8,  &src->__v[8],  sizeof(dst->q8));
  MemoryCopy(&dst->q9,  &src->__v[9],  sizeof(dst->q9));
  MemoryCopy(&dst->q10, &src->__v[10], sizeof(dst->q10));
  MemoryCopy(&dst->q11, &src->__v[11], sizeof(dst->q11));
  MemoryCopy(&dst->q12, &src->__v[12], sizeof(dst->q12));
  MemoryCopy(&dst->q13, &src->__v[13], sizeof(dst->q13));
  MemoryCopy(&dst->q14, &src->__v[14], sizeof(dst->q14));
  MemoryCopy(&dst->q15, &src->__v[15], sizeof(dst->q15));
  MemoryCopy(&dst->q16, &src->__v[16], sizeof(dst->q16));
  MemoryCopy(&dst->q17, &src->__v[17], sizeof(dst->q17));
  MemoryCopy(&dst->q18, &src->__v[18], sizeof(dst->q18));
  MemoryCopy(&dst->q19, &src->__v[19], sizeof(dst->q19));
  MemoryCopy(&dst->q20, &src->__v[20], sizeof(dst->q20));
  MemoryCopy(&dst->q21, &src->__v[21], sizeof(dst->q21));
  MemoryCopy(&dst->q22, &src->__v[22], sizeof(dst->q22));
  MemoryCopy(&dst->q23, &src->__v[23], sizeof(dst->q23));
  MemoryCopy(&dst->q24, &src->__v[24], sizeof(dst->q24));
  MemoryCopy(&dst->q25, &src->__v[25], sizeof(dst->q25));
  MemoryCopy(&dst->q26, &src->__v[26], sizeof(dst->q26));
  MemoryCopy(&dst->q27, &src->__v[27], sizeof(dst->q27));
  MemoryCopy(&dst->q28, &src->__v[28], sizeof(dst->q28));
  MemoryCopy(&dst->q29, &src->__v[29], sizeof(dst->q29));
  MemoryCopy(&dst->q30, &src->__v[30], sizeof(dst->q30));
  MemoryCopy(&dst->q31, &src->__v[31], sizeof(dst->q31));
}

internal void
mac_dmn_arm64_neon_state_from_reg_block(arm_neon_state64_t *dst, ARM64_RegBlock *src)
{
  MemoryCopy(&dst->__v[0],  &src->q0,  sizeof(src->q0));
  MemoryCopy(&dst->__v[1],  &src->q1,  sizeof(src->q1));
  MemoryCopy(&dst->__v[2],  &src->q2,  sizeof(src->q2));
  MemoryCopy(&dst->__v[3],  &src->q3,  sizeof(src->q3));
  MemoryCopy(&dst->__v[4],  &src->q4,  sizeof(src->q4));
  MemoryCopy(&dst->__v[5],  &src->q5,  sizeof(src->q5));
  MemoryCopy(&dst->__v[6],  &src->q6,  sizeof(src->q6));
  MemoryCopy(&dst->__v[7],  &src->q7,  sizeof(src->q7));
  MemoryCopy(&dst->__v[8],  &src->q8,  sizeof(src->q8));
  MemoryCopy(&dst->__v[9],  &src->q9,  sizeof(src->q9));
  MemoryCopy(&dst->__v[10], &src->q10, sizeof(src->q10));
  MemoryCopy(&dst->__v[11], &src->q11, sizeof(src->q11));
  MemoryCopy(&dst->__v[12], &src->q12, sizeof(src->q12));
  MemoryCopy(&dst->__v[13], &src->q13, sizeof(src->q13));
  MemoryCopy(&dst->__v[14], &src->q14, sizeof(src->q14));
  MemoryCopy(&dst->__v[15], &src->q15, sizeof(src->q15));
  MemoryCopy(&dst->__v[16], &src->q16, sizeof(src->q16));
  MemoryCopy(&dst->__v[17], &src->q17, sizeof(src->q17));
  MemoryCopy(&dst->__v[18], &src->q18, sizeof(src->q18));
  MemoryCopy(&dst->__v[19], &src->q19, sizeof(src->q19));
  MemoryCopy(&dst->__v[20], &src->q20, sizeof(src->q20));
  MemoryCopy(&dst->__v[21], &src->q21, sizeof(src->q21));
  MemoryCopy(&dst->__v[22], &src->q22, sizeof(src->q22));
  MemoryCopy(&dst->__v[23], &src->q23, sizeof(src->q23));
  MemoryCopy(&dst->__v[24], &src->q24, sizeof(src->q24));
  MemoryCopy(&dst->__v[25], &src->q25, sizeof(src->q25));
  MemoryCopy(&dst->__v[26], &src->q26, sizeof(src->q26));
  MemoryCopy(&dst->__v[27], &src->q27, sizeof(src->q27));
  MemoryCopy(&dst->__v[28], &src->q28, sizeof(src->q28));
  MemoryCopy(&dst->__v[29], &src->q29, sizeof(src->q29));
  MemoryCopy(&dst->__v[30], &src->q30, sizeof(src->q30));
  MemoryCopy(&dst->__v[31], &src->q31, sizeof(src->q31));
}
#endif

internal void
dmn_init(void)
{
  Arena *arena = arena_alloc();
  mac_dmn_state = push_array(arena, MAC_DMN_State, 1);
  mac_dmn_state->arena = arena;
  mac_dmn_state->access_mutex = mutex_alloc();
  mac_dmn_state->process_monitor_kq = -1;
  mac_dmn_state->process_monitor_mutex = mutex_alloc();
  mac_dmn_state->detach_arena = arena_alloc();
  mac_dmn_state->process_monitor_kq = kqueue();
  if(mac_dmn_state->process_monitor_kq >= 0)
  {
    mac_dmn_state->process_monitor_thread = thread_launch(mac_dmn_process_monitor_thread__entry_point, 0);
  }
}

internal DMN_CtrlCtx *
dmn_ctrl_begin(void)
{
  local_persist DMN_CtrlCtx ctx = {0};
  mac_dmn_ctrl_thread = 1;
  return &ctx;
}

internal void
dmn_ctrl_exclusive_access_begin(void)
{
  MutexScope(mac_dmn_state->access_mutex)
  {
    mac_dmn_state->access_run_state = 1;
  }
}

internal void
dmn_ctrl_exclusive_access_end(void)
{
  MutexScope(mac_dmn_state->access_mutex)
  {
    mac_dmn_state->access_run_state = 0;
  }
}

internal U32
dmn_ctrl_launch(DMN_CtrlCtx *ctx, ProcessLaunchParams *params)
{
  U32 result = 0;
  S32 auth_status = mac_dmn_taskport_authorization_status(1);
  if(auth_status != errAuthorizationSuccess)
  {
    String8 exe = params->cmd_line.first ? params->cmd_line.first->string : str8_zero();
    log_user_errorf("Could not launch `%S`: taskport authorization failed with OSStatus %d. On macOS, approve the administrator authorization prompt, or run `security authorize -u -P system.privilege.taskport` from a logged-in Terminal session before launching targets.", exe, auth_status);
    return result;
  }
  pid_t pid = mac_dmn_launch_traced_process(params);
  if(pid != 0)
  {
    mach_port_t task = MACH_PORT_NULL;
    kern_return_t task_result = task_for_pid(mach_task_self(), (int)pid, &task);
    if(task_result == KERN_SUCCESS && task != MACH_PORT_NULL)
    {
      MAC_DMN_Entity *entity = mac_dmn_process_entity_alloc(pid, task, 1, 1);
      MAC_DMN_Process *process = &entity->process;
      if(mac_dmn_process_begin_mach_exceptions(process))
      {
        result = (U32)pid;
      }
      else
      {
        String8 exe = params->cmd_line.first ? params->cmd_line.first->string : str8_zero();
        log_user_errorf("Could not launch `%S`: failed to install Mach exception port for pid %u.", exe, (U32)pid);
        mac_dmn_kill_launched_child(pid);
        mac_dmn_process_entity_release(entity);
      }
    }
    else
    {
      String8 exe = params->cmd_line.first ? params->cmd_line.first->string : str8_zero();
      log_user_errorf("Could not launch `%S`: task_for_pid failed for pid %u with kern_return_t %d. On macOS, enable DevToolsSecurity and ensure the debugger has permission to control processes.", exe, (U32)pid, task_result);
      mac_dmn_kill_launched_child(pid);
    }
  }
  return result;
}

internal B32
dmn_ctrl_attach(DMN_CtrlCtx *ctx, U32 pid)
{
  B32 result = 0;
  mach_port_t task = MACH_PORT_NULL;
  kern_return_t task_result = task_for_pid(mach_task_self(), (int)pid, &task);
  if(task_result == KERN_SUCCESS && task != MACH_PORT_NULL)
  {
    MAC_DMN_Entity *entity = mac_dmn_process_entity_alloc((pid_t)pid, task, 1, 1);
    MAC_DMN_Process *process = &entity->process;
    B32 did_ptrace_attach = 0;
    if(mac_dmn_process_begin_mach_exceptions(process))
    {
      int ptrace_result = ptrace(PT_ATTACHEXC, (pid_t)pid, 0, 0);
      if(ptrace_result == 0 || errno == EBUSY)
      {
        if(ptrace_result != 0)
        {
          result = 1;
        }
        else
        {
          did_ptrace_attach = 1;
        }
        if(did_ptrace_attach && mac_dmn_exception_message_receive(&process->pending_exception, 5000))
        {
          MAC_DMN_Entity *stop_entity = mac_dmn_process_entity_from_exception_port(process->pending_exception.request.hdr.msgh_local_port);
          result = (stop_entity == entity);
        }
        else if(did_ptrace_attach)
        {
          log_user_errorf("Could not attach to pid %u: timed out waiting for Mach attach exception.", pid);
        }
      }
      else
      {
        log_user_errorf("Could not attach to pid %u: ptrace(PT_ATTACHEXC) failed with errno %d.", pid, errno);
      }
    }
    else
    {
      log_user_errorf("Could not attach to pid %u: failed to install Mach exception port.", pid);
    }
    if(result == 0)
    {
      if(did_ptrace_attach)
      {
        ptrace(PT_DETACH, (pid_t)pid, (caddr_t)1, 0);
      }
      mac_dmn_process_entity_release(entity);
    }
  }
  else
  {
    log_user_errorf("Could not attach to pid %u: task_for_pid failed with kern_return_t %d. On macOS, enable DevToolsSecurity and ensure the debugger has permission to control processes.", pid, task_result);
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
    errno = 0;
    B32 ptrace_kill_worked = (ptrace(PT_KILL, process->pid, 0, 0) == 0);
    mac_dmn_process_reply_pending_exception(process, 0);
    errno = 0;
    B32 signal_kill_worked = (kill(process->pid, SIGKILL) == 0 || errno == ESRCH);
    result = (ptrace_kill_worked || signal_kill_worked);
    kill(process->pid, SIGCONT);
    if(result)
    {
      int status = 0;
      if(mac_dmn_process_wait_for_exit(process->pid, &status))
      {
        mac_dmn_process_monitor_push_exit_event(process->pid, status);
      }
      process->is_running = 1;
    }
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
      mac_dmn_process_stop_for_detach(process);
      mac_dmn_process_resume_suspended_threads(process);
      mac_dmn_process_reply_pending_exception(process, 0);
      if(ptrace(PT_DETACH, process->pid, (caddr_t)1, 0) == 0)
      {
        kill(process->pid, SIGCONT);
      }
      mac_dmn_process_end_mach_exceptions(process);
      process->is_attached = 0;
    }
    dmn_handle_list_push(mac_dmn_state->detach_arena, &mac_dmn_state->detach_processes, handle);
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
      mac_dmn_refresh_module_events(arena, 0, entity);
      mac_dmn_push_event_create_process(arena, &result, entity);
      for(MAC_DMN_Entity *thread_entity = process->first_thread_entity; thread_entity != 0; thread_entity = thread_entity->next)
      {
        mac_dmn_push_event_create_thread(arena, &result, entity, thread_entity);
      }
      for(MAC_DMN_Entity *module_entity = process->first_module_entity; module_entity != 0; module_entity = module_entity->next)
      {
        mac_dmn_push_event_load_module(arena, &result, entity, module_entity);
      }
      mac_dmn_push_event_handshake_complete(arena, &result, entity);
      process->needs_attach_events = 0;
      break;
    }
  }

  if(result.count == 0)
  {
    mac_dmn_ctrl_consume_monitor_exit_event(arena, &result);
  }

  if(result.count == 0 && mac_dmn_state->detach_processes.first != 0)
  {
    mac_dmn_push_detach_events(arena, &result);
  }

  if(result.count == 0 && mac_dmn_state->halt_requested)
  {
    for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
    {
      if(entity->kind == MAC_DMN_EntityKind_Process)
      {
        MAC_DMN_Process *process = &entity->process;
        if(process->is_running)
        {
          mac_dmn_process_request_halt(process);
        }
        else
        {
          mac_dmn_push_event_halt(arena, &result);
          mac_dmn_state->halt_requested = 0;
        }
        break;
      }
    }
  }

  if(result.count == 0)
  {
    Temp scratch = scratch_begin(0, 0);
    MAC_DMN_ActiveTrap *first_active_trap = 0;
    MAC_DMN_ActiveTrap *last_active_trap = 0;
    MAC_DMN_FlaggedTrapTask *first_flagged_trap_task = 0;
    MAC_DMN_FlaggedTrapTask *last_flagged_trap_task = 0;
    if(ctrls != 0)
    {
      for(DMN_TrapChunkNode *n = ctrls->traps.first; n != 0; n = n->next)
      {
        for(U64 trap_idx = 0; trap_idx < n->count; trap_idx += 1)
        {
          DMN_Trap *trap = n->v + trap_idx;
          if(trap->flags == 0)
          {
            MAC_DMN_ActiveTrap *active_trap = mac_dmn_push_active_trap(scratch.arena, first_active_trap, trap, MAC_DMN_ActiveTrapKind_User);
            SLLQueuePush(first_active_trap, last_active_trap, active_trap);
          }
          else
          {
            MAC_DMN_FlaggedTrapTask *task = 0;
            for(MAC_DMN_FlaggedTrapTask *t = first_flagged_trap_task; t != 0; t = t->next)
            {
              if(dmn_handle_match(t->process, trap->process))
              {
                task = t;
                break;
              }
            }
            if(task == 0)
            {
              task = push_array(scratch.arena, MAC_DMN_FlaggedTrapTask, 1);
              task->process = trap->process;
              SLLQueuePush(first_flagged_trap_task, last_flagged_trap_task, task);
            }
            B32 already_in_task = 0;
            for(DMN_TrapChunkNode *node = task->traps.first; node != 0; node = node->next)
            {
              for(U64 node_idx = 0; node_idx < node->count; node_idx += 1)
              {
                if(node->v[node_idx].id == trap->id)
                {
                  already_in_task = 1;
                  goto break_flagged_trap_search;
                }
              }
            }
            break_flagged_trap_search:;
            if(!already_in_task)
            {
              dmn_trap_chunk_list_push(scratch.arena, &task->traps, 8, trap);
            }
          }
        }
      }
    }

    for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
    {
      if(entity->kind == MAC_DMN_EntityKind_Process && entity->process.is_attached)
      {
        MAC_DMN_Process *process = &entity->process;
        U64 notification_vaddr = mac_dmn_dyld_notification_vaddr_from_process(process);
        DMN_Handle process_handle = mac_dmn_handle_from_entity(entity);
        if(notification_vaddr != 0 &&
           !mac_dmn_process_is_stepping_over_dyld_notification(process, notification_vaddr) &&
           mac_dmn_active_trap_from_process_vaddr(first_active_trap, process_handle, notification_vaddr) == 0)
        {
          DMN_Trap *trap = push_array(scratch.arena, DMN_Trap, 1);
          trap->process = process_handle;
          trap->vaddr = notification_vaddr;
          MAC_DMN_ActiveTrap *active_trap = mac_dmn_push_active_trap(scratch.arena, first_active_trap, trap, MAC_DMN_ActiveTrapKind_DyldNotification);
          SLLQueuePush(first_active_trap, last_active_trap, active_trap);
        }
      }
    }

    MAC_DMN_Entity *single_step_thread_entity = 0;
    if(ctrls != 0 && !dmn_handle_match(ctrls->single_step_thread, dmn_handle_zero()))
    {
      single_step_thread_entity = mac_dmn_entity_from_handle(ctrls->single_step_thread);
      if(single_step_thread_entity != 0 && single_step_thread_entity->kind == MAC_DMN_EntityKind_Thread)
      {
        mac_dmn_set_single_step_flag(&single_step_thread_entity->thread, 1);
      }
      else
      {
        single_step_thread_entity = 0;
      }
    }
    if(single_step_thread_entity == 0)
    {
      for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
      {
        if(entity->kind == MAC_DMN_EntityKind_Process)
        {
          single_step_thread_entity = mac_dmn_thread_entity_stepping_over_debug_trap(&entity->process);
          if(single_step_thread_entity != 0)
          {
            mac_dmn_set_single_step_flag(&single_step_thread_entity->thread, 1);
            break;
          }
        }
      }
    }

    for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
    {
      if(entity->kind == MAC_DMN_EntityKind_Process)
      {
        mac_dmn_process_set_dyld_notification_single_step_flags(&entity->process, 1);
      }
    }

    for(MAC_DMN_FlaggedTrapTask *task = first_flagged_trap_task; task != 0; task = task->next)
    {
      MAC_DMN_Entity *process_entity = mac_dmn_entity_from_handle(task->process);
      if(process_entity != 0 &&
         process_entity->kind == MAC_DMN_EntityKind_Process &&
         mac_dmn_thread_entity_stepping_over_debug_trap(&process_entity->process) == 0)
      {
        mac_dmn_process_set_debug_traps(&process_entity->process, &task->traps);
      }
    }

    B32 any_processes = 0;
    B32 any_running = 0;
    for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
    {
      if(entity->kind == MAC_DMN_EntityKind_Process)
      {
        any_processes = 1;
        MAC_DMN_Process *process = &entity->process;
        if(process->is_attached)
        {
          mac_dmn_process_suspend_frozen_threads(process, ctrls);
        }
        if(process->is_attached && mac_dmn_process_should_run(entity, ctrls))
        {
          mac_dmn_process_reply_pending_exception(process, 0);
          errno = 0;
          int ptrace_op = PT_CONTINUE;
          if(single_step_thread_entity != 0 && single_step_thread_entity->thread.process == process)
          {
            ptrace_op = PT_STEP;
          }
          if(ptrace(ptrace_op, process->pid, (caddr_t)1, 0) == 0 || errno == EBUSY)
          {
            process->is_running = 1;
            any_running = 1;
          }
        }
        else if(process->is_running)
        {
          any_running = 1;
        }
      }
    }

    if(!any_processes)
    {
      mac_dmn_push_event_not_attached(arena, &result);
    }
    else if(any_running)
    {
      int status = 0;
      pid_t wait_id = 0;
      MAC_DMN_Entity *exception_process_entity = 0;
      B32 got_mach_exception = 0;
      S32 mach_exception_signo = 0;
      for(;;)
      {
        pid_t monitor_pid = 0;
        int monitor_status = 0;
        if(mac_dmn_process_monitor_pop_exit_event(&monitor_pid, &monitor_status))
        {
          wait_id = monitor_pid;
          status = monitor_status;
          break;
        }
        MAC_DMN_ExceptionMessage exception_message = {0};
        if(mac_dmn_exception_message_receive(&exception_message, 10))
        {
          exception_process_entity = mac_dmn_process_entity_from_exception_port(exception_message.request.hdr.msgh_local_port);
          if(exception_process_entity != 0)
          {
            MAC_DMN_Process *process = &exception_process_entity->process;
            process->pending_exception = exception_message;
            mach_exception_signo = mac_dmn_soft_signal_from_exception_message(&process->pending_exception);
            if(mach_exception_signo == 0 && process->pending_exception.exception == EXC_BREAKPOINT)
            {
              mach_exception_signo = SIGTRAP;
            }
            got_mach_exception = 1;
            break;
          }
        }
        do
        {
          wait_id = waitpid(-1, &status, WNOHANG|WUNTRACED);
        }
        while(wait_id < 0 && errno == EINTR);
        if(wait_id > 0 && mac_dmn_process_entity_from_pid(wait_id) != 0)
        {
          break;
        }
      }

      for(MAC_DMN_ActiveTrap *active_trap = first_active_trap; active_trap != 0; active_trap = active_trap->next)
      {
        mac_dmn_unset_trap(active_trap);
      }
      if(single_step_thread_entity != 0)
      {
        mac_dmn_set_single_step_flag(&single_step_thread_entity->thread, 0);
      }
      for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
      {
        if(entity->kind == MAC_DMN_EntityKind_Process)
        {
          mac_dmn_process_set_dyld_notification_single_step_flags(&entity->process, 0);
        }
      }

      MAC_DMN_Entity *process_entity = got_mach_exception ? exception_process_entity : mac_dmn_process_entity_from_pid(wait_id);
      if(process_entity != 0)
      {
        if(!got_mach_exception && (WIFEXITED(status) || WIFSIGNALED(status)))
        {
          mac_dmn_ctrl_consume_exit_status(arena, &result, wait_id, status);
        }
        else
        {
          MAC_DMN_Process *process = &process_entity->process;
          process->is_running = 0;
          mac_dmn_process_resume_suspended_threads(process);
          mac_dmn_refresh_module_events(arena, &result, process_entity);
          mac_dmn_refresh_thread_events(arena, &result, process_entity);
          if(got_mach_exception || WIFSTOPPED(status))
          {
            S32 signo = got_mach_exception ? mach_exception_signo : WSTOPSIG(status);
            if(process->halt_expected && signo == SIGSTOP)
            {
              process->halt_expected = 0;
              if(mac_dmn_state->halt_requested)
              {
                mac_dmn_push_event_halt(arena, &result);
                mac_dmn_state->halt_requested = 0;
              }
            }
            else
            {
              mac_dmn_refresh_threads(process);
              MAC_DMN_ActiveTrap *hit_trap = 0;
              MAC_DMN_Entity *thread_entity = 0;
              if(signo == SIGTRAP)
              {
                thread_entity = mac_dmn_thread_entity_from_active_trap(process, first_active_trap, &hit_trap);
              }
              if(thread_entity == 0 && signo == SIGTRAP)
              {
                thread_entity = mac_dmn_thread_entity_stepping_over_dyld_notification(process);
              }
              if(thread_entity == 0 && single_step_thread_entity != 0 && single_step_thread_entity->thread.process == process)
              {
                thread_entity = single_step_thread_entity;
              }
              if(thread_entity == 0 && got_mach_exception && process->pending_exception.thread != MACH_PORT_NULL)
              {
                U64 thread_id = mac_dmn_thread_id_from_port(process->pending_exception.thread);
                thread_entity = mac_dmn_thread_entity_from_thread_id(process, thread_id);
              }
              if(thread_entity == 0)
              {
                thread_entity = process->first_thread_entity;
              }
              DMN_Trap *hit_debug_trap = 0;
              if(signo == SIGTRAP && thread_entity != 0)
              {
                hit_debug_trap = mac_dmn_thread_hit_debug_trap(&thread_entity->thread, first_flagged_trap_task);
              }
              if(signo == SIGTRAP && thread_entity != 0)
              {
                if(hit_trap != 0)
                {
                  mac_dmn_thread_write_ip(&thread_entity->thread, hit_trap->trap->vaddr);
                  if(hit_trap->kind == MAC_DMN_ActiveTrapKind_DyldNotification)
                  {
                    thread_entity->thread.is_stepping_over_dyld_notification = 1;
                    thread_entity->thread.dyld_notification_step_vaddr = hit_trap->trap->vaddr;
                    mac_dmn_refresh_module_events(arena, &result, process_entity);
                  }
                  else
                  {
                    mac_dmn_push_event_breakpoint(arena, &result, process_entity, thread_entity, hit_trap->trap->vaddr, hit_trap->trap->id);
                  }
                }
                else if(hit_debug_trap != 0)
                {
                  thread_entity->thread.is_stepping_over_debug_trap = 1;
                  mac_dmn_push_event_data_breakpoint(arena, &result, process_entity, thread_entity, hit_debug_trap);
                }
                else if(thread_entity->thread.is_stepping_over_dyld_notification)
                {
                  thread_entity->thread.is_stepping_over_dyld_notification = 0;
                  thread_entity->thread.dyld_notification_step_vaddr = 0;
                }
                else if(single_step_thread_entity == thread_entity)
                {
                  thread_entity->thread.is_stepping_over_debug_trap = 0;
                  mac_dmn_push_event_single_step(arena, &result, process_entity, thread_entity);
                }
                else
                {
                  mac_dmn_push_event_exception(arena, &result, process_entity, thread_entity, signo);
                }
              }
              else
              {
                mac_dmn_push_event_exception(arena, &result, process_entity, thread_entity, signo);
              }
            }
          }
        }
      }
      else
      {
        mac_dmn_push_event_not_attached(arena, &result);
      }
      for(MAC_DMN_FlaggedTrapTask *task = first_flagged_trap_task; task != 0; task = task->next)
      {
        MAC_DMN_Entity *process_entity = mac_dmn_entity_from_handle(task->process);
        if(process_entity != 0 && process_entity->kind == MAC_DMN_EntityKind_Process)
        {
          mac_dmn_process_clear_debug_traps(&process_entity->process);
        }
      }
    }
    else
    {
      for(MAC_DMN_ActiveTrap *active_trap = first_active_trap; active_trap != 0; active_trap = active_trap->next)
      {
        mac_dmn_unset_trap(active_trap);
      }
      for(MAC_DMN_FlaggedTrapTask *task = first_flagged_trap_task; task != 0; task = task->next)
      {
        MAC_DMN_Entity *process_entity = mac_dmn_entity_from_handle(task->process);
        if(process_entity != 0 && process_entity->kind == MAC_DMN_EntityKind_Process)
        {
          mac_dmn_process_clear_debug_traps(&process_entity->process);
        }
      }
      for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
      {
        if(entity->kind == MAC_DMN_EntityKind_Process)
        {
          mac_dmn_process_resume_suspended_threads(&entity->process);
        }
      }
      if(single_step_thread_entity != 0)
      {
        mac_dmn_set_single_step_flag(&single_step_thread_entity->thread, 0);
      }
      for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
      {
        if(entity->kind == MAC_DMN_EntityKind_Process)
        {
          mac_dmn_process_set_dyld_notification_single_step_flags(&entity->process, 0);
        }
      }
    }
    scratch_end(scratch);
  }

  return result;
}

internal void
dmn_halt(U64 code, U64 user_data)
{
  mac_dmn_state->halt_requested = 1;
  mac_dmn_state->halt_code = code;
  mac_dmn_state->halt_user_data = user_data;
  for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
  {
    if(entity->kind == MAC_DMN_EntityKind_Process)
    {
      MAC_DMN_Process *process = &entity->process;
      mac_dmn_process_request_halt(process);
    }
  }
}

internal B32
dmn_access_open(void)
{
  B32 result = 0;
  if(mac_dmn_ctrl_thread)
  {
    result = 1;
  }
  else
  {
    mutex_take(mac_dmn_state->access_mutex);
    result = !mac_dmn_state->access_run_state;
  }
  return result;
}

internal void
dmn_access_close(void)
{
  if(!mac_dmn_ctrl_thread)
  {
    mutex_drop(mac_dmn_state->access_mutex);
  }
}

internal U64
dmn_process_memory_reserve(DMN_Handle handle, U64 vaddr, U64 size)
{
  U64 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
    if(process != 0 && process->task != MACH_PORT_NULL)
    {
      mach_vm_address_t address = (mach_vm_address_t)vaddr;
      int flags = (vaddr == 0) ? VM_FLAGS_ANYWHERE : VM_FLAGS_FIXED;
      if(mach_vm_allocate(process->task, &address, size, flags) == KERN_SUCCESS)
      {
        mach_vm_protect(process->task, address, size, 0, VM_PROT_NONE);
        result = address;
      }
    }
  }
  return result;
}

internal void
dmn_process_memory_commit(DMN_Handle handle, U64 vaddr, U64 size)
{
  DMN_AccessScope
  {
    MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
    if(process != 0 && process->task != MACH_PORT_NULL)
    {
      mach_vm_protect(process->task, (mach_vm_address_t)vaddr, size, 0, VM_PROT_READ|VM_PROT_WRITE);
    }
  }
}

internal void
dmn_process_memory_decommit(DMN_Handle handle, U64 vaddr, U64 size)
{
  DMN_AccessScope
  {
    MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
    if(process != 0 && process->task != MACH_PORT_NULL)
    {
      mach_vm_protect(process->task, (mach_vm_address_t)vaddr, size, 0, VM_PROT_NONE);
    }
  }
}

internal void
dmn_process_memory_release(DMN_Handle handle, U64 vaddr, U64 size)
{
  DMN_AccessScope
  {
    MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
    if(process != 0 && process->task != MACH_PORT_NULL)
    {
      mach_vm_deallocate(process->task, (mach_vm_address_t)vaddr, size);
    }
  }
}

internal void
dmn_process_memory_protect(DMN_Handle handle, U64 vaddr, U64 size, AccessFlags flags)
{
  DMN_AccessScope
  {
    MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
    if(process != 0 && process->task != MACH_PORT_NULL)
    {
      mach_vm_protect(process->task, (mach_vm_address_t)vaddr, size, 0, mac_dmn_vm_prot_from_access_flags(flags));
    }
  }
}

internal U64
dmn_process_read(DMN_Handle handle, Rng1U64 range, void *dst)
{
  U64 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
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
  }
  return result;
}

internal B32
dmn_process_write(DMN_Handle handle, Rng1U64 range, void *src)
{
  B32 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Process *process = mac_dmn_process_from_handle(handle);
    if(process != 0 && process->task != MACH_PORT_NULL)
    {
      result = mac_dmn_process_write_with_protect(process, range, src);
    }
  }
  return result;
}

internal Arch
dmn_arch_from_thread(DMN_Handle handle)
{
  Arch result = Arch_Null;
  DMN_AccessScope
  {
    MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(handle);
    if(thread != 0)
    {
      result = thread->arch;
    }
  }
  return result;
}

internal U64
dmn_stack_base_vaddr_from_thread(DMN_Handle handle)
{
  U64 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(handle);
    result = mac_dmn_stack_base_vaddr_from_thread(thread);
  }
  return result;
}

internal U64
dmn_tls_root_vaddr_from_thread(DMN_Handle handle)
{
  return 0;
}

internal B32
dmn_thread_read_reg_block(DMN_Handle handle, void *reg_block)
{
  B32 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(handle);
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
#if ARCH_ARM64
        case Arch_arm64:
        {
          arm_thread_state64_t state = {0};
          mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
          if(thread_get_state(thread->thread, ARM_THREAD_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
          {
            mac_dmn_arm64_reg_block_from_thread_state((ARM64_RegBlock *)reg_block, &state);
            arm_neon_state64_t neon_state = {0};
            mach_msg_type_number_t neon_count = ARM_NEON_STATE64_COUNT;
            if(thread_get_state(thread->thread, ARM_NEON_STATE64, (thread_state_t)&neon_state, &neon_count) == KERN_SUCCESS)
            {
              mac_dmn_arm64_reg_block_from_neon_state((ARM64_RegBlock *)reg_block, &neon_state);
              result = 1;
            }
          }
        }break;
#endif
      }
    }
  }
  return result;
}

internal B32
dmn_thread_write_reg_block(DMN_Handle handle, void *reg_block)
{
  B32 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Thread *thread = mac_dmn_thread_from_handle(handle);
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
#if ARCH_ARM64
        case Arch_arm64:
        {
          arm_thread_state64_t state = {0};
          mac_dmn_arm64_thread_state_from_reg_block(&state, (ARM64_RegBlock *)reg_block);
          if(thread_set_state(thread->thread, ARM_THREAD_STATE64, (thread_state_t)&state, ARM_THREAD_STATE64_COUNT) == KERN_SUCCESS)
          {
            arm_neon_state64_t neon_state = {0};
            mach_msg_type_number_t neon_count = ARM_NEON_STATE64_COUNT;
            if(thread_get_state(thread->thread, ARM_NEON_STATE64, (thread_state_t)&neon_state, &neon_count) == KERN_SUCCESS)
            {
              mac_dmn_arm64_neon_state_from_reg_block(&neon_state, (ARM64_RegBlock *)reg_block);
              result = (thread_set_state(thread->thread, ARM_NEON_STATE64, (thread_state_t)&neon_state, ARM_NEON_STATE64_COUNT) == KERN_SUCCESS);
            }
          }
        }break;
#endif
      }
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
