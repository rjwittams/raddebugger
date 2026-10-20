// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define IPC_RECORD_SIZE 104
#if OS_WINDOWS
# define IPC_RECORD_OS 1
# include <aclapi.h>
#elif OS_LINUX
# define IPC_RECORD_OS 2
#elif OS_MAC
# define IPC_RECORD_OS 3
# include <libproc.h>
# include <sys/acl.h>
#else
# define IPC_RECORD_OS 0
#endif
#if OS_LINUX || OS_MAC
# include <sys/file.h>
#endif

struct IPC_Registration
{
#if OS_WINDOWS
  HANDLE file;
#else
  int directory;
  int file;
  char name[32];
#endif
};

internal B32
ipc_process_birth(U32 pid, U256 *out)
{
  U256 birth = {0};
#if OS_WINDOWS
  if(pid == 0) { return 0; }
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if(process == 0) { return 0; }
  FILETIME created, exited, kernel, user;
  B32 ok = GetProcessTimes(process, &created, &exited, &kernel, &user);
  CloseHandle(process);
  if(!ok) { return 0; }
  birth.u64[0] = (U64)created.dwLowDateTime | (U64)created.dwHighDateTime << 32;
#elif OS_MAC
  if(pid == 0 || pid > max_S32) { return 0; }
  struct proc_bsdinfo info = {0};
  if(proc_pidinfo((int)pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info)) { return 0; }
  birth.u64[0] = info.pbi_start_tvsec;
  birth.u64[1] = info.pbi_start_tvusec;
#elif OS_LINUX
  if(pid == 0 || pid > max_S32) { return 0; }
  char path[64];
  snprintf(path, sizeof(path), "/proc/%u/stat", pid);
  int fd = open(path, O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
  if(fd == -1) { return 0; }
  U8 bytes[4096];
  S64 size;
  do { size = read(fd, bytes, sizeof(bytes)); } while(size == -1 && errno == EINTR);
  close(fd);
  if(size <= 0 || size == sizeof(bytes)) { return 0; }
  U64 pos = (U64)size;
  while(pos != 0 && bytes[pos-1] != ')') { pos -= 1; }
  if(pos == 0) { return 0; }
  U64 ticks = 0;
  for(U64 field = 3; field <= 22; field += 1)
  {
    while(pos < (U64)size && bytes[pos] == ' ') { pos += 1; }
    U64 start = pos;
    while(pos < (U64)size && bytes[pos] != ' ' && bytes[pos] != '\n') { pos += 1; }
    if(start == pos) { return 0; }
    if(field == 22 && !try_u64_from_str8_c_rules(str8(bytes+start, pos-start), &ticks)) { return 0; }
  }
  fd = open("/proc/sys/kernel/random/boot_id", O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
  if(fd == -1) { return 0; }
  U8 identity[44];
  do { size = read(fd, identity, 36); } while(size == -1 && errno == EINTR);
  close(fd);
  if(size != 36) { return 0; }
  ipc_u64_to_wire(identity+36, ticks);
  SHA256 digest = sha256_from_data(str8(identity, sizeof(identity)));
  MemoryCopy(birth.u8, digest.u8, sizeof(birth.u8));
#else
  return 0;
#endif
  *out = birth;
  return 1;
}

internal B32
ipc_record_valid(U8 *bytes, U32 pid)
{
  if(!MemoryMatch(bytes, "RDIE", 4) || bytes[4] != 1 || bytes[5] != IPC_RECORD_OS ||
     bytes[6] != 0 || bytes[7] != 0 || ipc_u64_from_wire(bytes+8) != pid ||
     (bytes[48] | bytes[49]) == 0) { return 0; }
  for(U64 idx = 50; idx < 56; idx += 1) { if(bytes[idx] != 0) { return 0; } }
  U8 id_bits = 0, key_bits = 0;
  for(U64 idx = 56; idx < 72; idx += 1) { id_bits |= bytes[idx]; }
  for(U64 idx = 72; idx < 104; idx += 1) { key_bits |= bytes[idx]; }
  return id_bits != 0 && key_bits != 0;
}

internal void
ipc_record_write(U8 *bytes, U32 pid, U256 birth, IPC_Endpoint endpoint)
{
  MemoryZero(bytes, IPC_RECORD_SIZE);
  MemoryCopy(bytes, "RDIE", 4);
  bytes[4] = 1;
  bytes[5] = IPC_RECORD_OS;
  ipc_u64_to_wire(bytes+8, pid);
  MemoryCopy(bytes+16, birth.u8, sizeof(birth.u8));
  bytes[48] = (U8)endpoint.address.port;
  bytes[49] = (U8)(endpoint.address.port >> 8);
  MemoryCopy(bytes+56, endpoint.instance_id.u8, sizeof(endpoint.instance_id.u8));
  MemoryCopy(bytes+72, endpoint.auth_key.u8, sizeof(endpoint.auth_key.u8));
}

#if OS_WINDOWS
typedef struct IPC_WinSecurity IPC_WinSecurity;
struct IPC_WinSecurity
{
  AlignType(8) U8 sid[SECURITY_MAX_SID_SIZE];
  AlignType(8) U8 acl[sizeof(ACL)+sizeof(ACCESS_ALLOWED_ACE)+SECURITY_MAX_SID_SIZE];
  SECURITY_DESCRIPTOR descriptor;
  SECURITY_ATTRIBUTES attributes;
};

internal B32
ipc_win_security(IPC_WinSecurity *out)
{
  HANDLE token = 0;
  if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { return 0; }
  AlignType(8) U8 buffer[sizeof(TOKEN_USER)+SECURITY_MAX_SID_SIZE];
  DWORD needed = 0;
  B32 ok = GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &needed);
  CloseHandle(token);
  if(!ok || !CopySid(sizeof(out->sid), out->sid, ((TOKEN_USER *)buffer)->User.Sid)) { return 0; }
  if(!InitializeAcl((PACL)out->acl, sizeof(out->acl), ACL_REVISION) ||
     !AddAccessAllowedAce((PACL)out->acl, ACL_REVISION, FILE_ALL_ACCESS, out->sid) ||
     !InitializeSecurityDescriptor(&out->descriptor, SECURITY_DESCRIPTOR_REVISION) ||
     !SetSecurityDescriptorOwner(&out->descriptor, out->sid, FALSE) ||
     !SetSecurityDescriptorDacl(&out->descriptor, TRUE, (PACL)out->acl, FALSE) ||
     !SetSecurityDescriptorControl(&out->descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) { return 0; }
  out->attributes = (SECURITY_ATTRIBUTES){sizeof(SECURITY_ATTRIBUTES), &out->descriptor, FALSE};
  return 1;
}

internal B32
ipc_private_handle(HANDLE file, IPC_WinSecurity *security, B32 directory)
{
  BY_HANDLE_FILE_INFORMATION info = {0};
  if(GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) ||
     (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
     !!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != !!directory ||
     (!directory && info.nNumberOfLinks != 1)) { return 0; }
  PSID owner = 0;
  PACL acl = 0;
  PSECURITY_DESCRIPTOR descriptor = 0;
  DWORD code = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
                              &owner, 0, &acl, 0, &descriptor);
  B32 result = 0;
  if(code == ERROR_SUCCESS && owner != 0 && EqualSid(owner, security->sid) && acl != 0 && IsValidAcl(acl) && acl->AceCount == 1)
  {
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    ACCESS_ALLOWED_ACE *entry = 0;
    if(GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED) != 0 &&
       GetAce(acl, 0, (void **)&entry))
    {
      DWORD sid_size = GetLengthSid(security->sid);
      result = entry->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && entry->Header.AceFlags == 0 &&
               entry->Header.AceSize == OffsetOf(ACCESS_ALLOWED_ACE, SidStart)+sid_size &&
               entry->Mask == FILE_ALL_ACCESS && MemoryMatch(&entry->SidStart, security->sid, sid_size);
    }
  }
  if(descriptor != 0) { LocalFree(descriptor); }
  return result;
}

