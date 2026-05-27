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
#import <Foundation/NSRunLoop.h>
#import <Foundation/NSURL.h>
#import <AppKit/NSAlert.h>
#import <AppKit/NSApplication.h>
#import <AppKit/NSCursor.h>
#import <AppKit/NSEvent.h>
#import <AppKit/NSMenu.h>
#import <AppKit/NSMenuItem.h>
#import <AppKit/NSOpenPanel.h>
#import <AppKit/NSPasteboard.h>
#import <AppKit/NSScreen.h>
#import <AppKit/NSWindow.h>
#import <AppKit/NSWorkspace.h>
#import <QuartzCore/CADisplayLink.h>
#undef FileInfo
#pragma pop_macro("FileInfo")
#pragma pop_macro("global")
#pragma pop_macro("internal")
#if defined(nil)
# undef nil
#endif

typedef struct MAC_WM_Window MAC_WM_Window;

typedef enum MAC_WM_ChromeMode
{
  MAC_WM_ChromeMode_Integrated,
  MAC_WM_ChromeMode_Custom,
}
MAC_WM_ChromeMode;

typedef enum MAC_WM_MenuMode
{
  MAC_WM_MenuMode_PerWindow,
  MAC_WM_MenuMode_Native,
}
MAC_WM_MenuMode;

typedef struct MAC_WM_MenuCommandNode MAC_WM_MenuCommandNode;
struct MAC_WM_MenuCommandNode
{
  MAC_WM_MenuCommandNode *next;
  WM_Window window;
  String8 command_name;
  Vec2F32 pos;
};

@interface MAC_WM_WindowDelegate : NSObject<NSWindowDelegate>
{
@public
  MAC_WM_Window *window;
  CADisplayLink *live_resize_display_link;
  B32 live_resize_update_in_progress;
}
- (void)macLiveResizeStopDisplayLink;
@end

@interface MAC_WM_MenuTarget : NSObject
- (void)menuItemSelected:(id)sender;
@end

#define MAC_WM_TITLE_BAR_CLIENT_AREA_CAP 128

struct MAC_WM_Window
{
  MAC_WM_Window *next;
  MAC_WM_Window *prev;
  NSWindow *ns_window;
  MAC_WM_WindowDelegate *delegate;
  MAC_WM_ChromeMode chrome_mode;
  B32 custom_border;
  F32 custom_border_title_thickness;
  F32 custom_border_edge_thickness;
  U64 title_bar_client_area_count;
  Rng2F32 title_bar_client_areas[MAC_WM_TITLE_BAR_CLIENT_AREA_CAP];
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
  MAC_WM_Window *focused_window;
  MAC_WM_ChromeMode chrome_mode;
  MAC_WM_MenuMode menu_mode;
  MAC_WM_MenuTarget *menu_target;
  MAC_WM_MenuCommandNode *first_pending_menu_command;
  MAC_WM_MenuCommandNode *last_pending_menu_command;
  MAC_WM_MenuCommandNode *free_menu_command_node;
  B32 key_is_down[WM_Key_COUNT];
};

global MAC_WM_State *mac_wm_state = 0;

internal WM_Window mac_wm_handle_from_window(MAC_WM_Window *window);
internal MAC_WM_Window *mac_wm_window_from_handle(WM_Window handle);
internal MAC_WM_Window *mac_wm_window_from_ns_window(NSWindow *ns_window);
internal B32 mac_wm_window_is_live(MAC_WM_Window *window);
internal void mac_wm_set_focused_window(MAC_WM_Window *window);
internal MAC_WM_Window *mac_wm_menu_command_target_window(void);
internal B32 mac_wm_event_type_should_activate_window(NSEventType type);
internal void mac_wm_activate_window(MAC_WM_Window *window);
internal MAC_WM_Window *mac_wm_window_to_focus_after_close(MAC_WM_Window *window);
internal Vec2F32 mac_wm_close_menu_pos_from_window(MAC_WM_Window *window);
internal void mac_wm_push_menu_command_for_window_at_pos(MAC_WM_Window *window, String8 command_name, Vec2F32 pos);
internal void mac_wm_push_menu_command_for_window(MAC_WM_Window *window, String8 command_name);
internal MAC_WM_ChromeMode mac_wm_chrome_mode_from_window_decorations(B32 enabled);
internal MAC_WM_MenuMode mac_wm_menu_mode_from_native_menu_bar(B32 enabled);
internal B32 mac_wm_chrome_mode_has_native_controls(MAC_WM_ChromeMode mode);
internal void mac_wm_apply_chrome_mode_to_window(MAC_WM_Window *window, MAC_WM_ChromeMode mode);
internal void mac_wm_apply_menu_mode(MAC_WM_MenuMode mode);
internal void mac_wm_push_menu_command(String8 command_name);
internal B32 mac_wm_window_pos_is_native_title_bar_control_area(MAC_WM_Window *window, Vec2F32 pos);
internal B32 mac_wm_window_pos_is_title_bar_client_area(MAC_WM_Window *window, Vec2F32 pos);
internal Rng2F32 mac_wm_rect_from_ns_rect(NSRect rect);
internal Vec2F32 mac_wm_client_pos_from_ns_point(MAC_WM_Window *window, NSPoint point);
internal NSString *mac_wm_ns_string_from_string8(Arena *arena, String8 string);
internal WM_Modifiers mac_wm_modifiers_from_ns_flags(NSEventModifierFlags flags);
internal WM_Key mac_wm_key_from_key_code(unsigned short key_code, B32 *right_sided_out);
internal B32 mac_wm_event_type_should_send_to_nsapp_after_translation(NSEventType type);
internal void mac_wm_push_text_events_from_ns_string(Arena *arena, WM_EventList *events, MAC_WM_Window *window, NSString *string);
internal WM_Event *mac_wm_push_event(Arena *arena, WM_EventList *events, WM_EventKind kind, MAC_WM_Window *window);

#endif // MAC_WINDOW_MANAGER_H
