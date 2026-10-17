// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_BASE_H
#define MAC_BASE_H

////////////////////////////////
//~ Includes

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

typedef struct tm tm;
typedef struct timespec timespec;

////////////////////////////////
//~ Mac Call Interruption Retry Helper

#define MAC_RETRY_ON_EINTR(expr)          \
(__extension__({                           \
__typeof__(expr) __ret;                    \
do {                                       \
__ret = (expr);                            \
} while ((__ret == -1) && errno == EINTR); \
__ret;                                     \
}))


////////////////////////////////
//~ File Iterator

typedef struct MAC_FileIter MAC_FileIter;
struct MAC_FileIter
{
  DIR *dir;
  struct dirent *dp;
  String8 path;
};
StaticAssert(sizeof(Member(FileIter, memory)) >= sizeof(MAC_FileIter), mac_file_iter_size_check);

////////////////////////////////
//~ Safe Call Handler Chain

typedef struct MAC_SafeCallChain MAC_SafeCallChain;
struct MAC_SafeCallChain
{
  MAC_SafeCallChain *next;
  ThreadEntryPointFunctionType *fail_handler;
  void *ptr;
};

////////////////////////////////
//~ Entities

typedef enum MAC_EntityKind
{
  MAC_EntityKind_Thread,
  MAC_EntityKind_SharedMemory,
  MAC_EntityKind_Mutex,
  MAC_EntityKind_RWMutex,
  MAC_EntityKind_ConditionVariable,
  MAC_EntityKind_Semaphore,
  MAC_EntityKind_Barrier,
}
MAC_EntityKind;

typedef enum MAC_IPCNameKind
{
  MAC_IPCNameKind_SharedMemory,
  MAC_IPCNameKind_Semaphore,
}
MAC_IPCNameKind;

typedef struct MAC_IPCNameNode MAC_IPCNameNode;
struct MAC_IPCNameNode
{
  MAC_IPCNameNode *next;
  String8 name;
  MAC_IPCNameKind kind;
  B32 active;
};

typedef struct MAC_Entity MAC_Entity;
struct MAC_Entity
{
  MAC_Entity *next;
  MAC_EntityKind kind;
  union
  {
    struct
    {
      pthread_t handle;
      ThreadEntryPointFunctionType *func;
      void *ptr;
    } thread;
    struct
    {
      int id;
      MAC_IPCNameNode *name_node;
    } shared_memory;
    pthread_mutex_t mutex_handle;
    pthread_rwlock_t rwmutex_handle;
    struct
    {
      pthread_cond_t cond_handle;
      pthread_mutex_t rwlock_mutex_handle;
    } cv;
    struct
    {
      sem_t *handle;
      MAC_IPCNameNode *name_node;
    } semaphore;
    struct
    {
      pthread_mutex_t mutex;
      pthread_cond_t cond;
      U64 count;
      U64 crossing_count;
      U64 generation;
    } barrier;
  };
};

////////////////////////////////
//~ State

typedef struct MAC_State MAC_State;
struct MAC_State
{
  Arena *arena;
  SystemInfo system_info;
  ProcessInfo process_info;
  pthread_mutex_t entity_mutex;
  Arena *entity_arena;
  MAC_Entity *entity_free;
  MAC_IPCNameNode *first_owned_ipc_name;
  MAC_IPCNameNode *last_owned_ipc_name;
  U64 default_env_count;
  char **default_env;
};

////////////////////////////////
//~ Globals

global MAC_State mac_state = {0};
thread_static MAC_SafeCallChain *mac_safe_call_chain = 0;

////////////////////////////////
//~ Helpers

internal DateTime mac_date_time_from_tm(tm in, U32 msec);
internal MAC_IPCNameNode *mac_ipc_name_node_alloc(String8 name, MAC_IPCNameKind kind);
internal void mac_ipc_name_node_unlink(MAC_IPCNameNode *node);
internal void mac_ipc_name_node_cleanup(void);
internal tm mac_tm_from_date_time(DateTime dt);
internal timespec mac_timespec_from_date_time(DateTime dt);
internal DenseTime mac_dense_time_from_timespec(timespec in);
internal FileProperties mac_file_properties_from_stat(struct stat *s);
internal String8 mac_posix_ipc_name(Arena *arena, String8 name);
internal void mac_safe_call_sig_handler(int x);

////////////////////////////////
//~ Entities

internal MAC_Entity *mac_entity_alloc(MAC_EntityKind kind);
internal void mac_entity_release(MAC_Entity *entity);

////////////////////////////////
//~ Thread Entry Point

internal void *mac_thread_entry_point(void *ptr);

#endif // MAC_BASE_H
