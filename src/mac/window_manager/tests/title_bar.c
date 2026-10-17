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
    WM_Window custom = wm_window_open(r2f32p(100, 100, 900, 700), WM_WindowFlag_CustomBorder, str8_lit("Custom title bar"));
    WM_Window native = wm_window_open(r2f32p(100, 100, 900, 700), 0, str8_lit("Native title bar"));
    MAC_WM_Window *custom_window = mac_wm_window_from_handle(custom);
    MAC_WM_Window *native_window = mac_wm_window_from_handle(native);
    MAC_WM_ChromeMode modes[] = {MAC_WM_ChromeMode_Integrated, MAC_WM_ChromeMode_Custom};
    U64 failures = 0;
    for EachElement(idx, modes)
    {
      mac_wm_apply_chrome_mode_to_window(custom_window, modes[idx]);
      mac_wm_apply_chrome_mode_to_window(native_window, modes[idx]);
      wm_window_clear_custom_border_data(custom);
      wm_window_push_custom_title_bar(custom, 33);
      wm_window_push_custom_title_bar_client_area(custom, r2f32p(300, 0, 333, 33));
      B32 custom_movable = [custom_window->ns_window isMovable];
      B32 native_movable = [native_window->ns_window isMovable];
      B32 good = !custom_movable && native_movable &&
                 mac_wm_window_pos_is_title_bar_client_area(custom_window, v2f32(316, 16)) &&
                 !mac_wm_window_pos_is_title_bar_background(custom_window, v2f32(316, 16)) &&
                 mac_wm_window_pos_is_title_bar_background(custom_window, v2f32(400, 16)) &&
                 !mac_wm_window_pos_is_title_bar_background(custom_window, v2f32(400, 100));
      fprintf(stderr, "%s: title-bar policy mode=%d automatic_drag custom=%d native=%d\n",
              good ? "PASS" : "FAIL", modes[idx], custom_movable, native_movable);
      failures += !good;
    }
    // This checks policy and hit regions, not physical WindowServer drag behaviour.
    wm_window_close(custom);
    wm_window_close(native);
    exit(failures != 0);
  }
}
