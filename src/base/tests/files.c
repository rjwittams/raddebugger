// Copyright (c) 2026 Robert Wittams
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BUILD_CONSOLE_INTERFACE 1
#define NEED_ASYNC 0
#include "base/base_inc.h"
#include "base/base_inc.c"

#define Check(c) do { if(!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); abort_self(1); } } while(0)

internal void
test_create_truncate(void)
{
  String8 path = str8_lit("file.bin");
  File file = file_open(AccessFlag_Write, path);
  Check(!file_match(file, file_zero()));
  Check(properties_from_file(file).size == 0);
  Check(file_write(file, r1u64(0, 21), "long original payload") == 21);
  file_close(file);

  file = file_open(AccessFlag_Write, path);
  Check(!file_match(file, file_zero()));
  Check(properties_from_file(file).size == 0);
  Check(file_write(file, r1u64(0, 3), "new") == 3);
  Check(properties_from_file(file).size == 3);
  file_close(file);

  Temp scratch = scratch_begin(0, 0);
  Check(str8_match(data_from_file_path(scratch.arena, path), str8_lit("new"), 0));
  scratch_end(scratch);
}

internal void
test_append(void)
{
  String8 path = str8_lit("file.bin");
  Check(append_data_to_file_path(path, str8_lit("abc")));
  Check(append_data_to_file_path(path, str8_lit("de")));
  Temp scratch = scratch_begin(0, 0);
  Check(str8_match(data_from_file_path(scratch.arena, path), str8_lit("abcde"), 0));
  scratch_end(scratch);
}

internal void
test_short_read_and_eof(void)
{
  String8 path = str8_lit("file.bin");
  Check(write_data_to_file_path(path, str8_lit("abc")));
  File file = file_open(AccessFlag_Read, path);
  Check(!file_match(file, file_zero()));
  U8 buffer[8];
  MemorySet(buffer, 0xa5, sizeof(buffer));
  Check(file_read(file, r1u64(0, 8), buffer) == 3);
  Check(MemoryMatch(buffer, "abc", 3));
  for(U64 i = 3; i < sizeof(buffer); i += 1)
  {
    Check(buffer[i] == 0xa5);
  }
  MemorySet(buffer, 0xa5, sizeof(buffer));
  Check(file_read(file, r1u64(3, 8), buffer) == 0);
  Check(file_read(file, r1u64(10, 18), buffer) == 0);
  Check(file_read(file, r1u64(0, 0), buffer) == 0);
  for(U64 i = 0; i < sizeof(buffer); i += 1)
  {
    Check(buffer[i] == 0xa5);
  }
  file_close(file);
}

internal void
check_empty_iteration(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  FileIter *iter = file_iter_begin(scratch.arena, path, 0);
  Check(iter != 0);
  FileInfo info = {0};
  Check(!file_iter_next(scratch.arena, iter, &info));
  file_iter_end(iter);
  scratch_end(scratch);
}

internal void
test_directory(void)
{
  String8 path = str8_lit("directory");
  Check(make_directory(path));
  check_empty_iteration(path);
  Check(write_data_to_file_path(str8_lit("directory/item.bin"), str8_lit("abc")));
  Temp scratch = scratch_begin(0, 0);
  FileIter *iter = file_iter_begin(scratch.arena, path, 0);
  Check(iter != 0);
  FileInfo info = {0};
  Check(file_iter_next(scratch.arena, iter, &info));
  Check(str8_match(info.name, str8_lit("item.bin"), 0));
  Check(info.props.size == 3);
  Check(!file_iter_next(scratch.arena, iter, &info));
  file_iter_end(iter);
  scratch_end(scratch);
}

internal void
entry_point(CmdLine *cmdline)
{
  String8 selected = cmd_line_string(cmdline, str8_lit("case"));
  if(str8_match(selected, str8_lit("create_truncate"), 0))
  {
    test_create_truncate();
  }
  else if(str8_match(selected, str8_lit("append"), 0))
  {
    test_append();
  }
  else if(str8_match(selected, str8_lit("short_read_and_eof"), 0))
  {
    test_short_read_and_eof();
  }
  else if(str8_match(selected, str8_lit("missing_directory"), 0))
  {
    check_empty_iteration(str8_lit("missing"));
  }
  else if(str8_match(selected, str8_lit("non_directory"), 0))
  {
    Check(write_data_to_file_path(str8_lit("file.bin"), str8_lit("abc")));
    check_empty_iteration(str8_lit("file.bin"));
  }
  else if(str8_match(selected, str8_lit("directory"), 0))
  {
    test_directory();
  }
  else
  {
    Check(!"unknown file-API case");
  }
  fprintf(stdout, "PASS %.*s\n", (int)selected.size, selected.str);
}
