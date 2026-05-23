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
#include <mach/vm_attributes.h>
#include <mach/machine/thread_status.h>
#undef FileInfo
#pragma pop_macro("FileInfo")
#pragma pop_macro("global")
#pragma pop_macro("internal")

#include <libproc.h>
#include <spawn.h>
#include <errno.h>
#include <signal.h>
#include <mach-o/dyld_images.h>
#include <mach/mig_errors.h>
#include <mach/ndr.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef EXC_I386_SGL
# define EXC_I386_SGL 1
#endif
#ifndef EXC_I386_BPT
# define EXC_I386_BPT 2
#endif

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
typedef struct MAC_DMN_ExitEvent MAC_DMN_ExitEvent;

typedef union MAC_DMN_MachMsg
{
  mach_msg_header_t hdr;
  U8 data[4096];
}
MAC_DMN_MachMsg;

typedef struct MAC_DMN_ExceptionPortInfo
{
  exception_mask_t mask;
  exception_mask_t masks[EXC_TYPES_COUNT];
  mach_port_t ports[EXC_TYPES_COUNT];
  exception_behavior_t behaviors[EXC_TYPES_COUNT];
  thread_state_flavor_t flavors[EXC_TYPES_COUNT];
  mach_msg_type_number_t count;
}
MAC_DMN_ExceptionPortInfo;

typedef struct MAC_DMN_ExceptionMessage
{
  MAC_DMN_MachMsg request;
  mig_reply_error_t reply;
  mach_port_t thread;
  mach_port_t task;
  exception_type_t exception;
  mach_exception_data_type_t code[2];
  mach_msg_type_number_t code_count;
  B32 is_valid;
}
MAC_DMN_ExceptionMessage;

struct MAC_DMN_Process
{
  pid_t pid;
  mach_port_t task;
  Arch arch;
  B32 is_attached;
  B32 is_running;
  B32 needs_attach_events;
  B32 dyld_bootstrap_pending;
  B32 dyld_bootstrap_stepping;
  B32 uses_mach_exceptions;
  B32 halt_expected;
  mach_port_t exception_port;
  MAC_DMN_ExceptionPortInfo exception_port_info;
  MAC_DMN_ExceptionMessage pending_exception;
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
  B32 is_suspended_for_run;
  B32 is_stepping_over_dyld_notification;
  B32 is_stepping_over_dyld_bootstrap;
  U64 dyld_notification_step_vaddr;
  U64 dyld_bootstrap_step_vaddr;
};

