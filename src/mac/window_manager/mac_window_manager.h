// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_WINDOW_MANAGER_H
#define MAC_WINDOW_MANAGER_H

#pragma push_macro("internal")
#pragma push_macro("global")
#pragma push_macro("FileInfo")
#undef internal
#undef global
#define FileInfo MACOS_SDK_FileInfo
#import <Foundation/NSArray.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>
#import <AppKit/NSAlert.h>
#import <AppKit/NSApplication.h>
#import <AppKit/NSCursor.h>
#import <AppKit/NSEvent.h>
#import <AppKit/NSOpenPanel.h>
#import <AppKit/NSPasteboard.h>
#import <AppKit/NSScreen.h>
#import <AppKit/NSWindow.h>
#import <AppKit/NSWorkspace.h>
#undef FileInfo
#pragma pop_macro("FileInfo")
#pragma pop_macro("global")
#pragma pop_macro("internal")
#if defined(nil)
# undef nil
#endif

typedef struct MAC_WM_Window MAC_WM_Window;

@interface MAC_WM_WindowDelegate : NSObject<NSWindowDelegate>
{
@public
  MAC_WM_Window *window;
}
@end

struct MAC_WM_Window
{
  MAC_WM_Window *next;
  MAC_WM_Window *prev;
  NSWindow *ns_window;
  MAC_WM_WindowDelegate *delegate;
  B32 close_requested;
  B32 lose_focus_requested;
};

typedef struct MAC_WM_State MAC_WM_State;
struct MAC_WM_State
{
  Arena *arena;
  WM_SystemInfo gfx_info;
  MAC_WM_Window *first_window;
  MAC_WM_Window *last_window;
  MAC_WM_Window *free_window;
  B32 key_is_down[WM_Key_COUNT];
};

global MAC_WM_State *mac_wm_state = 0;

internal WM_Window mac_wm_handle_from_window(MAC_WM_Window *window);
internal MAC_WM_Window *mac_wm_window_from_handle(WM_Window handle);
internal MAC_WM_Window *mac_wm_window_from_ns_window(NSWindow *ns_window);
internal Rng2F32 mac_wm_rect_from_ns_rect(NSRect rect);
internal Vec2F32 mac_wm_client_pos_from_ns_point(MAC_WM_Window *window, NSPoint point);
internal NSString *mac_wm_ns_string_from_string8(Arena *arena, String8 string);
internal WM_Modifiers mac_wm_modifiers_from_ns_flags(NSEventModifierFlags flags);
internal WM_Key mac_wm_key_from_key_code(unsigned short key_code, B32 *right_sided_out);
internal void mac_wm_push_text_events_from_ns_string(Arena *arena, WM_EventList *events, MAC_WM_Window *window, NSString *string);
internal WM_Event *mac_wm_push_event(Arena *arena, WM_EventList *events, WM_EventKind kind, MAC_WM_Window *window);

#endif // MAC_WINDOW_MANAGER_H