internal WCHAR *
ipc_win_path(Arena *arena, String8 path)
{
  if(path.size == 0 || path.size > 4*32760) { return 0; }
  for(U64 idx = 0; idx < path.size; idx += 1) { if(path.str[idx] == 0) { return 0; } }
  String16 wide = str16_from_8(arena, path);
  if(wide.size > 32760 || !str8_match(path, str8_from_16(arena, wide), 0)) { return 0; }
  return (WCHAR *)wide.str;
}

internal HANDLE
ipc_win_directory(WCHAR *path, B32 create, IPC_WinSecurity *security)
{
  if(path == 0) { return INVALID_HANDLE_VALUE; }
  if(create && !CreateDirectoryW(path, &security->attributes) && GetLastError() != ERROR_ALREADY_EXISTS) { return INVALID_HANDLE_VALUE; }
  HANDLE file = CreateFileW(path, FILE_READ_ATTRIBUTES|READ_CONTROL, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                            0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT, 0);
  if(file != INVALID_HANDLE_VALUE && !ipc_private_handle(file, security, 1)) { CloseHandle(file); file = INVALID_HANDLE_VALUE; }
  return file;
}

internal HANDLE
ipc_win_lock(WCHAR *path, IPC_WinSecurity *security)
{
  if(path == 0) { return INVALID_HANDLE_VALUE; }
  HANDLE file = CreateFileW(path, GENERIC_READ|GENERIC_WRITE|READ_CONTROL, FILE_SHARE_READ|FILE_SHARE_WRITE,
                            &security->attributes, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT, 0);
  if(file != INVALID_HANDLE_VALUE)
  {
    OVERLAPPED overlap = {0};
    if(!ipc_private_handle(file, security, 0) || !LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlap))
    {
      CloseHandle(file);
      file = INVALID_HANDLE_VALUE;
    }
  }
  return file;
}

