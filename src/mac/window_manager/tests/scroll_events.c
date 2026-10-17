// Licensed under the MIT license (https://opensource.org/license/mit/)
#define BUILD_CONSOLE_INTERFACE 1
#include "base/base_inc.h"
#include "window_manager/window_manager_inc.h"
#include "base/base_inc.c"
#include "window_manager/window_manager_inc.c"
#include "render/render_inc.h"
#include "font_provider/font_provider_inc.h"
#include "font_cache/font_cache.h"
#include "font_cache/font_cache.c"
#include "draw/draw.h"
#include "ui/ui_inc.h"
#include "ui/ui_core.c"

global U32 failures;
#define Check(test, name) do { B32 pass = (test); failures += !pass; fprintf(stderr, "%s: %s\n", pass ? "PASS" : "FAIL", name); } while(0)

internal void
entry_point(CmdLine *cmdline)
{
  ui_state = ui_state_alloc();
  UI_InitStacks(ui_state);
  Temp scratch = scratch_begin(0, 0);
  UI_EventList events = {0};
  ui_state->events = &events;
  UI_Box box = {0};
  box.flags = UI_BoxFlag_Scroll|UI_BoxFlag_ScrollPrecise;
  box.rect = r2f32p(0, 0, 100, 100);
  UI_Event event = {0};
  event.kind = UI_EventKind_Scroll;
  event.pos = v2f32(50, 50);
  event.delta_2f32.y = 0.25f;
  event.scroll_is_precise = 1;
  ui_event_list_push(scratch.arena, &events, &event);
  UI_Signal sig = ui_signal_from_box(&box);
  B32 good = sig.scroll.y == 0 && sig.scroll_px.y == 0.25f && events.count == 0;
  Check(good, "precise movement reaches the view without row rounding");
  UI_ScrollPt position = ui_scroll_pt(10, 0);
  ui_scroll_pt_scroll(&position, 0.25f, r1s64(0, 100), 0);
  Check(position.idx == 10 && ui_scroll_pt_offset(position) == 0.25f && position.off == 0,
        "quarter-row movement remains visible at rest");
  for(U64 i = 0; i < 3; i += 1) { ui_scroll_pt_scroll(&position, 0.25f, r1s64(0, 100), 0); }
  Check(position.idx == 11 && ui_scroll_pt_offset(position) == 0, "split movement totals one row");
  ui_scroll_pt_scroll(&position, -0.25f, r1s64(0, 100), 0);
  Check(position.idx == 10 && ui_scroll_pt_offset(position) == 0.75f, "reverse movement preserves fraction");
  ui_scroll_pt_scroll(&position, 1000, r1s64(0, 100), 0);
  Check(position.idx == 100 && ui_scroll_pt_offset(position) == 0, "upper limit has no overshoot");
  ui_scroll_pt_scroll(&position, -1000, r1s64(0, 100), 0);
  Check(position.idx == 0 && ui_scroll_pt_offset(position) == 0, "lower limit has no overshoot");
  ui_scroll_pt_scroll(&position, 0.25f, r1s64(0, 100), 0);
  Check(position.idx == 0 && ui_scroll_pt_offset(position) == 0.25f, "reverse immediately after clamping");
  ui_scroll_pt_target_idx(&position, 20);
  Check(position.idx == 20 && position.target_off == 0 && position.off == -19.75f,
        "jump targets a whole row while preserving the starting visual position");
  ui_scroll_pt_scroll(&position, 0.25f, r1s64(0, 100), 0);
  Check(position.idx == 0 && ui_scroll_pt_offset(position) == 0.5f && position.off == 0,
        "precise gesture takes over from the visible animation position");
  position = ui_scroll_pt(10, 0);
  for(U64 i = 0; i < 3; i += 1) { ui_scroll_pt_scroll(&position, 0.25f, r1s64(0, 100), 1); }
  Check(position.idx == 10 && ui_scroll_pt_offset(position) == 0, "snap mode waits for a whole row");
  ui_scroll_pt_scroll(&position, 0.25f, r1s64(0, 100), 1);
  Check(position.idx == 11 && position.target_off == 0 && position.remainder == 0, "snap mode retains tiny movements");
  position = ui_scroll_pt(0, 0);
  ui_scroll_pt_scroll(&position, -0.75f, r1s64(0, 100), 1);
  ui_scroll_pt_scroll(&position, 1, r1s64(0, 100), 1);
  Check(position.idx == 1, "snap mode discards outward movement at a limit");
  S64 large = (S64)1 << 54;
  position = ui_scroll_pt(large, 0);
  ui_scroll_pt_scroll(&position, 0.25f, r1s64(large-100, large+100), 0);
  Check(position.idx == large && ui_scroll_pt_offset(position) == 0.25f, "large row index retains small movement");

  // Each iteration represents a separate delivery/frame. Two receiving boxes
  // must not share movement, and Shift swaps axes before content conversion.
  UI_ScrollPt first = ui_scroll_pt(10, 0), second = ui_scroll_pt(50, 0);
  UI_Box other_box = box;
  other_box.rect = r2f32p(100, 0, 200, 100);
  for(U64 i = 0; i < 120; i += 1)
  {
    ui_event_list_push(scratch.arena, &events, &event);
    sig = ui_signal_from_box(&other_box);
    ui_scroll_pt_scroll(&second, sig.scroll_px.y/30.f, r1s64(0, 100), 0);
    sig = ui_signal_from_box(&box);
    ui_scroll_pt_scroll(&first, sig.scroll_px.y/30.f, r1s64(0, 100), 0);
  }
  Check(abs_f32((F32)(first.idx-11) + ui_scroll_pt_offset(first)) < 0.00001f &&
        second.idx == 50 && ui_scroll_pt_offset(second) == 0,
        "120 tiny events move one row, leaving another view unchanged");
  event.pos.x = 150;
  ui_event_list_push(scratch.arena, &events, &event);
  sig = ui_signal_from_box(&box);
  Check(sig.scroll_px.y == 0 && events.count == 1, "a view does not consume another view's event");
  sig = ui_signal_from_box(&other_box);
  ui_scroll_pt_scroll(&second, sig.scroll_px.y/30.f, r1s64(0, 100), 0);
  Check(second.idx == 50 && ui_scroll_pt_offset(second) == 0.25f/30.f && events.count == 0,
        "the second view receives its own fractional movement");
  event.pos.x = 50;
  event.modifiers = WM_Modifier_Shift;
  ui_event_list_push(scratch.arena, &events, &event);
  sig = ui_signal_from_box(&box);
  Check(sig.scroll_px.x == 0.25f && sig.scroll_px.y == 0, "Shift routes precise movement horizontally");
  event.modifiers = 0;
  event.scroll_is_precise = 0;
  event.delta_2f32.y = 120;
  ui_event_list_push(scratch.arena, &events, &event);
  sig = ui_signal_from_box(&box);
  Check(sig.scroll.y == 4 && sig.scroll_px.y == 0, "Windows-style wheel delta retains four rows");
  event.delta_2f32.y = 1;
  ui_event_list_push(scratch.arena, &events, &event);
  sig = ui_signal_from_box(&box);
  Check(sig.scroll.y == 1, "Linux-style wheel delta retains one row");
  event.scroll_is_precise = 1;
  event.delta_2f32.y = 0.25f;
  box.flags = UI_BoxFlag_Scroll;
  ui_event_list_push(scratch.arena, &events, &event);
  sig = ui_signal_from_box(&box);
  Check(sig.scroll.y == 1 && sig.scroll_px.y == 0, "unadapted row views keep their existing input path");
  box.flags = UI_BoxFlag_ViewScrollY;
  box.first_touched_build_index = 1;
  box.last_touched_build_index = 2;
  ui_event_list_push(scratch.arena, &events, &event);
  sig = ui_signal_from_box(&box);
  Check(box.view_off_target.y == 0.25f, "existing pixel view retains precise movement");

  @autoreleasepool
  {
    for(U32 precise = 0; precise < 2; precise += 1)
    {
      wm_get_events(scratch.arena, 0);
      CGEventRef cg = CGEventCreateScrollWheelEvent(0, precise ? kCGScrollEventUnitPixel : kCGScrollEventUnitLine, 1, 1);
      NSEvent *native = [NSEvent eventWithCGEvent:cg];
      [NSApp postEvent:native atStart:NO];
      WM_EventList translated = wm_get_events(scratch.arena, 0);
      U64 count = 0;
      for(WM_Event *e = translated.first; e != 0; e = e->next)
      {
        if(e->kind == WM_EventKind_Scroll)
        {
          count += 1;
          Check(e->scroll_is_precise == precise && e->delta.y == -(F32)[native scrollingDeltaY],
                "AppKit scroll units and distance survive WM translation");
        }
      }
      Check(count == 1, "one local AppKit event produces one WM scroll event");
      CFRelease(cg);
    }
  }
  fprintf(stderr, "Scroll tests: %u failures\n", failures);
  exit(failures != 0);
}
