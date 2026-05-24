// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

@implementation MAC_WM_WindowDelegate
- (void)macLiveResizeStopDisplayLink
{
  if(live_resize_display_link != 0)
  {
    [live_resize_display_link invalidate];
    live_resize_display_link = 0;
  }
}
- (void)macLiveResizeTick:(CADisplayLink *)display_link
{
  if(window != 0 && !live_resize_update_in_progress)
  {
    live_resize_update_in_progress = 1;
    update();
    live_resize_update_in_progress = 0;
  }
}
- (void)windowWillStartLiveResize:(NSNotification *)notification
{
  if(window != 0 && live_resize_display_link == 0)
  {
    live_resize_display_link = [window->ns_window displayLinkWithTarget:self selector:@selector(macLiveResizeTick:)];
    [live_resize_display_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
  }
}
- (void)windowDidResize:(NSNotification *)notification
{
  if(window != 0 && ![window->ns_window inLiveResize])
  {
    wm_send_wakeup_event();
  }
}
- (void)windowDidEndLiveResize:(NSNotification *)notification
{
  [self macLiveResizeStopDisplayLink];
  if(window != 0)
  {
    update();
  }
}
- (BOOL)windowShouldClose:(id)sender
{
  if(window != 0)
  {
    window->close_requested = 1;
  }
  return NO;
}
- (void)windowDidResignKey:(NSNotification *)notification
{
  if(window != 0)
  {
    window->lose_focus_requested = 1;
  }
}
- (void)dealloc
{
  [self macLiveResizeStopDisplayLink];
  [super dealloc];
}
@end

@implementation MAC_WM_MenuTarget
- (void)menuItemSelected:(id)sender
{
  NSString *command = [sender representedObject];
  if(command != 0)
  {
    char const *utf8 = [command UTF8String];
    mac_wm_push_menu_command(str8_cstring((char *)utf8));
  }
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

internal MAC_WM_Window *
mac_wm_window_from_ns_window(NSWindow *ns_window)
{
  MAC_WM_Window *result = 0;
  if(ns_window == 0)
  {
    ns_window = [NSApp keyWindow];
  }
  for(MAC_WM_Window *window = mac_wm_state->first_window; window != 0; window = window->next)
  {
    if(window->ns_window == ns_window)
    {
      result = window;
      break;
    }
  }
  return result;
}

internal MAC_WM_ChromeMode
mac_wm_chrome_mode_from_environment(void)
{
  MAC_WM_ChromeMode result = MAC_WM_ChromeMode_Integrated;
  char *env = getenv("RADDBG_MAC_CHROME");
  if(env != 0)
  {
    String8 string = str8_cstring(env);
    if(str8_match(string, str8_lit("native"), 0))
    {
      result = MAC_WM_ChromeMode_Native;
    }
    else if(str8_match(string, str8_lit("custom"), 0))
    {
      result = MAC_WM_ChromeMode_Custom;
    }
    else if(str8_match(string, str8_lit("integrated"), 0))
    {
      result = MAC_WM_ChromeMode_Integrated;
    }
  }
  return result;
}

internal MAC_WM_MenuMode
mac_wm_menu_mode_from_environment(void)
{
  MAC_WM_MenuMode result = MAC_WM_MenuMode_Self;
  char *env = getenv("RADDBG_MAC_MENU");
  if(env != 0)
  {
    String8 string = str8_cstring(env);
    if(str8_match(string, str8_lit("native"), 0))
    {
      result = MAC_WM_MenuMode_Native;
    }
    else if(str8_match(string, str8_lit("self"), 0))
    {
      result = MAC_WM_MenuMode_Self;
    }
  }
  return result;
}

internal B32
mac_wm_chrome_mode_has_native_controls(MAC_WM_ChromeMode mode)
{
  B32 result = (mode == MAC_WM_ChromeMode_Integrated ||
                mode == MAC_WM_ChromeMode_Native);
  return result;
}

internal void
mac_wm_push_menu_command(String8 command_name)
{
  if(mac_wm_state != 0)
  {
    MAC_WM_MenuCommandNode *node = mac_wm_state->free_menu_command_node;
    if(node != 0)
    {
      SLLStackPop(mac_wm_state->free_menu_command_node);
      MemoryZeroStruct(node);
    }
    else
    {
      node = push_array(mac_wm_state->arena, MAC_WM_MenuCommandNode, 1);
    }
    node->command_name = push_str8_copy(mac_wm_state->arena, command_name);
    node->window = mac_wm_handle_from_window(mac_wm_window_from_ns_window([NSApp keyWindow]));
    SLLQueuePush(mac_wm_state->first_pending_menu_command, mac_wm_state->last_pending_menu_command, node);
    wm_send_wakeup_event();
  }
}

internal B32
mac_wm_window_pos_is_native_title_bar_control_area(MAC_WM_Window *window, Vec2F32 pos)
{
  B32 result = 0;
  if(window != 0 &&
     window->custom_border &&
     window->chrome_mode == MAC_WM_ChromeMode_Integrated &&
     !wm_window_is_fullscreen(mac_wm_handle_from_window(window)) &&
     0 <= pos.y && pos.y <= window->custom_border_title_thickness)
  {
    F32 width = 88.f;
    NSButton *close_button = [window->ns_window standardWindowButton:NSWindowCloseButton];
    NSButton *zoom_button = [window->ns_window standardWindowButton:NSWindowZoomButton];
    if(close_button != 0 && zoom_button != 0)
    {
      width = Max(width, (F32)Max(NSMaxX([close_button frame]), NSMaxX([zoom_button frame])) + 16.f);
    }
    result = (0 <= pos.x && pos.x < width);
  }
  return result;
}

internal B32
mac_wm_window_pos_is_title_bar_client_area(MAC_WM_Window *window, Vec2F32 pos)
{
  B32 result = 0;
  if(window != 0)
  {
    for(U64 idx = 0; idx < window->title_bar_client_area_count; idx += 1)
    {
      Rng2F32 rect = window->title_bar_client_areas[idx];
      if(rect.x0 <= pos.x && pos.x < rect.x1 &&
         rect.y0 <= pos.y && pos.y < rect.y1)
      {
        result = 1;
        break;
      }
    }
  }
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

internal Vec2F32
mac_wm_client_pos_from_ns_point(MAC_WM_Window *window, NSPoint point)
{
  Vec2F32 result = {0};
  if(window != 0)
  {
    NSView *content_view = [window->ns_window contentView];
    NSRect bounds = [content_view bounds];
    result = v2f32((F32)point.x, (F32)(bounds.size.height - point.y));
  }
  return result;
}

internal NSString *
mac_wm_ns_string_from_string8(Arena *arena, String8 string)
{
  String8 copy = push_str8_copy(arena, string);
  NSString *result = [[NSString alloc] initWithBytes:copy.str length:copy.size encoding:NSUTF8StringEncoding];
  return result;
}

internal WM_Modifiers
mac_wm_modifiers_from_ns_flags(NSEventModifierFlags flags)
{
  WM_Modifiers result = 0;
  if(flags & (NSEventModifierFlagControl|NSEventModifierFlagCommand)) { result |= WM_Modifier_Ctrl; }
  if(flags & NSEventModifierFlagShift) { result |= WM_Modifier_Shift; }
  if(flags & NSEventModifierFlagOption) { result |= WM_Modifier_Alt; }
  return result;
}

internal WM_Key
mac_wm_key_from_key_code(unsigned short key_code, B32 *right_sided_out)
{
  WM_Key result = WM_Key_Null;
  B32 right_sided = 0;
  switch(key_code)
  {
    default:{}break;
    case 0:{result = WM_Key_A;}break;
    case 1:{result = WM_Key_S;}break;
    case 2:{result = WM_Key_D;}break;
    case 3:{result = WM_Key_F;}break;
    case 4:{result = WM_Key_H;}break;
    case 5:{result = WM_Key_G;}break;
    case 6:{result = WM_Key_Z;}break;
    case 7:{result = WM_Key_X;}break;
    case 8:{result = WM_Key_C;}break;
    case 9:{result = WM_Key_V;}break;
    case 11:{result = WM_Key_B;}break;
    case 12:{result = WM_Key_Q;}break;
    case 13:{result = WM_Key_W;}break;
    case 14:{result = WM_Key_E;}break;
    case 15:{result = WM_Key_R;}break;
    case 16:{result = WM_Key_Y;}break;
    case 17:{result = WM_Key_T;}break;
    case 18:{result = WM_Key_1;}break;
    case 19:{result = WM_Key_2;}break;
    case 20:{result = WM_Key_3;}break;
    case 21:{result = WM_Key_4;}break;
    case 22:{result = WM_Key_6;}break;
    case 23:{result = WM_Key_5;}break;
    case 24:{result = WM_Key_Equal;}break;
    case 25:{result = WM_Key_9;}break;
    case 26:{result = WM_Key_7;}break;
    case 27:{result = WM_Key_Minus;}break;
    case 28:{result = WM_Key_8;}break;
    case 29:{result = WM_Key_0;}break;
    case 30:{result = WM_Key_RightBracket;}break;
    case 31:{result = WM_Key_O;}break;
    case 32:{result = WM_Key_U;}break;
    case 33:{result = WM_Key_LeftBracket;}break;
    case 34:{result = WM_Key_I;}break;
    case 35:{result = WM_Key_P;}break;
    case 36:{result = WM_Key_Return;}break;
    case 37:{result = WM_Key_L;}break;
    case 38:{result = WM_Key_J;}break;
    case 39:{result = WM_Key_Quote;}break;
    case 40:{result = WM_Key_K;}break;
    case 41:{result = WM_Key_Semicolon;}break;
    case 42:{result = WM_Key_BackSlash;}break;
    case 43:{result = WM_Key_Comma;}break;
    case 44:{result = WM_Key_Slash;}break;
    case 45:{result = WM_Key_N;}break;
    case 46:{result = WM_Key_M;}break;
    case 47:{result = WM_Key_Period;}break;
    case 48:{result = WM_Key_Tab;}break;
    case 49:{result = WM_Key_Space;}break;
    case 50:{result = WM_Key_Tick;}break;
    case 51:{result = WM_Key_Backspace;}break;
    case 53:{result = WM_Key_Esc;}break;
    case 54:{result = WM_Key_Ctrl; right_sided = 1;}break;
    case 55:{result = WM_Key_Ctrl;}break;
    case 56:{result = WM_Key_Shift;}break;
    case 57:{result = WM_Key_CapsLock;}break;
    case 58:{result = WM_Key_Alt;}break;
    case 59:{result = WM_Key_Ctrl;}break;
    case 60:{result = WM_Key_Shift; right_sided = 1;}break;
    case 61:{result = WM_Key_Alt; right_sided = 1;}break;
    case 62:{result = WM_Key_Ctrl; right_sided = 1;}break;
    case 65:{result = WM_Key_NumPeriod;}break;
    case 67:{result = WM_Key_NumStar;}break;
    case 69:{result = WM_Key_NumPlus;}break;
    case 71:{result = WM_Key_NumLock;}break;
    case 75:{result = WM_Key_NumSlash;}break;
    case 76:{result = WM_Key_Return;}break;
    case 78:{result = WM_Key_NumMinus;}break;
    case 82:{result = WM_Key_Num0;}break;
    case 83:{result = WM_Key_Num1;}break;
    case 84:{result = WM_Key_Num2;}break;
    case 85:{result = WM_Key_Num3;}break;
    case 86:{result = WM_Key_Num4;}break;
    case 87:{result = WM_Key_Num5;}break;
    case 88:{result = WM_Key_Num6;}break;
    case 89:{result = WM_Key_Num7;}break;
    case 91:{result = WM_Key_Num8;}break;
    case 92:{result = WM_Key_Num9;}break;
    case 96:{result = WM_Key_F5;}break;
    case 97:{result = WM_Key_F6;}break;
    case 98:{result = WM_Key_F7;}break;
    case 99:{result = WM_Key_F3;}break;
    case 100:{result = WM_Key_F8;}break;
    case 101:{result = WM_Key_F9;}break;
    case 103:{result = WM_Key_F11;}break;
    case 105:{result = WM_Key_F13;}break;
    case 106:{result = WM_Key_F16;}break;
    case 107:{result = WM_Key_F14;}break;
    case 109:{result = WM_Key_F10;}break;
    case 111:{result = WM_Key_F12;}break;
    case 113:{result = WM_Key_F15;}break;
    case 114:{result = WM_Key_Insert;}break;
    case 115:{result = WM_Key_Home;}break;
    case 116:{result = WM_Key_PageUp;}break;
    case 117:{result = WM_Key_Delete;}break;
    case 118:{result = WM_Key_F4;}break;
    case 119:{result = WM_Key_End;}break;
    case 120:{result = WM_Key_F2;}break;
    case 121:{result = WM_Key_PageDown;}break;
    case 122:{result = WM_Key_F1;}break;
    case 123:{result = WM_Key_Left;}break;
    case 124:{result = WM_Key_Right;}break;
    case 125:{result = WM_Key_Down;}break;
    case 126:{result = WM_Key_Up;}break;
  }
  if(right_sided_out != 0)
  {
    *right_sided_out = right_sided;
  }
  return result;
}

internal void
mac_wm_push_text_events_from_ns_string(Arena *arena, WM_EventList *events, MAC_WM_Window *window, NSString *string)
{
  if(window != 0 && string != 0)
  {
    char const *utf8 = [string UTF8String];
    if(utf8 != 0)
    {
      String8 text = str8_cstring((char *)utf8);
      for(U64 off = 0; off < text.size;)
      {
        UnicodeDecode decode = utf8_decode(text.str + off, text.size - off);
        if(decode.inc == 0)
        {
          break;
        }
        if(decode.codepoint >= 32 &&
           decode.codepoint != 127 &&
           !(0xf700 <= decode.codepoint && decode.codepoint <= 0xf8ff))
        {
          WM_Event *event = wm_event_list_push_new(arena, events, WM_EventKind_Text);
          event->window = mac_wm_handle_from_window(window);
          event->modifiers = wm_get_modifiers();
          event->character = decode.codepoint;
        }
        off += decode.inc;
      }
    }
  }
}

internal WM_Event *
mac_wm_push_event(Arena *arena, WM_EventList *events, WM_EventKind kind, MAC_WM_Window *window)
{
  WM_Event *event = wm_event_list_push_new(arena, events, kind);
  event->window = mac_wm_handle_from_window(window);
  event->modifiers = wm_get_modifiers();
  return event;
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
  [window->delegate macLiveResizeStopDisplayLink];
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
  mac_wm_state->chrome_mode = mac_wm_chrome_mode_from_environment();
  mac_wm_state->menu_mode = mac_wm_menu_mode_from_environment();
  mac_wm_state->menu_target = [MAC_WM_MenuTarget new];

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
  B32 custom_border = !!(flags & WM_WindowFlag_CustomBorder);
  MAC_WM_ChromeMode chrome_mode = mac_wm_state->chrome_mode;
  NSUInteger style_mask = (NSWindowStyleMaskTitled |
                           NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable |
                           NSWindowStyleMaskResizable);
  if(custom_border && chrome_mode != MAC_WM_ChromeMode_Native)
  {
    style_mask |= NSWindowStyleMaskFullSizeContentView;
  }
  window->ns_window = [[NSWindow alloc] initWithContentRect:ns_rect
                                                   styleMask:style_mask
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
  window->chrome_mode = chrome_mode;
  window->custom_border = custom_border;
  window->delegate = [MAC_WM_WindowDelegate new];
  window->delegate->window = window;
  [window->ns_window setDelegate:window->delegate];
  [window->ns_window setReleasedWhenClosed:NO];
  [window->ns_window setAcceptsMouseMovedEvents:YES];
  if(custom_border && chrome_mode != MAC_WM_ChromeMode_Native)
  {
    [window->ns_window setTitleVisibility:NSWindowTitleHidden];
    [window->ns_window setTitlebarAppearsTransparent:YES];
  }
  if(custom_border && chrome_mode == MAC_WM_ChromeMode_Custom)
  {
    [[window->ns_window standardWindowButton:NSWindowCloseButton] setHidden:YES];
    [[window->ns_window standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
    [[window->ns_window standardWindowButton:NSWindowZoomButton] setHidden:YES];
  }
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
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  NSScreen *screen = (NSScreen *)monitor.u64[0];
  if(window != 0 && screen != 0)
  {
    Rng2F32 existing_rect = wm_rect_from_window(handle);
    Vec2F32 window_size = dim_2f32(existing_rect);
    NSRect work_area = [screen visibleFrame];
    NSRect frame = NSMakeRect(work_area.origin.x + work_area.size.width/2 - window_size.x/2,
                              work_area.origin.y + work_area.size.height/2 - window_size.y/2,
                              window_size.x,
                              window_size.y);
    [window->ns_window setFrame:frame display:YES];
  }
}

internal void
wm_window_clear_custom_border_data(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0)
  {
    window->custom_border_title_thickness = 0;
    window->custom_border_edge_thickness = 0;
    window->title_bar_client_area_count = 0;
  }
}

internal void
wm_window_push_custom_title_bar(WM_Window handle, F32 thickness)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0)
  {
    window->custom_border_title_thickness = thickness;
  }
}

internal void
wm_window_push_custom_edges(WM_Window handle, F32 thickness)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0)
  {
    window->custom_border_edge_thickness = thickness;
  }
}

internal void
wm_window_push_custom_title_bar_client_area(WM_Window handle, Rng2F32 rect)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  if(window != 0 && window->title_bar_client_area_count < MAC_WM_TITLE_BAR_CLIENT_AREA_CAP)
  {
    window->title_bar_client_areas[window->title_bar_client_area_count] = rect;
    window->title_bar_client_area_count += 1;
  }
}

