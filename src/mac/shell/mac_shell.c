// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Helpers

internal NSString *
mac_sh_ns_string_from_string8(Arena *arena, String8 string)
{
  String8 copy = push_str8_copy(arena, string);
  NSString *result = [[NSString alloc] initWithBytes:copy.str length:copy.size encoding:NSUTF8StringEncoding];
  return result;
}

////////////////////////////////
//~ rjf: @per_os_impl Shell Operations

internal void
sh_message(B32 error, String8 title, String8 message)
{
  Temp scratch = scratch_begin(0, 0);
  NSAlert *alert = [NSAlert new];
  [alert setAlertStyle:(error ? NSAlertStyleCritical : NSAlertStyleInformational)];
  [alert setMessageText:mac_sh_ns_string_from_string8(scratch.arena, title)];
  [alert setInformativeText:mac_sh_ns_string_from_string8(scratch.arena, message)];
  [alert runModal];
  scratch_end(scratch);
}

internal String8
sh_pick_file(Arena *arena, String8 title, String8 initial_path)
{
  Temp scratch = scratch_begin(&arena, 1);
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  [panel setCanChooseFiles:YES];
  [panel setCanChooseDirectories:NO];
  [panel setAllowsMultipleSelection:NO];
  if(title.size != 0)
  {
    [panel setTitle:mac_sh_ns_string_from_string8(scratch.arena, title)];
  }
  if(initial_path.size != 0)
  {
    NSString *ns_path = mac_sh_ns_string_from_string8(scratch.arena, initial_path);
    [panel setDirectoryURL:[NSURL fileURLWithPath:ns_path]];
  }
  String8 result = str8_zero();
  if([panel runModal] == NSModalResponseOK)
  {
    NSURL *url = [panel URL];
    NSString *path = [url path];
    char const *utf8 = [path UTF8String];
    if(utf8 != 0)
    {
      result = push_str8_copy(arena, str8_cstring((char *)utf8));
    }
  }
  scratch_end(scratch);
  return result;
}

internal void
sh_show_in_file_browser(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  NSString *ns_path = mac_sh_ns_string_from_string8(scratch.arena, path);
  NSURL *url = [NSURL fileURLWithPath:ns_path];
  [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[url]];
  scratch_end(scratch);
}

internal void
sh_open_in_browser(String8 url)
{
  Temp scratch = scratch_begin(0, 0);
  NSString *ns_url = mac_sh_ns_string_from_string8(scratch.arena, url);
  [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:ns_url]];
  scratch_end(scratch);
}

internal B32
sh_install_or_uninstall_self(B32 write, B32 install)
{
  // The .app bundle is the macOS installation unit; moving it is user-managed.
  (void)write;
  (void)install;
  return 0;
}
