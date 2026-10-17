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
  char *marker_path = getenv("RAD_TRAP_TEST_MARKER");
  B32 detach_init = mode != 0 && strcmp(mode, "detach-init") == 0;
  B32 detach_healthy = mode != 0 && strcmp(mode, "detach-healthy") == 0;
  B32 detach_trap = mode != 0 && strcmp(mode, "detach-trap") == 0;
  B32 detach_moved_pc = mode != 0 && strcmp(mode, "detach-moved-pc") == 0;
  B32 detach = detach_init || detach_healthy || detach_trap || detach_moved_pc;
  B32 fault_address = mode != 0 && strcmp(mode, "fault-address") == 0;
  if(target_path == 0 || mode == 0 || (detach && marker_path == 0))
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
  if(detach)
  {
    unlink(marker_path);
    str8_list_push(arena, &launch.cmd_line, str8_cstring(detach_init ? "init" : detach_healthy ? "healthy" : "trap"));
    str8_list_push(arena, &launch.cmd_line, str8_cstring(marker_path));
  }
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
      if((detach_init && event->kind == DMN_EventKind_HandshakeComplete) ||
         (detach_healthy && event->kind == DMN_EventKind_Exception && event->code == SIGSTOP) ||
         ((detach_trap || detach_moved_pc) && event->kind == DMN_EventKind_Trap))
      {
        if(detach_moved_pc)
        {
          ARM64_RegBlock regs = {0};
          if(!dmn_thread_read_reg_block(event->thread, &regs))
          {
            fprintf(stderr, "Could not read trap registers.\n");
            done = 1;
            break;
          }
          regs.pc = regs.x21;
          if(!dmn_thread_write_reg_block(event->thread, &regs))
          {
            fprintf(stderr, "Could not move PC to the alternate resume address.\n");
            done = 1;
            break;
          }
        }
        if(dmn_ctrl_detach(ctx, event->process))
        {
          // The API's success (or a synthetic ExitProcess) is not proof that
          // the target resumed. Require the target's post-detach completion.
          for(U64 attempt = 0; attempt < 500 && !passed; attempt += 1)
          {
            char marker[32] = {0};
            int fd = open(marker_path, O_RDONLY);
            if(fd >= 0)
            {
              ssize_t size = read(fd, marker, sizeof(marker));
              close(fd);
              char *expected = detach_moved_pc ? "moved\n" : "completed\n";
              passed = size == (ssize_t)strlen(expected) && memcmp(marker, expected, size) == 0;
            }
            if(!passed)
            {
              usleep(10000);
            }
          }
          if(!passed)
          {
            fprintf(stderr, "FAIL: Detached target did not reach completion.\n");
          }
        }
        else
        {
          fprintf(stderr, "FAIL: Detach was rejected.\n");
        }
        done = 1;
      }
      else if(fault_address && event->kind == DMN_EventKind_Exception)
      {
        passed = (event->address == 0x12345 && event->instruction_pointer != 0 &&
                  event->instruction_pointer != event->address);
        printf("Fault address: 0x%llx, PC: 0x%llx\n",
               (unsigned long long)event->address, (unsigned long long)event->instruction_pointer);
        done = 1;
      }
      else if(event->kind == DMN_EventKind_Trap)
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
        passed = !detach && saw_trap && event->code == 23;
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
  if(test_pid > 0 && (!passed || fault_address))
  {
    kill(test_pid, SIGKILL);
    kill(test_pid, SIGCONT);
  }
  printf("%s: %s\n", mode, passed ? (fault_address ? "PASS (fault address differs from PC)" :
                                      detach ? "PASS (target completed after detach)" : "PASS (target exited 23)") : "FAIL");
  exit(passed ? 0 : 1);
}