internal B32
wm_window_should_draw_custom_title_bar_controls(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  B32 result = 1;
  if(window != 0 && window->custom_border && mac_wm_chrome_mode_has_native_controls(window->chrome_mode))
  {
    result = 0;
  }
  return result;
}

internal F32
wm_window_native_title_bar_left_padding(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  F32 result = 0;
  if(window != 0 &&
     window->custom_border &&
     window->chrome_mode == MAC_WM_ChromeMode_Integrated &&
     !wm_window_is_fullscreen(handle))
  {
    NSButton *close_button = [window->ns_window standardWindowButton:NSWindowCloseButton];
    NSButton *zoom_button = [window->ns_window standardWindowButton:NSWindowZoomButton];
    if(close_button != 0 && zoom_button != 0)
    {
      result = (F32)Max(NSMaxX([close_button frame]), NSMaxX([zoom_button frame])) + 16.f;
    }
    result = Max(result, 88.f);
  }
  return result;
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
    MAC_WM_Window *window = mac_wm_window_from_ns_window([event window]);
    NSEventType type = [event type];
    B32 send_to_nsapp = 1;
    switch(type)
    {
      default:{}break;
      case NSEventTypeApplicationDefined:
      {
        mac_wm_push_event(arena, &result, WM_EventKind_Wakeup, window);
      }break;
      case NSEventTypeKeyDown:
      case NSEventTypeKeyUp:
      {
        B32 is_release = (type == NSEventTypeKeyUp);
        B32 right_sided = 0;
        WM_Key key = mac_wm_key_from_key_code([event keyCode], &right_sided);
        WM_Event *wm_event = mac_wm_push_event(arena, &result, is_release ? WM_EventKind_Release : WM_EventKind_Press, window);
        wm_event->modifiers = mac_wm_modifiers_from_ns_flags([event modifierFlags]);
        wm_event->key = key;
        wm_event->right_sided = right_sided;
        wm_event->is_repeat = [event isARepeat];
        wm_event->repeat_count = 1;
        if(key != WM_Key_Null)
        {
          mac_wm_state->key_is_down[key] = !is_release;
        }
        if(key == WM_Key_Alt   && (wm_event->modifiers & WM_Modifier_Alt))   { wm_event->modifiers &= ~WM_Modifier_Alt; }
        if(key == WM_Key_Ctrl  && (wm_event->modifiers & WM_Modifier_Ctrl))  { wm_event->modifiers &= ~WM_Modifier_Ctrl; }
        if(key == WM_Key_Shift && (wm_event->modifiers & WM_Modifier_Shift)) { wm_event->modifiers &= ~WM_Modifier_Shift; }
        if(type == NSEventTypeKeyDown)
        {
          mac_wm_push_text_events_from_ns_string(arena, &result, window, [event characters]);
        }
      }break;
      case NSEventTypeFlagsChanged:
      {
        B32 right_sided = 0;
        WM_Key key = mac_wm_key_from_key_code([event keyCode], &right_sided);
        if(key != WM_Key_Null)
        {
          WM_Modifiers modifiers = mac_wm_modifiers_from_ns_flags([event modifierFlags]);
          B32 is_down = 0;
          if(key == WM_Key_Shift) { is_down = !!(modifiers & WM_Modifier_Shift); }
          else if(key == WM_Key_Alt) { is_down = !!(modifiers & WM_Modifier_Alt); }
          else if(key == WM_Key_Ctrl) { is_down = !!(modifiers & WM_Modifier_Ctrl); }
          if(mac_wm_state->key_is_down[key] != is_down)
          {
            WM_Event *wm_event = mac_wm_push_event(arena, &result, is_down ? WM_EventKind_Press : WM_EventKind_Release, window);
            wm_event->modifiers = modifiers;
            wm_event->key = key;
            wm_event->right_sided = right_sided;
            mac_wm_state->key_is_down[key] = is_down;
          }
        }
      }break;
      case NSEventTypeLeftMouseDown:
      case NSEventTypeLeftMouseUp:
      case NSEventTypeRightMouseDown:
      case NSEventTypeRightMouseUp:
      case NSEventTypeOtherMouseDown:
      case NSEventTypeOtherMouseUp:
      {
        B32 is_release = (type == NSEventTypeLeftMouseUp ||
                          type == NSEventTypeRightMouseUp ||
                          type == NSEventTypeOtherMouseUp);
        WM_Key key = WM_Key_Null;
        if(type == NSEventTypeLeftMouseDown || type == NSEventTypeLeftMouseUp)
        {
          key = WM_Key_LeftMouseButton;
        }
        else if(type == NSEventTypeRightMouseDown || type == NSEventTypeRightMouseUp)
        {
          key = WM_Key_RightMouseButton;
        }
        else
        {
          key = WM_Key_MiddleMouseButton;
        }
        Vec2F32 pos = mac_wm_client_pos_from_ns_point(window, [event locationInWindow]);
        B32 handled_by_chrome = 0;
        if(window != 0 &&
           window->custom_border &&
           window->chrome_mode != MAC_WM_ChromeMode_Native &&
           type == NSEventTypeLeftMouseDown &&
           pos.y <= window->custom_border_title_thickness &&
           !mac_wm_window_pos_is_native_title_bar_control_area(window, pos) &&
           !mac_wm_window_pos_is_title_bar_client_area(window, pos))
        {
          [window->ns_window performWindowDragWithEvent:event];
          handled_by_chrome = 1;
          send_to_nsapp = 0;
        }
        if(!handled_by_chrome)
        {
          WM_Event *wm_event = mac_wm_push_event(arena, &result, is_release ? WM_EventKind_Release : WM_EventKind_Press, window);
          wm_event->modifiers = mac_wm_modifiers_from_ns_flags([event modifierFlags]);
          wm_event->key = key;
          wm_event->pos = pos;
          mac_wm_state->key_is_down[key] = !is_release;
        }
      }break;
      case NSEventTypeMouseMoved:
      case NSEventTypeLeftMouseDragged:
      case NSEventTypeRightMouseDragged:
      case NSEventTypeOtherMouseDragged:
      {
        WM_Event *wm_event = mac_wm_push_event(arena, &result, WM_EventKind_MouseMove, window);
        wm_event->modifiers = mac_wm_modifiers_from_ns_flags([event modifierFlags]);
        wm_event->pos = mac_wm_client_pos_from_ns_point(window, [event locationInWindow]);
      }break;
      case NSEventTypeScrollWheel:
      {
        WM_Event *wm_event = mac_wm_push_event(arena, &result, WM_EventKind_Scroll, window);
        wm_event->modifiers = mac_wm_modifiers_from_ns_flags([event modifierFlags]);
        wm_event->pos = mac_wm_client_pos_from_ns_point(window, [event locationInWindow]);
        wm_event->delta = v2f32(-(F32)[event scrollingDeltaX], -(F32)[event scrollingDeltaY]);
      }break;
    }
    if(send_to_nsapp)
    {
      [NSApp sendEvent:event];
    }
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
    if(window->lose_focus_requested)
    {
      window->lose_focus_requested = 0;
      WM_Event *event = wm_event_list_push_new(arena, &result, WM_EventKind_WindowLoseFocus);
      event->window = mac_wm_handle_from_window(window);
    }
  }
  for(MAC_WM_MenuCommandNode *node = mac_wm_state->first_pending_menu_command, *next = 0;
      node != 0;
      node = next)
  {
    next = node->next;
    WM_Event *event = wm_event_list_push_new(arena, &result, WM_EventKind_MenuCommand);
    event->window = node->window;
    event->string = push_str8_copy(arena, node->command_name);
    SLLStackPush(mac_wm_state->free_menu_command_node, node);
  }
  mac_wm_state->first_pending_menu_command = 0;
  mac_wm_state->last_pending_menu_command = 0;
  return result;
}