internal void
ipc_win_unlock(HANDLE file)
{
  OVERLAPPED overlap = {0};
  UnlockFileEx(file, 0, 1, 0, &overlap);
  CloseHandle(file);
}

internal B32
ipc_win_record_read(HANDLE file, IPC_WinSecurity *security, U8 *bytes)
{
  LARGE_INTEGER size = {0};
  if(!ipc_private_handle(file, security, 0) || !GetFileSizeEx(file, &size) || size.QuadPart != IPC_RECORD_SIZE) { return 0; }
  for(DWORD pos = 0; pos < IPC_RECORD_SIZE;)
  {
    DWORD count = 0;
    if(!ReadFile(file, bytes+pos, IPC_RECORD_SIZE-pos, &count, 0) || count == 0) { return 0; }
    pos += count;
  }
  return 1;
}

internal B32
ipc_win_delete(HANDLE file)
{
  FILE_DISPOSITION_INFO disposition = {TRUE};
  return SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition));
}
#endif

#if OS_LINUX || OS_MAC
internal B32
ipc_private_fd(int fd, B32 directory)
{
  struct stat st = {0};
  if(fstat(fd, &st) != 0 || st.st_uid != geteuid() ||
     (st.st_mode & 07777) != (directory ? 0700 : 0600) ||
     (directory ? !S_ISDIR(st.st_mode) : (!S_ISREG(st.st_mode) || st.st_nlink != 1))) { return 0; }
#if OS_MAC
  acl_t acl = acl_get_fd_np(fd, ACL_TYPE_EXTENDED);
  if(acl == 0)
  {
    if(errno != ENOENT) { return 0; }
  }
  else
  {
    acl_entry_t entry;
    B32 empty = acl_valid(acl) == 0 && acl_get_entry(acl, ACL_FIRST_ENTRY, &entry) == -1 && errno == EINVAL;
    acl_free(acl);
    if(!empty) { return 0; }
  }
#endif
  return 1;
}

