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
                                                    35, WM_Key_P, @"p",      0,      WM_Modifier_Ctrl|WM_Modifier_Shift},
      {"command-f",      NSEventModifierFlagCommand, 3, WM_Key_F, @"f",      0,      WM_Modifier_Ctrl},
      {"control-a",      NSEventModifierFlagControl, 0, WM_Key_A, @"\x01",   0,      WM_Modifier_Ctrl},
      {"control-shift-p",NSEventModifierFlagControl|NSEventModifierFlagShift,
                                                    35, WM_Key_P, @"P",      0,      WM_Modifier_Ctrl|WM_Modifier_Shift},
      {"return",         0,                         36, WM_Key_Return, @"\r", 0,     0},
      {"delete",         0,                         51, WM_Key_Backspace, @"\x7f", 0, 0},
      {"left-arrow",     0,                        123, WM_Key_Left, @"\uf702", 0,   0},
      {"repeat-text",    0,                          0, WM_Key_A, @"a",      'a',    0, 1},
      {"repeat-command", NSEventModifierFlagCommand, 3, WM_Key_F, @"f",      0,      WM_Modifier_Ctrl, 1},
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
    wm_window_close(window);
    exit(failures != 0);
  }
}
