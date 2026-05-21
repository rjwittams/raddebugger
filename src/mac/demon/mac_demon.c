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

internal char **
mac_dmn_argv_from_launch_params(Arena *arena, ProcessLaunchParams *params)
{
  char **argv = push_array(arena, char *, params->cmd_line.node_count + 1);
  if(params->cmd_line.first != 0)
  {
    String8List path_parts = str8_split_path(arena, params->path);
    str8_list_push(arena, &path_parts, params->cmd_line.first->string);
    String8 path_to_exe = str8_path_list_join_by_style(arena, &path_parts, PathStyle_SystemAbsolute);
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
    ptrace(PT_SIGEXC, 0, 0, 0);
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
      kill(pid, SIGKILL);
      waitpid(pid, 0, WNOHANG);
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
mac_dmn_process_entity_alloc(pid_t pid, mach_port_t task, B32 is_attached, B32 needs_attach_events)
{
  MAC_DMN_Entity *entity = mac_dmn_entity_alloc(MAC_DMN_EntityKind_Process);
  entity->process.pid = pid;
  entity->process.task = task;
  entity->process.arch = mac_dmn_host_arch();
  entity->process.is_attached = is_attached;
  entity->process.needs_attach_events = needs_attach_events;
  SLLQueuePush(mac_dmn_state->first_process_entity, mac_dmn_state->last_process_entity, entity);
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
        if(mac_dmn_thread_entity_from_thread_id(process, thread_id) == 0)
        {
          MAC_DMN_Entity *thread_entity = mac_dmn_thread_entity_alloc(process, threads[idx], process->arch);
          if(arena != 0 && events != 0)
          {
            mac_dmn_push_event_create_thread(arena, events, process_entity, thread_entity);
          }
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
        if(!module_entity->module.is_live)
        {
          if(events != 0)
          {
            mac_dmn_push_event_unload_module(arena, events, process_entity, module_entity);
          }
          mac_dmn_module_entity_release(module_entity);
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
      if(!thread_entity->thread.is_suspended_for_run &&
         !mac_dmn_thread_should_run(thread_entity, ctrls) &&
         thread_suspend(thread_entity->thread.thread) == KERN_SUCCESS)
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
    good_write = dmn_process_write(trap->process, r1u64(trap->vaddr, trap->vaddr + trap_inst.size), trap_inst.str);
  }
  MAC_DMN_ActiveTrap *result = push_array(arena, MAC_DMN_ActiveTrap, 1);
  result->kind = kind;
  result->good = good_read && good_write;
  result->trap = trap;
  result->swap_bytes = str8(swap_bytes, trap_inst.size);
  return result;
}

internal void
mac_dmn_unset_trap(MAC_DMN_ActiveTrap *active_trap)
{
  if(active_trap->good)
  {
    dmn_process_write(active_trap->trap->process,
                      r1u64(active_trap->trap->vaddr, active_trap->trap->vaddr + active_trap->swap_bytes.size),
                      active_trap->swap_bytes.str);
  }
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
          if(active_trap->good &&
             trap_process == process &&
             trap_size <= ip &&
             active_trap->trap->vaddr == ip - trap_size)
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
    if(size <= max_U32 && mach_vm_write(process->task, range.min, (vm_offset_t)src, (mach_msg_type_number_t)size) == KERN_SUCCESS)
    {
      result = 1;
    }
    else
    {
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

        vm_prot_t old_protection = info.protection;
        vm_prot_t write_protection = old_protection|VM_PROT_WRITE|VM_PROT_COPY;
        kern_return_t protect_code = mach_vm_protect(process->task, write_address, region_write_size, 0, write_protection);
        if(protect_code != KERN_SUCCESS)
        {
          result = 0;
          break;
        }

        mach_msg_type_number_t write_size = (mach_msg_type_number_t)region_write_size;
        kern_return_t write_code = mach_vm_write(process->task, write_address, (vm_offset_t)((U8 *)src + write_off), write_size);
        mach_vm_protect(process->task, write_address, region_write_size, 0, old_protection);
        if(write_code != KERN_SUCCESS)
        {
          result = 0;
          break;
        }
        write_off += region_write_size;
      }
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

internal void
dmn_init(void)
{
  Arena *arena = arena_alloc();
  mac_dmn_state = push_array(arena, MAC_DMN_State, 1);
  mac_dmn_state->arena = arena;
  mac_dmn_state->access_mutex = mutex_alloc();
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
  pid_t pid = mac_dmn_launch_traced_process(params);
  U32 result = 0;
  if(pid != 0)
  {
    mach_port_t task = MACH_PORT_NULL;
    if(task_for_pid(mach_task_self(), (int)pid, &task) == KERN_SUCCESS && task != MACH_PORT_NULL)
    {
      mac_dmn_process_entity_alloc(pid, task, 1, 1);
      result = (U32)pid;
    }
    else
    {
      kill(pid, SIGKILL);
      waitpid(pid, 0, WNOHANG);
    }
  }
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
      mac_dmn_process_entity_alloc((pid_t)pid, task, ptrace_result == 0, 1);
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
    Temp scratch = scratch_begin(0, 0);
    MAC_DMN_ActiveTrap *first_active_trap = 0;
    MAC_DMN_ActiveTrap *last_active_trap = 0;
    if(ctrls != 0)
    {
      for(DMN_TrapChunkNode *n = ctrls->traps.first; n != 0; n = n->next)
      {
        for(U64 trap_idx = 0; trap_idx < n->count; trap_idx += 1)
        {
          DMN_Trap *trap = n->v + trap_idx;
          if(trap->flags == 0)
          {
            MAC_DMN_ActiveTrap *active_trap = mac_dmn_set_trap(scratch.arena, trap, MAC_DMN_ActiveTrapKind_User);
            SLLQueuePush(first_active_trap, last_active_trap, active_trap);
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
          MAC_DMN_ActiveTrap *active_trap = mac_dmn_set_trap(scratch.arena, trap, MAC_DMN_ActiveTrapKind_DyldNotification);
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

    for(MAC_DMN_Entity *entity = mac_dmn_state->first_process_entity; entity != 0; entity = entity->next)
    {
      if(entity->kind == MAC_DMN_EntityKind_Process)
      {
        mac_dmn_process_set_dyld_notification_single_step_flags(&entity->process, 1);
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
        if(process->is_attached && mac_dmn_process_should_run(entity, ctrls))
        {
          mac_dmn_process_suspend_frozen_threads(process, ctrls);
          errno = 0;
          if(ptrace(PT_CONTINUE, process->pid, (caddr_t)1, 0) == 0 || errno == EBUSY)
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
      do
      {
        wait_id = waitpid(-1, &status, 0);
      }
      while(wait_id < 0 && errno == EINTR);

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

      MAC_DMN_Entity *process_entity = mac_dmn_process_entity_from_pid(wait_id);
      if(process_entity != 0)
      {
        MAC_DMN_Process *process = &process_entity->process;
        process->is_running = 0;
        mac_dmn_process_resume_suspended_threads(process);
        mac_dmn_refresh_thread_events(arena, &result, process_entity);
        mac_dmn_refresh_module_events(arena, &result, process_entity);
        if(WIFEXITED(status))
        {
          mac_dmn_push_event_exit_process(arena, &result, process_entity, (U32)WEXITSTATUS(status));
          mac_dmn_process_entity_release(process_entity);
        }
        else if(WIFSIGNALED(status))
        {
          mac_dmn_push_event_exit_process(arena, &result, process_entity, (U32)WTERMSIG(status));
          mac_dmn_process_entity_release(process_entity);
        }
        else if(WIFSTOPPED(status))
        {
          S32 signo = WSTOPSIG(status);
          if(mac_dmn_state->halt_requested && signo == SIGSTOP)
          {
            mac_dmn_push_event_halt(arena, &result);
            mac_dmn_state->halt_requested = 0;
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
            if(thread_entity == 0)
            {
              thread_entity = process->first_thread_entity;
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
              else if(thread_entity->thread.is_stepping_over_dyld_notification)
              {
                thread_entity->thread.is_stepping_over_dyld_notification = 0;
                thread_entity->thread.dyld_notification_step_vaddr = 0;
              }
              else if(single_step_thread_entity == thread_entity)
              {
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
      else
      {
        mac_dmn_push_event_not_attached(arena, &result);
      }
    }
    else
    {
      for(MAC_DMN_ActiveTrap *active_trap = first_active_trap; active_trap != 0; active_trap = active_trap->next)
      {
        mac_dmn_unset_trap(active_trap);
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
      kill(process->pid, SIGSTOP);
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