internal int
ipc_private_directory(String8 directory, B32 create)
{
  if(directory.size == 0 || directory.size >= 4096) { return -1; }
  for(U64 idx = 0; idx < directory.size; idx += 1) { if(directory.str[idx] == 0) { return -1; } }
  Temp scratch = scratch_begin(0, 0);
  char *path = (char *)push_cstr(scratch.arena, directory).str;
  int fd = -1;
  if(!create || mkdir(path, 0700) == 0 || errno == EEXIST)
  {
    fd = open(path, O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd != -1 && !ipc_private_fd(fd, 1)) { close(fd); fd = -1; }
  }
  scratch_end(scratch);
  return fd;
}

internal int
ipc_registration_lock(int directory)
{
  int fd = openat(directory, ".lock", O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK, 0600);
  if(fd != -1)
  {
    int code = -1;
    if(ipc_private_fd(fd, 0))
    {
      do { code = flock(fd, LOCK_EX); } while(code == -1 && errno == EINTR);
    }
    if(code != 0) { close(fd); fd = -1; }
  }
  return fd;
}

internal B32
ipc_record_read(int fd, U8 *bytes)
{
  struct stat st = {0};
  if(!ipc_private_fd(fd, 0) || fstat(fd, &st) != 0 || st.st_size != IPC_RECORD_SIZE) { return 0; }
  for(U64 pos = 0; pos < IPC_RECORD_SIZE;)
  {
    S64 count = read(fd, bytes+pos, IPC_RECORD_SIZE-pos);
    if(count > 0) { pos += (U64)count; }
    else if(count == -1 && errno == EINTR) { continue; }
    else { return 0; }
  }
  return 1;
}

internal B32
ipc_registration_unlink(int directory, char *name, int file)
{
  struct stat owned = {0}, named = {0};
  return fstat(file, &owned) == 0 && fstatat(directory, name, &named, AT_SYMLINK_NOFOLLOW) == 0 &&
         owned.st_dev == named.st_dev && owned.st_ino == named.st_ino && unlinkat(directory, name, 0) == 0;
}
#endif

internal void
ipc_registration_close(IPC_Registration *registration)
{
  if(registration != 0)
  {
#if OS_WINDOWS
    ipc_win_delete(registration->file);
    CloseHandle(registration->file);
#elif OS_LINUX || OS_MAC
    int lock = ipc_registration_lock(registration->directory);
    if(lock != -1)
    {
      ipc_registration_unlink(registration->directory, registration->name, registration->file);
      close(lock);
    }
    close(registration->file);
    close(registration->directory);
#endif
    free(registration);
  }
}

