// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

@implementation MAC_WM_WindowDelegate
- (BOOL)windowShouldClose:(id)sender
{
  if(window != 0)
  {
    window->close_requested = 1;
  }
  return NO;
}
@end

internal WM_Window
mac_wm_handle_from_window(MAC_WM_Window *window)
{
  WM_Window result = {(U64)window};
  return result;
}

internal MAC_WM_Window *
mac_wm_window_from_handle(WM_Window handle)
{
  MAC_WM_Window *result = (MAC_WM_Window *)handle.u64[0];
  return result;
}

internal Rng2F32
mac_wm_rect_from_ns_rect(NSRect rect)
{
  Rng2F32 result = r2f32p((F32)rect.origin.x,
                          (F32)rect.origin.y,
                          (F32)(rect.origin.x + rect.size.width),
                          (F32)(rect.origin.y + rect.size.height));
  return result;
}

internal NSString *
mac_wm_ns_string_from_string8(Arena *arena, String8 string)
{
  String8 copy = push_str8_copy(arena, string);
  NSString *result = [[NSString alloc] initWithBytes:copy.str length:copy.size encoding:NSUTF8StringEncoding];
  return result;
}

internal MAC_WM_Window *
mac_wm_window_alloc(void)
{
  MAC_WM_Window *result = mac_wm_state->free_window;
  if(result != 0)
  {
    SLLStackPop(mac_wm_state->free_window);
  }
  else
  {
    result = push_array_no_zero(mac_wm_state->arena, MAC_WM_Window, 1);
  }
  MemoryZeroStruct(result);
  DLLPushBack(mac_wm_state->first_window, mac_wm_state->last_window, result);
  return result;
}

internal void
mac_wm_window_release(MAC_WM_Window *window)
{
  DLLRemove(mac_wm_state->first_window, mac_wm_state->last_window, window);
  [window->ns_window setDelegate:0];
  [window->ns_window close];
  window->ns_window = 0;
  window->delegate = 0;
  SLLStackPush(mac_wm_state->free_window, window);
}

////////////////////////////////
//~ @os_hooks Main Initialization API (Implemented Per-OS)

internal void
wm_init(void)
{
  Arena *arena = arena_alloc();
  mac_wm_state = push_array(arena, MAC_WM_State, 1);
  mac_wm_state->arena = arena;
  mac_wm_state->gfx_info.double_click_time = 0.5f;
  mac_wm_state->gfx_info.caret_blink_time = 0.5f;
  mac_wm_state->gfx_info.default_refresh_rate = 60.f;

  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [NSApp finishLaunching];
}

////////////////////////////////
//~ @os_hooks Graphics System Info (Implemented Per-OS)

internal WM_SystemInfo *
wm_get_system_info(void)
{
  return &mac_wm_state->gfx_info;
}

////////////////////////////////
//~ @os_hooks Clipboards (Implemented Per-OS)

internal void
wm_set_clipboard_text(String8 string)
{
  Temp scratch = scratch_begin(0, 0);
  NSString *ns_string = mac_wm_ns_string_from_string8(scratch.arena, string);
  NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
  [pasteboard clearContents];
  [pasteboard setString:ns_string forType:NSPasteboardTypeString];
  scratch_end(scratch);
}

internal String8
wm_get_clipboard_text(Arena *arena)
{
  NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
  NSString *ns_string = [pasteboard stringForType:NSPasteboardTypeString];
  String8 result = {0};
  if(ns_string != 0)
  {
    char const *utf8 = [ns_string UTF8String];
    result = push_str8_copy(arena, str8_cstring((char *)utf8));
  }
  return result;
}

////////////////////////////////
//~ @os_hooks Windows (Implemented Per-OS)

internal WM_Window
wm_window_open(Rng2F32 rect, WM_WindowFlags flags, String8 title)
{
  MAC_WM_Window *window = mac_wm_window_alloc();
  Vec2F32 dim = dim_2f32(rect);
  NSRect ns_rect = NSMakeRect(rect.x0, rect.y0, dim.x, dim.y);
  NSUInteger style_mask = (NSWindowStyleMaskTitled |
                           NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable |
                           NSWindowStyleMaskResizable);
  window->ns_window = [[NSWindow alloc] initWithContentRect:ns_rect
                                                   styleMask:style_mask
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
  window->delegate = [MAC_WM_WindowDelegate new];
  window->delegate->window = window;
  [window->ns_window setDelegate:window->delegate];
  [window->ns_window setReleasedWhenClosed:NO];
  [window->ns_window setAcceptsMouseMovedEvents:YES];
  wm_window_set_title(mac_wm_handle_from_window(window), title);
  return mac_wm_handle_from_window(window);
}

internal void
wm_window_close(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0)
  {
    mac_wm_window_release(window);
  }
}