internal WM_Modifiers
wm_get_modifiers(void)
{
  NSEventModifierFlags flags = [NSEvent modifierFlags];
  WM_Modifiers result = mac_wm_modifiers_from_ns_flags(flags);
  return result;
}

internal B32
wm_key_is_down(WM_Key key)
{
  B32 result = 0;
  if(key < WM_Key_COUNT)
  {
    result = mac_wm_state->key_is_down[key];
  }
  return result;
}

internal Vec2F32
wm_mouse_from_window(WM_Window handle)
{
  MAC_WM_Window *window = mac_wm_window_from_handle(handle);
  Vec2F32 result = {0};
  if(window != 0)
  {
    NSPoint point = [window->ns_window mouseLocationOutsideOfEventStream];
    result = mac_wm_client_pos_from_ns_point(window, point);
  }
  return result;
}

////////////////////////////////
//~ @os_hooks Application Menu (Implemented Per-OS)

internal NSString *
mac_wm_persistent_ns_string_from_string8(String8 string)
{
  NSString *result = [[NSString alloc] initWithBytes:string.str length:string.size encoding:NSUTF8StringEncoding];
  return result;
}

internal void
wm_set_main_menu(WM_MenuArray menu_array)
{
  if(mac_wm_state->menu_mode == MAC_WM_MenuMode_Native)
  {
    NSMenu *main_menu = [[NSMenu alloc] initWithTitle:@""];

    NSMenuItem *app_menu_item = [[NSMenuItem alloc] initWithTitle:@"" action:0 keyEquivalent:@""];
    [main_menu addItem:app_menu_item];

    NSMenu *app_menu = [[NSMenu alloc] initWithTitle:@"RAD Debugger"];
    NSMenuItem *quit_item = [[NSMenuItem alloc] initWithTitle:@"Quit RAD Debugger"
                                                       action:@selector(menuItemSelected:)
                                                keyEquivalent:@"q"];
    [quit_item setTarget:mac_wm_state->menu_target];
    [quit_item setRepresentedObject:mac_wm_persistent_ns_string_from_string8(str8_lit("exit"))];
    [app_menu addItem:quit_item];
    [main_menu setSubmenu:app_menu forItem:app_menu_item];

    for(U64 menu_idx = 0; menu_idx < menu_array.count; menu_idx += 1)
    {
      WM_Menu *menu = &menu_array.menus[menu_idx];
      NSString *menu_label = mac_wm_persistent_ns_string_from_string8(menu->label);
      NSMenuItem *menu_item = [[NSMenuItem alloc] initWithTitle:menu_label action:0 keyEquivalent:@""];
      NSMenu *submenu = [[NSMenu alloc] initWithTitle:menu_label];
      [main_menu addItem:menu_item];
      [main_menu setSubmenu:submenu forItem:menu_item];

      for(U64 item_idx = 0; item_idx < menu->item_count; item_idx += 1)
      {
        WM_MenuItem *item = &menu->items[item_idx];
        switch(item->kind)
        {
          default:{}break;
          case WM_MenuItemKind_Separator:
          {
            [submenu addItem:[NSMenuItem separatorItem]];
          }break;
          case WM_MenuItemKind_Command:
          {
            NSString *label = mac_wm_persistent_ns_string_from_string8(item->label);
            NSString *command = mac_wm_persistent_ns_string_from_string8(item->command_name);
            NSMenuItem *ns_item = [[NSMenuItem alloc] initWithTitle:label
                                                             action:@selector(menuItemSelected:)
                                                      keyEquivalent:@""];
            [ns_item setTarget:mac_wm_state->menu_target];
            [ns_item setRepresentedObject:command];
            [submenu addItem:ns_item];
          }break;
        }
      }
    }

    [NSApp setMainMenu:main_menu];
  }
}