internal IPC_Server *
ipc_server_open_local(String8 directory, SOCK_WakeupFunctionType *wakeup)
{
  IPC_Server *server = 0;
#if OS_WINDOWS
  Temp scratch = scratch_begin(0, 0);
  IPC_WinSecurity security = {0};
  U32 pid = get_process_info()->pid;
  U256 birth = {0};
  if(!ipc_win_security(&security) || !ipc_process_birth(pid, &birth)) { scratch_end(scratch); return 0; }
  HANDLE dir = ipc_win_directory(ipc_win_path(scratch.arena, directory), 1, &security);
  if(dir == INVALID_HANDLE_VALUE) { scratch_end(scratch); return 0; }
  WCHAR *lock_path = ipc_win_path(scratch.arena, str8f(scratch.arena, "%S/.lock", directory));
  HANDLE lock = ipc_win_lock(lock_path, &security);
  if(lock != INVALID_HANDLE_VALUE)
  {
    WCHAR *name = ipc_win_path(scratch.arena, str8f(scratch.arena, "%S/%u.endpoint", directory, pid));
    HANDLE old = name != 0 ? CreateFileW(name, GENERIC_READ|READ_CONTROL|DELETE, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                        0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0) : INVALID_HANDLE_VALUE;
    B32 available = name != 0 && old == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND;
    if(old != INVALID_HANDLE_VALUE)
    {
      U8 previous[IPC_RECORD_SIZE];
      if(ipc_win_record_read(old, &security, previous) && ipc_record_valid(previous, pid) &&
         !MemoryMatch(previous+16, birth.u8, sizeof(birth.u8))) { available = ipc_win_delete(old); }
      CloseHandle(old);
    }
    IPC_Registration *registration = available ? calloc(1, sizeof(*registration)) : 0;
    if(registration != 0) { server = ipc_server_open_random(0, wakeup); }
    if(server != 0)
    {
      IPC_Endpoint endpoint = ipc_server_endpoint(server);
      U8 bytes[IPC_RECORD_SIZE];
      ipc_record_write(bytes, pid, birth, endpoint);
      String8 temporary_name = str8f(scratch.arena, "%S/.%u-%016llx%016llx.tmp", directory, pid,
                                     (unsigned long long)endpoint.instance_id.u64[0], (unsigned long long)endpoint.instance_id.u64[1]);
      WCHAR *temporary = ipc_win_path(scratch.arena, temporary_name);
      HANDLE file = temporary != 0 ? CreateFileW(temporary, GENERIC_READ|GENERIC_WRITE|READ_CONTROL|DELETE,
                                                FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, &security.attributes,
                                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT, 0) : INVALID_HANDLE_VALUE;
      B32 written = file != INVALID_HANDLE_VALUE;
      for(DWORD pos = 0; written && pos < sizeof(bytes);)
      {
        DWORD count = 0;
        written = WriteFile(file, bytes+pos, (DWORD)sizeof(bytes)-pos, &count, 0) && count != 0;
        pos += count;
      }
      B32 published = written && ipc_private_handle(file, &security, 0) && MoveFileExW(temporary, name, MOVEFILE_WRITE_THROUGH);
      if(published)
      {
        registration->file = file;
        server->registration = registration;
      }
      else
      {
        if(file != INVALID_HANDLE_VALUE) { ipc_win_delete(file); CloseHandle(file); }
        ipc_server_close(server);
        server = 0;
      }
    }
    if(server == 0) { free(registration); }
    ipc_win_unlock(lock);
  }
  CloseHandle(dir);
  scratch_end(scratch);
#elif OS_LINUX || OS_MAC
  int dir = ipc_private_directory(directory, 1);
  if(dir == -1) { return 0; }
  int lock = ipc_registration_lock(dir);
  if(lock == -1) { close(dir); return 0; }
  U32 pid = get_process_info()->pid;
  U256 birth = {0};
  IPC_Registration *registration = calloc(1, sizeof(*registration));
  if(registration != 0 && ipc_process_birth(pid, &birth))
  {
    snprintf(registration->name, sizeof(registration->name), "%u.endpoint", pid);
    U8 previous[IPC_RECORD_SIZE];
    int old = openat(dir, registration->name, O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    B32 available = old == -1 && errno == ENOENT;
    if(old != -1)
    {
      if(ipc_record_read(old, previous) && ipc_record_valid(previous, pid) &&
         !MemoryMatch(previous+16, birth.u8, sizeof(birth.u8)))
      {
        available = ipc_registration_unlink(dir, registration->name, old);
      }
      close(old);
    }
    if(available) { server = ipc_server_open_random(0, wakeup); }
    if(server != 0)
    {
      IPC_Endpoint endpoint = ipc_server_endpoint(server);
      U8 bytes[IPC_RECORD_SIZE];
      ipc_record_write(bytes, pid, birth, endpoint);
      char temporary[80];
      snprintf(temporary, sizeof(temporary), ".%u-%016llx%016llx.tmp", pid,
               (unsigned long long)endpoint.instance_id.u64[0], (unsigned long long)endpoint.instance_id.u64[1]);
      int fd = openat(dir, temporary, O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW, 0600);
      B32 written = fd != -1;
      for(U64 pos = 0; written && pos < sizeof(bytes);)
      {
        S64 count = write(fd, bytes+pos, sizeof(bytes)-pos);
        if(count > 0) { pos += (U64)count; }
        else if(count == -1 && errno == EINTR) { continue; }
        else { written = 0; }
      }
      B32 published = written && ipc_private_fd(fd, 0) && linkat(dir, temporary, dir, registration->name, 0) == 0;
      if(fd != -1)
      {
        if(unlinkat(dir, temporary, 0) != 0 && published)
        {
          ipc_registration_unlink(dir, registration->name, fd);
          published = 0;
        }
      }
      if(published)
      {
        registration->directory = dir;
        registration->file = fd;
        server->registration = registration;
      }
      else
      {
        if(fd != -1) { close(fd); }
        ipc_server_close(server);
        server = 0;
      }
    }
  }
  close(lock);
  if(server == 0) { free(registration); close(dir); }
#endif
  return server;
}

internal IPC_Response
ipc_request_local(Arena *arena, String8 directory, U32 pid, String8 data, U64 endt_us)
{
  IPC_Response result = {IPC_Status_Unavailable};
  if(data.size > IPC_PAYLOAD_LIMIT) { result.status = IPC_Status_TooLarge; return result; }
  if(now_time_us() >= endt_us) { result.status = IPC_Status_Timeout; return result; }
#if OS_WINDOWS
  if(pid == 0) { return result; }
  Temp scratch = scratch_begin(&arena, 1);
  IPC_WinSecurity security = {0};
  if(ipc_win_security(&security))
  {
    HANDLE dir = ipc_win_directory(ipc_win_path(scratch.arena, directory), 0, &security);
    if(dir != INVALID_HANDLE_VALUE)
    {
      WCHAR *name = ipc_win_path(scratch.arena, str8f(scratch.arena, "%S/%u.endpoint", directory, pid));
      HANDLE file = name != 0 ? CreateFileW(name, GENERIC_READ|READ_CONTROL, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                           0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0) : INVALID_HANDLE_VALUE;
      if(file != INVALID_HANDLE_VALUE)
      {
        U8 bytes[IPC_RECORD_SIZE];
        U256 birth = {0};
        if(ipc_win_record_read(file, &security, bytes) && ipc_record_valid(bytes, pid) && ipc_process_birth(pid, &birth) &&
           MemoryMatch(bytes+16, birth.u8, sizeof(birth.u8)))
        {
          IPC_Endpoint endpoint = {0};
          endpoint.address.kind = SOCK_EndpointKind_IPv4;
          endpoint.address.address_u8[0] = 127;
          endpoint.address.address_u8[3] = 1;
          endpoint.address.port = bytes[48] | (U16)bytes[49] << 8;
          MemoryCopy(endpoint.instance_id.u8, bytes+56, sizeof(endpoint.instance_id.u8));
          MemoryCopy(endpoint.auth_key.u8, bytes+72, sizeof(endpoint.auth_key.u8));
          CloseHandle(file);
          CloseHandle(dir);
          scratch_end(scratch);
          return ipc_request(arena, endpoint, data, endt_us);
        }
        CloseHandle(file);
      }
      CloseHandle(dir);
    }
  }
  scratch_end(scratch);
#elif OS_LINUX || OS_MAC
  if(pid == 0) { return result; }
  int dir = ipc_private_directory(directory, 0);
  if(dir == -1) { return result; }
  char name[32];
  snprintf(name, sizeof(name), "%u.endpoint", pid);
  int fd = openat(dir, name, O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
  close(dir);
  if(fd != -1)
  {
    U8 bytes[IPC_RECORD_SIZE];
    U256 birth = {0};
    if(ipc_record_read(fd, bytes) && ipc_record_valid(bytes, pid) && ipc_process_birth(pid, &birth) &&
       MemoryMatch(bytes+16, birth.u8, sizeof(birth.u8)))
    {
      IPC_Endpoint endpoint = {0};
      endpoint.address.kind = SOCK_EndpointKind_IPv4;
      endpoint.address.address_u8[0] = 127;
      endpoint.address.address_u8[3] = 1;
      endpoint.address.port = bytes[48] | (U16)bytes[49] << 8;
      MemoryCopy(endpoint.instance_id.u8, bytes+56, sizeof(endpoint.instance_id.u8));
      MemoryCopy(endpoint.auth_key.u8, bytes+72, sizeof(endpoint.auth_key.u8));
      close(fd);
      return ipc_request(arena, endpoint, data, endt_us);
    }
    close(fd);
  }
#endif
  return result;
}