internal void
wm_window_set_title(WM_Window handle, String8 title)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0)
  {
    Temp scratch = scratch_begin(0, 0);
    NSString *ns_title = mac_wm_ns_string_from_string8(scratch.arena, title);
    [window->ns_window setTitle:ns_title];
    scratch_end(scratch);
  }
}

internal void
wm_window_first_paint(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0)
  {
    [window->ns_window makeKeyAndOrderFront:0];
    [NSApp activateIgnoringOtherApps:YES];
  }
}

internal void
wm_window_focus(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0)
  {
    [window->ns_window makeKeyAndOrderFront:0];
  }
}

internal B32
wm_window_is_focused(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  B32 result = (window != 0 && [window->ns_window isKeyWindow]);
  return result;
}

internal B32
wm_window_is_fullscreen(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  B32 result = 0;
  if(window != 0)
  {
    result = !!([window->ns_window styleMask] & NSWindowStyleMaskFullScreen);
  }
  return result;
}

internal void
wm_window_set_fullscreen(WM_Window handle, B32 fullscreen)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0 && wm_window_is_fullscreen(handle) != fullscreen)
  {
    [window->ns_window toggleFullScreen:0];
  }
}

internal B32
wm_window_is_maximized(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  B32 result = (window != 0 && [window->ns_window isZoomed]);
  return result;
}

internal void
wm_window_set_maximized(WM_Window handle, B32 maximized)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0 && [window->ns_window isZoomed] != maximized)
  {
    [window->ns_window zoom:0];
  }
}

internal B32
wm_window_is_minimized(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  B32 result = (window != 0 && [window->ns_window isMiniaturized]);
  return result;
}

internal void
wm_window_set_minimized(WM_Window handle, B32 minimized)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0)
  {
    if(minimized) { [window->ns_window miniaturize:0]; }
    else { [window->ns_window deminiaturize:0]; }
  }
}

internal void
wm_window_bring_to_front(WM_Window handle)
{
  wm_window_focus(handle);
}

internal void
wm_window_set_monitor(WM_Window handle, WM_Monitor monitor)
{
}

internal void
wm_window_clear_custom_border_data(WM_Window handle)
{
}

internal void
wm_window_push_custom_title_bar(WM_Window handle, F32 thickness)
{
}

internal void
wm_window_push_custom_edges(WM_Window handle, F32 thickness)
{
}

internal void
wm_window_push_custom_title_bar_client_area(WM_Window handle, Rng2F32 rect)
{
}

internal Rng2F32
wm_rect_from_window(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  Rng2F32 result = {0};
  if(window != 0)
  {
    result = mac_wm_rect_from_ns_rect([window->ns_window frame]);
  }
  return result;
}

internal Rng2F32
wm_client_rect_from_window(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  Rng2F32 result = {0};
  if(window != 0)
  {
    result = mac_wm_rect_from_ns_rect([[window->ns_window contentView] bounds]);
  }
  return result;
}

internal F32
wm_dpi_from_window(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  F32 result = 96.f;
  if(window != 0)
  {
    result = 96.f * (F32)[window->ns_window backingScaleFactor];
  }
  return result;
}

////////////////////////////////
//~ @os_hooks External Windows (Implemented Per-OS)

internal WM_ExtWindow
wm_focused_external_window(void)
{
  WM_ExtWindow result = {0};
  return result;
}

internal void
wm_focus_external_window(WM_ExtWindow handle)
{
}

////////////////////////////////
//~ @os_hooks Monitors (Implemented Per-OS)

internal WM_MonitorArray
wm_push_monitors_array(Arena *arena)
{
  NSArray<NSScreen *> *screens = [NSScreen screens];
  WM_MonitorArray result = {0};
  result.count = (U64)[screens count];
  result.v = push_array(arena, WM_Monitor, result.count);
  for(U64 idx = 0; idx < result.count; idx += 1)
  {
    result.v[idx].u64[0] = (U64)[screens objectAtIndex:idx];
  }
  return result;
}

internal WM_Monitor
wm_primary_monitor(void)
{
  WM_Monitor result = {(U64)[NSScreen mainScreen]};
  return result;
}