typedef struct MAC_DMN_Module MAC_DMN_Module;
struct MAC_DMN_Module
{
  MAC_DMN_Process *process;
  U64 base_vaddr;
  U64 size;
  String8 path;
  Arch arch;
  B32 is_live;
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

struct MAC_DMN_ExitEvent
{
  MAC_DMN_ExitEvent *next;
  pid_t pid;
  int status;
};

typedef struct MAC_DMN_State MAC_DMN_State;
struct MAC_DMN_State
{
  Arena *arena;
  MAC_DMN_Entity *free_entity;
  MAC_DMN_Entity *first_process_entity;
  MAC_DMN_Entity *last_process_entity;
  Mutex access_mutex;
  mach_port_t exception_port_set;
  B32 access_run_state;
  volatile B32 halt_requested;
  U64 halt_code;
  U64 halt_user_data;
  int process_monitor_kq;
  Mutex process_monitor_mutex;
  Thread process_monitor_thread;
  MAC_DMN_ExitEvent *first_exit_event;
  MAC_DMN_ExitEvent *last_exit_event;
  Arena *detach_arena;
  DMN_HandleList detach_processes;
};

typedef struct MAC_DMN_ProcessIterState MAC_DMN_ProcessIterState;
struct MAC_DMN_ProcessIterState
{
  struct kinfo_proc *v;
  U64 count;
  U64 idx;
};

typedef struct MAC_DMN_ActiveTrap MAC_DMN_ActiveTrap;
typedef struct MAC_DMN_FlaggedTrapTask MAC_DMN_FlaggedTrapTask;
typedef enum MAC_DMN_ActiveTrapKind
{
  MAC_DMN_ActiveTrapKind_User,
  MAC_DMN_ActiveTrapKind_DyldNotification,
  MAC_DMN_ActiveTrapKind_DyldBootstrap,
}
MAC_DMN_ActiveTrapKind;

struct MAC_DMN_ActiveTrap
{
  MAC_DMN_ActiveTrap *next;
  MAC_DMN_ActiveTrapKind kind;
  B32 good;
  DMN_Trap *trap;
  String8 swap_bytes;
};

struct MAC_DMN_FlaggedTrapTask
{
  MAC_DMN_FlaggedTrapTask *next;
  DMN_Handle process;
  DMN_TrapChunkList traps;
};

global MAC_DMN_State *mac_dmn_state = 0;
thread_static B32 mac_dmn_ctrl_thread = 0;

internal DMN_Handle mac_dmn_handle_from_entity(MAC_DMN_Entity *entity);
internal MAC_DMN_Entity *mac_dmn_entity_from_handle(DMN_Handle handle);
internal MAC_DMN_Entity *mac_dmn_entity_alloc(MAC_DMN_EntityKind kind);
internal void mac_dmn_entity_release(MAC_DMN_Entity *entity);
internal void mac_dmn_process_entity_release(MAC_DMN_Entity *entity);
internal void mac_dmn_process_monitor_thread__entry_point(void *p);
internal void mac_dmn_process_monitor_register_pid(pid_t pid);
internal void mac_dmn_process_monitor_push_exit_event(pid_t pid, int status);
internal B32 mac_dmn_process_monitor_pop_exit_event(pid_t *pid_out, int *status_out);
internal B32 mac_dmn_process_wait_for_exit(pid_t pid, int *status_out);
internal B32 mac_dmn_ctrl_consume_exit_status(Arena *arena, DMN_EventList *events, pid_t pid, int status);
internal B32 mac_dmn_ctrl_consume_monitor_exit_event(Arena *arena, DMN_EventList *events);
internal MAC_DMN_Process *mac_dmn_process_from_handle(DMN_Handle handle);
internal MAC_DMN_Thread *mac_dmn_thread_from_handle(DMN_Handle handle);
internal MAC_DMN_Module *mac_dmn_module_from_handle(DMN_Handle handle);
internal MAC_DMN_Entity *mac_dmn_process_entity_from_pid(pid_t pid);
internal MAC_DMN_Entity *mac_dmn_thread_entity_from_thread_id(MAC_DMN_Process *process, U64 thread_id);
internal B32 mac_dmn_thread_port_is_valid(MAC_DMN_Thread *thread);
internal char **mac_dmn_argv_from_launch_params(Arena *arena, ProcessLaunchParams *params);
internal char **mac_dmn_envp_from_launch_params(Arena *arena, ProcessLaunchParams *params);
internal void mac_dmn_apply_child_stdio(ProcessLaunchParams *params);
internal pid_t mac_dmn_launch_traced_process(ProcessLaunchParams *params);
internal S32 mac_dmn_taskport_authorization_status(B32 interaction_allowed);
internal vm_prot_t mac_dmn_vm_prot_from_access_flags(AccessFlags flags);
internal Arch mac_dmn_host_arch(void);
internal U64 mac_dmn_thread_id_from_port(mach_port_t thread);
internal kern_return_t mac_dmn_exception_port_info_save(MAC_DMN_ExceptionPortInfo *info, task_t task);
internal kern_return_t mac_dmn_exception_port_info_restore(MAC_DMN_ExceptionPortInfo *info, task_t task);
internal B32 mac_dmn_process_begin_mach_exceptions(MAC_DMN_Process *process);
internal void mac_dmn_process_end_mach_exceptions(MAC_DMN_Process *process);
internal B32 mac_dmn_process_reacquire_task_after_exec(MAC_DMN_Process *process);
internal S32 mac_dmn_soft_signal_from_exception_message(MAC_DMN_ExceptionMessage *message);
internal B32 mac_dmn_process_reply_pending_exception(MAC_DMN_Process *process, S32 signal);
internal B32 mac_dmn_process_stop_for_detach(MAC_DMN_Process *process);
internal void mac_dmn_process_request_halt(MAC_DMN_Process *process);
internal MAC_DMN_Entity *mac_dmn_process_entity_from_exception_port(mach_port_t exception_port);
internal B32 mac_dmn_exception_message_receive(MAC_DMN_ExceptionMessage *message, mach_msg_timeout_t timeout);
internal MAC_DMN_Entity *mac_dmn_process_entity_alloc(pid_t pid, mach_port_t task, B32 is_attached, B32 needs_attach_events);
internal MAC_DMN_Entity *mac_dmn_thread_entity_alloc(MAC_DMN_Process *process, mach_port_t thread, Arch arch);
internal void mac_dmn_thread_entity_release(MAC_DMN_Entity *entity);
internal void mac_dmn_process_clear_thread_entities(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity);
internal MAC_DMN_Entity *mac_dmn_module_entity_alloc(MAC_DMN_Process *process, U64 base_vaddr, U64 size, String8 path, Arch arch);
internal MAC_DMN_Entity *mac_dmn_module_entity_from_base_vaddr(MAC_DMN_Process *process, U64 base_vaddr);
internal void mac_dmn_module_entity_release(MAC_DMN_Entity *entity);
internal void mac_dmn_process_clear_module_entities(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity);
internal void mac_dmn_refresh_threads(MAC_DMN_Process *process);
internal void mac_dmn_refresh_thread_events(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity);
internal String8 mac_dmn_executable_path_from_pid(Arena *arena, pid_t pid);
internal U64 mac_dmn_main_module_base_vaddr_from_process(MAC_DMN_Process *process, MachO_UUID expected_uuid);
internal U64 mac_dmn_main_module_entry_vaddr_from_process(MAC_DMN_Process *process);
internal void mac_dmn_refresh_initial_module(MAC_DMN_Process *process);
internal String8 mac_dmn_read_string(Arena *arena, MAC_DMN_Process *process, U64 vaddr, U64 max_size);
internal B32 mac_dmn_macho_image_info_from_process(MAC_DMN_Process *process, U64 base_vaddr, U64 *size_out, Arch *arch_out);
internal B32 mac_dmn_dyld_all_image_infos_is_ready(U64 all_image_infos_vaddr, struct dyld_all_image_infos *all_images);
internal B32 mac_dmn_read_dyld_all_image_infos(MAC_DMN_Process *process, struct dyld_all_image_infos *all_images_out);
internal B32 mac_dmn_read_dyld_all_image_infos_with_addr(MAC_DMN_Process *process, struct dyld_all_image_infos *all_images_out, U64 *all_image_infos_vaddr_out);
internal B32 mac_dmn_read_dyld_image_infos(Arena *arena, MAC_DMN_Process *process, struct dyld_image_info **images_out, U32 *count_out);
internal U64 mac_dmn_fix_code_vaddr(Arch arch, U64 vaddr);
internal U64 mac_dmn_dyld_notification_vaddr_from_process(MAC_DMN_Process *process);
internal B32 mac_dmn_process_vaddr_is_executable(MAC_DMN_Process *process, U64 vaddr);
internal B32 mac_dmn_exception_is_software_breakpoint(Arch arch, MAC_DMN_ExceptionMessage *exception);
internal void mac_dmn_refresh_module_events(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity);
internal B32 mac_dmn_thread_should_run(MAC_DMN_Entity *thread_entity, DMN_RunCtrls *ctrls);
internal B32 mac_dmn_process_should_run(MAC_DMN_Entity *process_entity, DMN_RunCtrls *ctrls);
internal void mac_dmn_process_suspend_frozen_threads(MAC_DMN_Process *process, DMN_RunCtrls *ctrls);
internal void mac_dmn_process_resume_suspended_threads(MAC_DMN_Process *process);
internal B32 mac_dmn_process_is_stepping_over_dyld_notification(MAC_DMN_Process *process, U64 vaddr);
internal B32 mac_dmn_process_is_stepping_over_dyld_bootstrap(MAC_DMN_Process *process, U64 vaddr);
internal MAC_DMN_Entity *mac_dmn_thread_entity_stepping_over_dyld_notification(MAC_DMN_Process *process);
internal MAC_DMN_Entity *mac_dmn_thread_entity_stepping_over_dyld_bootstrap(MAC_DMN_Process *process);
internal void mac_dmn_process_set_dyld_notification_single_step_flags(MAC_DMN_Process *process, B32 is_on);
internal void mac_dmn_process_set_dyld_bootstrap_single_step_flags(MAC_DMN_Process *process, B32 is_on);
internal MAC_DMN_ActiveTrap *mac_dmn_set_trap(Arena *arena, DMN_Trap *trap, MAC_DMN_ActiveTrapKind kind);
internal MAC_DMN_ActiveTrap *mac_dmn_push_active_trap(Arena *arena, MAC_DMN_ActiveTrap *first, DMN_Trap *trap, MAC_DMN_ActiveTrapKind kind);
internal void mac_dmn_unset_trap(MAC_DMN_ActiveTrap *active_trap);
internal B32 mac_dmn_process_sync_instruction_cache(MAC_DMN_Process *process, Rng1U64 range);
internal U64 mac_dmn_thread_read_ip(MAC_DMN_Thread *thread);
internal U64 mac_dmn_thread_read_sp(MAC_DMN_Thread *thread);
internal U64 mac_dmn_stack_base_vaddr_from_thread(MAC_DMN_Thread *thread);
internal B32 mac_dmn_thread_write_ip(MAC_DMN_Thread *thread, U64 ip);
internal B32 mac_dmn_thread_skip_past_trap_instruction(MAC_DMN_Thread *thread);
internal B32 mac_dmn_set_single_step_flag(MAC_DMN_Thread *thread, B32 is_on);
#if ARCH_ARM64
internal B32 mac_dmn_arm64_debug_state_regs_from_trap(DMN_Trap *trap, U64 *wvr_out, U64 *wcr_out);
internal U64 mac_dmn_arm64_debug_trap_idx_from_state(arm_debug_state64_t *state, U64 fault_vaddr, U64 slot_count);
internal U64 mac_dmn_arm64_supported_watchpoint_count(void);
#endif
internal B32 mac_dmn_thread_set_debug_traps(MAC_DMN_Thread *thread, DMN_TrapChunkList *traps);
internal B32 mac_dmn_thread_clear_debug_traps(MAC_DMN_Thread *thread);
internal B32 mac_dmn_process_set_debug_traps(MAC_DMN_Process *process, DMN_TrapChunkList *traps);
internal void mac_dmn_process_clear_debug_traps(MAC_DMN_Process *process);
internal DMN_Trap *mac_dmn_thread_hit_debug_trap(MAC_DMN_Thread *thread, MAC_DMN_FlaggedTrapTask *first_task);
internal MAC_DMN_ActiveTrap *mac_dmn_active_trap_from_process_vaddr(MAC_DMN_ActiveTrap *first, DMN_Handle process, U64 vaddr);
internal MAC_DMN_Entity *mac_dmn_thread_entity_from_active_trap(MAC_DMN_Process *process, MAC_DMN_ActiveTrap *first, MAC_DMN_ActiveTrap **active_trap_out);
internal B32 mac_dmn_process_write_with_protect(MAC_DMN_Process *process, Rng1U64 range, void *src);
internal void mac_dmn_push_event_create_process(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity);
internal void mac_dmn_push_event_create_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity);
internal void mac_dmn_push_event_exit_thread(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity);
internal void mac_dmn_push_event_load_module(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *module_entity);
internal void mac_dmn_push_event_unload_module(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *module_entity);
internal void mac_dmn_push_event_handshake_complete(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity);
internal void mac_dmn_push_event_exit_process(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, U32 exit_code);
internal void mac_dmn_push_detach_events(Arena *arena, DMN_EventList *events);
internal void mac_dmn_push_event_breakpoint(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity, U64 instruction_pointer, U64 user_data);
internal void mac_dmn_push_event_data_breakpoint(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity, DMN_Trap *trap);
internal void mac_dmn_push_event_single_step(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity);
internal void mac_dmn_push_event_trap(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity, S32 signo);
internal void mac_dmn_push_event_exception(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *process_entity, MAC_DMN_Entity *thread_entity, S32 signo);
internal void mac_dmn_push_event_halt(Arena *arena, DMN_EventList *events);
internal void mac_dmn_push_event_not_attached(Arena *arena, DMN_EventList *events);
#if ARCH_X64
internal void mac_dmn_x64_reg_block_from_thread_state(X64_RegBlock *dst, x86_thread_state64_t *src);
internal void mac_dmn_x64_thread_state_from_reg_block(x86_thread_state64_t *dst, X64_RegBlock *src);
#endif
#if ARCH_ARM64
internal void mac_dmn_arm64_reg_block_from_thread_state(ARM64_RegBlock *dst, arm_thread_state64_t *src);
internal void mac_dmn_arm64_thread_state_from_reg_block(arm_thread_state64_t *dst, ARM64_RegBlock *src);
internal void mac_dmn_arm64_reg_block_from_neon_state(ARM64_RegBlock *dst, arm_neon_state64_t *src);
internal void mac_dmn_arm64_neon_state_from_reg_block(arm_neon_state64_t *dst, ARM64_RegBlock *src);
#endif

#endif // MAC_DEMON_H
