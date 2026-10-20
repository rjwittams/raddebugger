// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BUILD_CONSOLE_INTERFACE 1
#include "base/base_inc.h"
#include "window_manager/window_manager_inc.h"
#include "base/base_inc.c"
#include "window_manager/window_manager_inc.c"

internal void
entry_point(CmdLine *cmdline)
{
  @autoreleasepool
  {
    WM_Window window = wm_window_open(r2f32p(0, 0, 320, 200), 0, str8_lit("Key event tests"));
    NSWindow *ns_window = mac_wm_window_from_handle(window)->ns_window;
    struct
    {
      char *name;
      NSEventModifierFlags flags;
      U16 key_code;
      WM_Key key;
      NSString *characters;
      U32 expected_character;
      WM_Modifiers expected_modifiers;
      B32 repeat;
    } cases[] =
    {
      {"plain",          0,                          0, WM_Key_A, @"a",      'a',    0},
      {"shift",          NSEventModifierFlagShift,   0, WM_Key_A, @"A",      'A',    WM_Modifier_Shift},
      {"option",         NSEventModifierFlagOption,  0, WM_Key_A, @"\u00e5", 0xe5,   WM_Modifier_Alt},
      {"option-shift",   NSEventModifierFlagOption|NSEventModifierFlagShift,
                                                     0, WM_Key_A, @"\u00c5", 0xc5,   WM_Modifier_Alt|WM_Modifier_Shift},
      {"command-shift-p",NSEventModifierFlagCommand|NSEventModifierFlagShift,
                                                    35, WM_Key_P, @"p",      0,      WM_Modifier_Cmd|WM_Modifier_Shift},
      {"command-f",      NSEventModifierFlagCommand, 3, WM_Key_F, @"f",      0,      WM_Modifier_Cmd},
      {"command-control",NSEventModifierFlagCommand|NSEventModifierFlagControl,
                                                     3, WM_Key_F, @"f",      0,      WM_Modifier_Cmd|WM_Modifier_Ctrl},
      {"control-a",      NSEventModifierFlagControl, 0, WM_Key_A, @"\x01",   0,      WM_Modifier_Ctrl},
      {"control-shift-p",NSEventModifierFlagControl|NSEventModifierFlagShift,
                                                    35, WM_Key_P, @"P",      0,      WM_Modifier_Ctrl|WM_Modifier_Shift},
      {"return",         0,                         36, WM_Key_Return, @"\r", 0,     0},
      {"delete",         0,                         51, WM_Key_Backspace, @"\x7f", 0, 0},
      {"left-arrow",     0,                        123, WM_Key_Left, @"\uf702", 0,   0},
      {"repeat-text",    0,                          0, WM_Key_A, @"a",      'a',    0, 1},
      {"repeat-command", NSEventModifierFlagCommand, 3, WM_Key_F, @"f",      0,      WM_Modifier_Cmd, 1},
    };
    U64 failures = 0;
    for EachElement(idx, cases)
    {
      Temp scratch = scratch_begin(0, 0);
      wm_get_events(scratch.arena, 0);
      // Post into this application's queue, not the system-wide input stream.
      for(U64 release = 0; release < 2; release += 1)
      {
        NSEvent *event = [NSEvent keyEventWithType:release ? NSEventTypeKeyUp : NSEventTypeKeyDown
                                         location:NSZeroPoint
                                    modifierFlags:cases[idx].flags
                                        timestamp:0
                                     windowNumber:[ns_window windowNumber]
                                          context:0
                                       characters:cases[idx].characters
                      charactersIgnoringModifiers:cases[idx].characters
                                        isARepeat:!release && cases[idx].repeat
                                          keyCode:cases[idx].key_code];
        [NSApp postEvent:event atStart:NO];
      }
      WM_EventList events = wm_get_events(scratch.arena, 0);
      U64 presses = 0, releases = 0, texts = 0;
      B32 good = 1;
      for(WM_Event *event = events.first; event != 0; event = event->next)
      {
        if(event->kind == WM_EventKind_Press || event->kind == WM_EventKind_Release)
        {
          B32 release = event->kind == WM_EventKind_Release;
          presses += !release;
          releases += release;
          good &= wm_window_match(event->window, window);
          good &= event->key == cases[idx].key;
          good &= event->modifiers == cases[idx].expected_modifiers;
          good &= event->is_repeat == (!release && cases[idx].repeat);
        }
        else if(event->kind == WM_EventKind_Text)
        {
          texts += 1;
          good &= wm_window_match(event->window, window);
          good &= event->character == cases[idx].expected_character;
        }
      }
      good &= presses == 1 && releases == 1;
      good &= texts == (cases[idx].expected_character != 0);
      good &= !mac_wm_state->key_is_down[cases[idx].key];
      fprintf(stderr, "%s: %s (press=%llu release=%llu text=%llu)\n",
              good ? "PASS" : "FAIL", cases[idx].name, presses, releases, texts);
      failures += !good;
      scratch_end(scratch);
    }
    // Modifier keys themselves must remain distinct as well as their masks.
    B32 right = 0;
    B32 modifier_keys_good = mac_wm_key_from_key_code(54, &right) == WM_Key_Cmd && right;
    modifier_keys_good &= mac_wm_key_from_key_code(55, &right) == WM_Key_Cmd && !right;
    modifier_keys_good &= mac_wm_key_from_key_code(59, &right) == WM_Key_Ctrl && !right;
    modifier_keys_good &= mac_wm_key_from_key_code(62, &right) == WM_Key_Ctrl && right;
    fprintf(stderr, "%s: physical modifier keys\n", modifier_keys_good ? "PASS" : "FAIL");
    failures += !modifier_keys_good;

    // Exercise the native-menu adapter without system-wide input synthesis.
    WM_MenuItem items[] =
    {
      {WM_MenuItemKind_Command, str8_lit("Palette"), str8_lit("open_palette"), WM_Key_P, WM_Modifier_Cmd|WM_Modifier_Shift},
      {WM_MenuItemKind_Command, str8_lit("Exit"), str8_lit("exit"), WM_Key_Q, WM_Modifier_Cmd},
      {WM_MenuItemKind_Command, str8_lit("Step"), str8_lit("step_into"), WM_Key_F11, 0},
    };
    WM_Menu menu = {str8_lit("Probe"), ArrayCount(items), items};
    WM_MenuArray menus = {1, &menu};
    wm_set_preferred_native_menu_bar(1);
    wm_set_main_menu(menus);
    NSMenu *native_menu = [[[NSApp mainMenu] itemAtIndex:1] submenu];
    NSMenuItem *palette = [native_menu itemAtIndex:0];
    B32 menu_good = [[palette keyEquivalent] isEqualToString:@"p"] &&
                   [palette keyEquivalentModifierMask] == (NSEventModifierFlagCommand|NSEventModifierFlagShift);
    NSMenuItem *quit = [[[[NSApp mainMenu] itemAtIndex:0] submenu] itemAtIndex:0];
    menu_good &= [[quit keyEquivalent] isEqualToString:@"q"] && [quit keyEquivalentModifierMask] == NSEventModifierFlagCommand;
    menu_good &= [[[native_menu itemAtIndex:1] keyEquivalent] characterAtIndex:0] == NSF11FunctionKey;
    fprintf(stderr, "menu equivalents: palette=%s/%lx quit=%s/%lx step=%x good=%d\n",
            [[palette keyEquivalent] UTF8String], (unsigned long)[palette keyEquivalentModifierMask],
            [[quit keyEquivalent] UTF8String], (unsigned long)[quit keyEquivalentModifierMask],
            [[[native_menu itemAtIndex:1] keyEquivalent] characterAtIndex:0], menu_good);
    [mac_wm_state->menu_target menuWillOpen:native_menu];
    mac_wm_set_focused_window(mac_wm_window_from_handle(window));
    [mac_wm_state->menu_target menuItemSelected:palette];
    Temp scratch = scratch_begin(0, 0);
    WM_EventList menu_events = wm_get_events(scratch.arena, 0);
    U64 opens = 0, commands = 0, presses = 0;
    for(WM_Event *event = menu_events.first; event != 0; event = event->next)
    {
      opens += event->kind == WM_EventKind_MenuOpen;
      presses += event->kind == WM_EventKind_Press;
      if(event->kind == WM_EventKind_MenuCommand)
      {
        commands += 1;
        menu_good &= opens == 1 && str8_match(event->string, str8_lit("open_palette"), 0);
      }
    }
    menu_good &= opens == 1 && commands == 1 && presses == 0;
    fprintf(stderr, "menu events: open=%llu command=%llu press=%llu good=%d\n", opens, commands, presses, menu_good);
    scratch_end(scratch);
    items[0].shortcut_key = WM_Key_L;
    items[0].shortcut_modifiers = WM_Modifier_Ctrl;
    wm_set_main_menu(menus);
    palette = [[[[NSApp mainMenu] itemAtIndex:1] submenu] itemAtIndex:0];
    menu_good &= [[palette keyEquivalent] isEqualToString:@"l"] && [palette keyEquivalentModifierMask] == NSEventModifierFlagControl;
    fprintf(stderr, "updated equivalent: %s/%lx good=%d\n", [[palette keyEquivalent] UTF8String],
            (unsigned long)[palette keyEquivalentModifierMask], menu_good);
    wm_set_preferred_native_menu_bar(0);
    wm_set_main_menu(menus);
    menu_good &= [[NSApp mainMenu] numberOfItems] == 1;
    quit = [[[[NSApp mainMenu] itemAtIndex:0] submenu] itemAtIndex:0];
    menu_good &= [[quit keyEquivalent] isEqualToString:@"q"];
    fprintf(stderr, "minimal menu: count=%ld quit=%s good=%d\n", (long)[[NSApp mainMenu] numberOfItems],
            [[quit keyEquivalent] UTF8String], menu_good);
    fprintf(stderr, "%s: native menu bindings, mode and event queue\n", menu_good ? "PASS" : "FAIL");
    failures += !menu_good;
    wm_window_close(window);
    exit(failures != 0);
  }
}
