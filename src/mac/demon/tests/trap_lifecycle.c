// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BUILD_CONSOLE_INTERFACE 1
#define DMN_INIT_MANUAL 1
#include "base/base_inc.h"
#include "x64/x64.h"
#include "arm64/arm64.h"
#include "macho/macho.h"
#include "arch/arch_inc.h"
#include "demon/demon_inc.h"
#include "base/base_inc.c"
#include "x64/x64.c"
#include "arm64/arm64.c"
#include "macho/macho.c"
#include "arch/arch_inc.c"
#include "demon/demon_inc.c"

global volatile sig_atomic_t test_pid;

internal void
test_timeout(int signal)
{
  if(test_pid > 0)
  {
    kill(test_pid, SIGKILL);
  }
  _exit(2);
}

internal void
entry_point(CmdLine *cmdline)
{
  char *target_path = getenv("RAD_TRAP_TEST_TARGET");
  char *mode = getenv("RAD_TRAP_TEST_MODE");
  if(target_path == 0 || mode == 0)
  {
    fprintf(stderr, "Run through run_trap_lifecycle.sh.\n");
    exit(2);
  }
  if(mac_dmn_taskport_authorization_status(0) != 0)
  {
    fprintf(stderr, "Debugger authorization unavailable; test not run.\n");
    exit(2);
  }

  signal(SIGALRM, test_timeout);
  alarm(15);
  Arena *arena = arena_alloc();
  log_select(log_alloc());
  log_scope_begin();
  dmn_init();
  DMN_CtrlCtx *ctx = dmn_ctrl_begin();
  ProcessLaunchParams launch = {0};
  str8_list_push(arena, &launch.cmd_line, str8_cstring(target_path));
  launch.path = get_process_info()->initial_path;
  launch.inherit_env = 1;
  test_pid = dmn_ctrl_launch(ctx, &launch);
  if(test_pid == 0)
  {
    LogScopeResult logs = log_scope_end(arena);
    String8 error = logs.strings[LogMsgKind_UserError];
    fprintf(stderr, "Launch failed: %.*s\n", (int)error.size, error.str);
    exit(2);
  }

  DMN_RunCtrls controls = {0};
  controls.ignore_previous_exception = 1;
  B32 saw_trap = 0;
  B32 passed = 0;
  B32 done = 0;
  for(U64 iteration = 0; iteration < 64 && !done; iteration += 1)
  {
    DMN_EventList events = dmn_ctrl_run(arena, ctx, &controls);
    for(DMN_EventNode *n = events.first; n != 0 && !done; n = n->next)
    {
      DMN_Event *event = &n->v;
      if(event->kind == DMN_EventKind_Trap)
      {
        if(saw_trap)
        {
          fprintf(stderr, "FAIL: Continue re-hit the target trap.\n");
          done = 1;
        }
        else
        {
          saw_trap = 1;
          ARM64_RegBlock regs = {0};
          if(!dmn_thread_read_reg_block(event->thread, &regs))
          {
            fprintf(stderr, "Could not read trap registers.\n");
            done = 1;
          }
          else if(strcmp(mode, "call") == 0)
          {
            DMN_ThreadCallParams params = {0};
            params.function_vaddr = regs.x20;
            params.return_value_kind = DMN_ThreadCallValueKind_U64;
            DMN_ThreadCallResult call = dmn_thread_call(arena, ctx, event->thread, &params);
            if(!call.success || call.return_value.u64 != 42)
            {
              fprintf(stderr, "Target call failed: %.*s\n", (int)call.error.size, call.error.str);
              done = 1;
            }
          }
        }
      }
      else if(event->kind == DMN_EventKind_ExitProcess)
      {
        passed = saw_trap && event->code == 23;
        test_pid = 0;
        done = 1;
      }
      else if(event->kind == DMN_EventKind_Error || event->kind == DMN_EventKind_Exception)
      {
        fprintf(stderr, "Unexpected debugger event: %u\n", event->kind);
        done = 1;
      }
    }
  }
  if(test_pid > 0)
  {
    kill(test_pid, SIGKILL);
    kill(test_pid, SIGCONT);
  }
  printf("%s: %s\n", mode, passed ? "PASS (target exited 23)" : "FAIL");
  exit(passed ? 0 : 1);
}
