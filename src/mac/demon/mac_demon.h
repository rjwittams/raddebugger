// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_DEMON_H
#define MAC_DEMON_H

#pragma push_macro("internal")
#pragma push_macro("global")
#pragma push_macro("FileInfo")
#undef internal
#undef global
#define FileInfo MACOS_SDK_FileInfo
#include <mach/mach.h>
#include <mach/mach_vm.h>
#undef FileInfo
#pragma pop_macro("FileInfo")
#pragma pop_macro("global")
#pragma pop_macro("internal")

#include <libproc.h>
#include <spawn.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef enum MAC_DMN_EntityKind
{
  MAC_DMN_EntityKind_Null,
  MAC_DMN_EntityKind_Process,
  MAC_DMN_EntityKind_Thread,
  MAC_DMN_EntityKind_Module,
}
MAC_DMN_EntityKind;

typedef struct MAC_DMN_Entity MAC_DMN_Entity;
typedef struct MAC_DMN_Process MAC_DMN_Process;
struct MAC_DMN_Process
{
  pid_t pid;
  mach_port_t task;
  Arch arch;
  B32 is_attached;
  B32 needs_attach_events;
  MAC_DMN_Entity *first_thread_entity;
  MAC_DMN_Entity *last_thread_entity;
  MAC_DMN_Entity *first_module_entity;
  MAC_DMN_Entity *last_module_entity;
};

typedef struct MAC_DMN_Thread MAC_DMN_Thread;
struct MAC_DMN_Thread
{
  MAC_DMN_Process *process;
  mach_port_t thread;
  U64 thread_id;
  Arch arch;
};

typedef struct MAC_DMN_Module MAC_DMN_Module;
struct MAC_DMN_Module
{
  MAC_DMN_Process *process;
  U64 base_vaddr;
  U64 size;
  String8 path;
  Arch arch;
};

struct MAC_DMN_Entity
{
  MAC_DMN_Entity *next;
  MAC_DMN_EntityKind kind;
  U32 gen;
  union
  {
    MAC_DMN_Process process;
    MAC_DMN_Thread thread;
    MAC_DMN_Module module;
  };
};

typedef struct MAC_DMN_State MAC_DMN_State;
struct MAC_DMN_State
{
  Arena *arena;
  MAC_DMN_Entity *free_entity;
  MAC_DMN_Entity *first_process_entity;
  MAC_DMN_Entity *last_process_entity;
};

typedef struct MAC_DMN_ProcessIterState MAC_DMN_ProcessIterState;
struct MAC_DMN_ProcessIterState
{
  struct kinfo_proc *v;
  U64 count;
  U64 idx;
};

global MAC_DMN_State *mac_dmn_state = 0;

internal DMN_Handle mac_dmn_handle_from_entity(MAC_DMN_Entity *entity);
internal MAC_DMN_Entity *mac_dmn_entity_from_handle(DMN_Handle handle);
internal MAC_DMN_Entity *mac_dmn_entity_alloc(MAC_DMN_EntityKind kind);
internal void mac_dmn_entity_release(MAC_DMN_Entity *entity);
internal void mac_dmn_process_entity_release(MAC_DMN_Entity *entity);
internal MAC_DMN_Process *mac_dmn_process_from_handle(DMN_Handle handle);
internal MAC_DMN_Thread *mac_dmn_thread_from_handle(DMN_Handle handle);
internal MAC_DMN_Module *mac_dmn_module_from_handle(DMN_Handle handle);
internal vm_prot_t mac_dmn_vm_prot_from_access_flags(AccessFlags flags);
internal Arch mac_dmn_host_arch(void);
internal U64 mac_dmn_thread_id_from_port(mach_port_t thread);
internal MAC_DMN_Entity *mac_dmn_process_entity_alloc(pid_t pid, mach_port_t task, B32 is_attached, B32 needs_attach_events);
internal MAC_DMN_Entity *mac_dmn_thread_entity_alloc(MAC_DMN_Process *process, mach_port_t thread, Arch arch);
internal MAC_DMN_Entity *mac_dmn_module_entity_alloc(MAC_DMN_Process *process, U64 base_vaddr, U64 size, String8 path, Arch arch);
internal void mac_dmn_refresh_threads(MAC_DMN_Process *process);
internal String8 mac_dmn_executable_path_from_pid(Arena *arena, pid_t pid);
internal U64 mac_dmn_main_module_base_vaddr_from_process(MAC_DMN_Process *process, MachO_UUID expected_uuid);
internal void mac_dmn_refresh_initial_module(MAC_DMN_Process *process);
internal void mac_dmn_push_event_create_process(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity);
internal void mac_dmn_push_event_create_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity);
internal void mac_dmn_push_event_load_module(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *module_entity);
internal void mac_dmn_push_event_handshake_complete(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity);
#if ARCH_X64
internal void mac_dmn_x64_reg_block_from_thread_state(X64_RegBlock *dst, x86_thread_state64_t *src);
internal void mac_dmn_x64_thread_state_from_reg_block(x86_thread_state64_t *dst, X64_RegBlock *src);
#endif

#endif // MAC_DEMON_H