internal B32
wm_application_menu_bar_is_native(void)
{
  B32 result = (mac_wm_state->menu_mode == MAC_WM_MenuMode_Native);
  return result;
}

////////////////////////////////
//~ @os_hooks Cursors (Implemented Per-OS)

internal void
wm_set_cursor(WM_Cursor cursor)
{
  NSCursor *ns_cursor = [NSCursor arrowCursor];
  switch(cursor)
  {
    default:{}break;
    case WM_Cursor_Pointer:{ns_cursor = [NSCursor arrowCursor];}break;
    case WM_Cursor_IBar:{ns_cursor = [NSCursor IBeamCursor];}break;
    case WM_Cursor_LeftRight:{ns_cursor = [NSCursor resizeLeftRightCursor];}break;
    case WM_Cursor_UpDown:{ns_cursor = [NSCursor resizeUpDownCursor];}break;
    case WM_Cursor_HandPoint:{ns_cursor = [NSCursor pointingHandCursor];}break;
    case WM_Cursor_Disabled:{ns_cursor = [NSCursor operationNotAllowedCursor];}break;
    case WM_Cursor_DownRight:
    case WM_Cursor_UpRight:
    case WM_Cursor_UpDownLeftRight:{ns_cursor = [NSCursor arrowCursor];}break;
  }
  [ns_cursor set];
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
  Temp scratch = scratch_begin(&arena, 1);
  NSOpenPanel *panel = [NSOpenPanel openPanel];
  [panel setCanChooseFiles:YES];
  [panel setCanChooseDirectories:NO];
  [panel setAllowsMultipleSelection:NO];
  if(initial_path.size != 0)
  {
    NSString *ns_path = mac_wm_ns_string_from_string8(scratch.arena, initial_path);
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