internal WM_Monitor
wm_monitor_from_window(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  WM_Monitor result = {0};
  if(window != 0)
  {
    result.u64[0] = (U64)[window->ns_window screen];
  }
  return result;
}

internal String8
wm_name_from_monitor(Arena *arena, WM_Monitor monitor)
{
  NSScreen *screen = (NSScreen *)monitor.u64[0];
  NSString *name = screen.localizedName;
  char const *utf8 = [name UTF8String];
  String8 result = push_str8_copy(arena, str8_cstring((char *)utf8));
  return result;
}

internal Vec2F32
wm_dim_from_monitor(WM_Monitor monitor)
{
  NSScreen *screen = (NSScreen *)monitor.u64[0];
  NSRect frame = screen.frame;
  Vec2F32 result = v2f32(frame.size.width, frame.size.height);
  return result;
}

internal F32
wm_dpi_from_monitor(WM_Monitor monitor)
{
  NSScreen *screen = (NSScreen *)monitor.u64[0];
  F32 result = 96.f * (F32)screen.backingScaleFactor;
  return result;
}

////////////////////////////////
//~ @os_hooks Events (Implemented Per-OS)

internal void
wm_send_wakeup_event(void)
{
  NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                      location:NSZeroPoint
                                 modifierFlags:0
                                     timestamp:0
                                  windowNumber:0
                                       context:0
                                       subtype:0
                                         data1:0
                                         data2:0];
  [NSApp postEvent:event atStart:NO];
}

internal WM_EventList
wm_get_events(Arena *arena, B32 wait)
{
  WM_EventList result = {0};
  NSDate *limit = wait ? [NSDate distantFuture] : [NSDate distantPast];
  for(;;)
  {
    NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:limit
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
    if(event == 0)
    {
      break;
    }
    [NSApp sendEvent:event];
    limit = [NSDate distantPast];
  }
  for(MAC_WM_Window *window = mac_wm_state->first_window; window != 0; window = window->next)
  {
    if(window->close_requested)
    {
      window->close_requested = 0;
      WM_Event *event = wm_event_list_push_new(arena, &result, WM_EventKind_WindowClose);
      event->window = mac_wm_handle_from_window(window);
    }
  }
  return result;
}

internal WM_Modifiers
wm_get_modifiers(void)
{
  NSEventModifierFlags flags = [NSEvent modifierFlags];
  WM_Modifiers result = 0;
  if(flags & NSEventModifierFlagControl) { result |= WM_Modifier_Ctrl; }
  if(flags & NSEventModifierFlagShift) { result |= WM_Modifier_Shift; }
  if(flags & NSEventModifierFlagOption) { result |= WM_Modifier_Alt; }
  return result;
}

internal B32
wm_key_is_down(WM_Key key)
{
  return 0;
}

internal Vec2F32
wm_mouse_from_window(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  Vec2F32 result = {0};
  if(window != 0)
  {
    NSPoint point = [window->ns_window mouseLocationOutsideOfEventStream];
    result = v2f32(point.x, point.y);
  }
  return result;
}

////////////////////////////////
//~ @os_hooks Cursors (Implemented Per-OS)

internal void
wm_set_cursor(WM_Cursor cursor)
{
}

////////////////////////////////
//~ @os_hooks Native User-Facing Graphical Messages (Implemented Per-OS)

internal void
wm_graphical_message(B32 error, String8 title, String8 message)
{
  Temp scratch = scratch_begin(0, 0);
  NSAlert *alert = [NSAlert new];
  [alert setMessageText:mac_wm_ns_string_from_string8(scratch.arena, title)];
  [alert setInformativeText:mac_wm_ns_string_from_string8(scratch.arena, message)];
  [alert runModal];
  scratch_end(scratch);
}

internal String8
wm_graphical_pick_file(Arena *arena, String8 initial_path)
{
  return str8_zero();
}

////////////////////////////////
//~ @os_hooks Shell Operations

internal void
wm_show_in_filesystem_ui(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  NSString *ns_path = mac_wm_ns_string_from_string8(scratch.arena, path);
  NSURL *url = [NSURL fileURLWithPath:ns_path];
  [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[url]];
  scratch_end(scratch);
}

internal void
wm_open_in_browser(String8 url)
{
  Temp scratch = scratch_begin(0, 0);
  NSString *ns_url = mac_wm_ns_string_from_string8(scratch.arena, url);
  [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:ns_url]];
  scratch_end(scratch);
}
