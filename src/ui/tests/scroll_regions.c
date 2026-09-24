// Licensed under the MIT license (https://opensource.org/license/mit/)
#define BUILD_CONSOLE_INTERFACE 1
#define WM_STUB 1
#define R_BACKEND R_BACKEND_STUB
#include "base/base_inc.h"
#include "window_manager/window_manager_inc.h"
#include "render/render_inc.h"
#include "font_provider/font_provider_inc.h"
#include "font_cache/font_cache.h"
#include "draw/draw.h"
#include "ui/ui_inc.h"
#include "base/base_inc.c"
#include "window_manager/window_manager_inc.c"
#include "render/render_inc.c"
#include "font_provider/font_provider_inc.c"
#include "font_cache/font_cache.c"
#include "draw/draw.c"
#include "ui/ui_inc.c"
typedef struct ScrollTestWindow ScrollTestWindow;
struct ScrollTestWindow { UI_State *ui; WM_Window os; UI_Theme *theme; };
// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

// Uses a separate UI state and synthetic events; never sends input to the OS.
internal UI_ScrollRegionSignal
scroll_test_frame(ScrollTestWindow *ws, UI_ScrollRegion *region, UI_ScrollRegionAxis axes[Axis2_COUNT],
                          Vec2F32 mouse, UI_Event event, UI_Signal *content_signal)
{
  UI_IconInfo icons = ws->ui->icon_info;
  UI_AnimationInfo animation = {0};
  animation.scroll_animation_rate = animation.hot_animation_rate = 1.f;
  UI_EventNode node = {.v = event};
  UI_EventList events = {0};
  if(event.kind != UI_EventKind_Null) { events.first = events.last = &node; events.count = 1; }
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = mouse;
  UI_ScrollRegionSignal result = {0};
  UI_Font(fnt_tag_from_path(str8_lit("data/segoeui.ttf"))) UI_FontSize(16)
  {
    result = ui_scroll_region_build(ui_top_parent(), ui_key_make(1001), region, axes, UI_BoxFlag_Clickable|UI_BoxFlag_Scroll);
    *content_signal = ui_signal_from_box(result.content_box);
  }
  ui_end_build();
  return result;
}

internal B32
scroll_region_diagnostics(ScrollTestWindow *ws)
{
  U32 failures = 0;
#define ScrollCheck(condition, name) do { if(!(condition)) { fprintf(stderr, "FAIL: %s\n", name); failures += 1; } } while(0)
  UI_ScrollRegionParams params = {0};
  params.rect = r2f32p(20, 30, 220, 230);
  params.gutter_px = 20;
  params.overlay_rest_px = 7;
  params.overlay_hover_px = 14;
  params.overlay_inset_px = 3;
  params.axis[0] = params.axis[1] = UI_ScrollAxisPolicy_Auto;
  params.content_dim_px = v2f32(195, 205);
  UI_ScrollRegion region = ui_scroll_region_layout(params);
  ScrollCheck(region.bar_enabled[0] && region.bar_enabled[1], "vertical gutter induces horizontal overflow");
  ScrollCheck(region.viewport.x1 == 200 && region.viewport.y1 == 210, "both classic gutters reduce viewport");
  params.content_dim_px = v2f32(205, 195);
  region = ui_scroll_region_layout(params);
  ScrollCheck(region.bar_enabled[0] && region.bar_enabled[1], "horizontal gutter induces vertical overflow");
  params.content_dim_px = v2f32(200, 200);
  region = ui_scroll_region_layout(params);
  ScrollCheck(!region.bar_enabled[0] && !region.bar_enabled[1], "exact fit has no auto gutters");
  params.style = UI_ScrollBarStyle_Overlay;
  params.content_dim_px = v2f32(195, 205);
  region = ui_scroll_region_layout(params);
  ScrollCheck(!region.bar_enabled[0] && region.bar_enabled[1] && region.viewport.x1 == 220 && region.viewport.y1 == 230,
              "overlay doesn't introduce overflow on other axis");
  params.axis[0] = params.axis[1] = UI_ScrollAxisPolicy_Always;
  region = ui_scroll_region_layout(params);
  Rng2F32 xbar = ui_scroll_region_bar_rect(&region, Axis2_X, 14, 1);
  Rng2F32 ybar = ui_scroll_region_bar_rect(&region, Axis2_Y, 14, 1);
  ScrollCheck(xbar.x1 < ybar.x0 && ybar.y1 < xbar.y0, "expanded overlays have separate corner hit areas");
  params.rect = r2f32p(10, 10, 12, 11);
  for(U32 style = 0; style < 2; style += 1)
  {
    params.style = (UI_ScrollBarStyle)style;
    region = ui_scroll_region_layout(params);
    ScrollCheck(region.viewport.x1 >= region.viewport.x0 && region.viewport.y1 >= region.viewport.y0, "tiny viewport stays nonnegative");
    for EachEnumVal(Axis2, axis)
    {
      Rng2F32 bar = ui_scroll_region_bar_rect(&region, axis, 14, 1);
      ScrollCheck(bar.x0 >= 10 && bar.x1 <= 12 && bar.y0 >= 10 && bar.y1 <= 11 && bar.x1 >= bar.x0 && bar.y1 >= bar.y0,
                  "tiny bar remains within bounds");
    }
  }

  UI_State *saved = ui_state;
  // Exercise the list's public begin/end boundary with both styles. A style
  // change must affect its viewport, not discard its fractional scroll state.
  for(U32 style = 0; style < 2; style += 1)
  {
    UI_State *test_ui = ui_state_alloc();
    ui_select_state(test_ui);
    ui_set_active_scroll_bar_style((UI_ScrollBarStyle)style);
    UI_ScrollPt pt = ui_scroll_pt(4, 0);
    pt.target_off = 0.5f;
    for(U32 frame = 0; frame < 3; frame += 1)
    {
      UI_EventList events = {0};
      UI_EventNode wheel = {.v = {.kind = UI_EventKind_Scroll, .pos = {350, 250}, .delta_2f32 = {0, 5}, .scroll_is_precise = 1}};
      if(frame == 2) { events.first = events.last = &wheel; events.count = 1; }
      UI_IconInfo icons = {0};
      UI_AnimationInfo animation = {0};
      animation.scroll_animation_rate = animation.hot_animation_rate = 1.f;
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      ui_state->mouse = v2f32(350, 250);
      UI_Font(fnt_tag_from_path(str8_lit("data/segoeui.ttf"))) UI_FontSize(16)
      {
        UI_ScrollListParams list = {0};
        list.dim_px = v2f32(200, 100);
        list.row_height_px = 20;
        list.item_range = r1s64(0, 100);
        list.precise_scroll = 1;
        Rng1S64 rows = {0};
        ui_set_next_fixed_x(300);
        ui_set_next_fixed_y(200);
        ui_scroll_list_begin(&list, &pt, 0, 0, &rows, 0);
        UI_Box *content = ui_top_parent();
        if(frame == 1)
        {
          UI_Key bar_key = ui_key_from_stringf(content->key, "scroll_region_bar_%i", Axis2_Y);
          ScrollCheck(!ui_box_is_nil(ui_box_from_key(bar_key)), "hovering list shows its scrollbar");
        }
        ScrollCheck(content->view_off.y == 10 && rows.min == 4, "list keeps fractional row offset");
        ui_scroll_list_end();
        ui_end_build();
        ScrollCheck(dim_2f32(content->rect).x == (style ? 200 : 176), "list viewport follows scrollbar style");
        ScrollCheck(pt.idx == 4 && pt.target_off == (frame == 2 ? 0.75f : 0.5f), "list preserves fractions and consumes precise wheel once");
        ScrollCheck(events.count == 0, "list consumes its wheel event");
      }
    }
    ui_state_release(test_ui);
  }
  ui_set_active_scroll_bar_style(UI_ScrollBarStyle_Classic);
  for(U32 style = 0; style < 2; style += 1)
  {
    UI_State *test_ui = ui_state_alloc();
    ui_select_state(test_ui);
    params.rect = r2f32p(100, 100, 400, 300);
    params.style = (UI_ScrollBarStyle)style;
    region = ui_scroll_region_layout(params);
    UI_ScrollRegionAxis axes[Axis2_COUNT] = {0};
    axes[0] = (UI_ScrollRegionAxis){ui_scroll_pt(0, 0), r1s64(0, 600), 300};
    axes[1] = (UI_ScrollRegionAxis){ui_scroll_pt(0, 0), r1s64(0, 600), 200};
    UI_Signal content = {0};
    UI_Event none = {0};
    Vec2F32 mouse = v2f32(200, 180);
    for(U32 frame = 0; frame < 3; frame += 1)
    {
      scroll_test_frame(ws, &region, axes, mouse, none, &content);
    }
    for EachEnumVal(Axis2, axis)
    {
      UI_Key bar_key = ui_key_from_stringf(ui_key_make(1001), "scroll_region_bar_%i", axis);
      UI_Key track_key = ui_key_from_stringf(bar_key, "##_scroll_area_%i", axis);
      UI_Key thumb_key = ui_key_from_stringf(track_key, "##_scroller_%i", axis);
      UI_Box *thumb = ui_box_from_key(thumb_key);
      ScrollCheck(!ui_box_is_nil(thumb), "thumb built for each axis/style");
      if(ui_box_is_nil(thumb)) { continue; }
      Vec2F32 press = scale_2f32(add_2f32(thumb->rect.p0, thumb->rect.p1), 0.5f);
      // Settle hover expansion before the press.
      for(U32 frame = 0; frame < 3; frame += 1)
      {
        scroll_test_frame(ws, &region, axes, press, none, &content);
      }
      thumb = ui_box_from_key(thumb_key);
      press = scale_2f32(add_2f32(thumb->rect.p0, thumb->rect.p1), 0.5f);
      UI_Box *track = thumb->parent;
      ScrollCheck(thumb->rect.x0 >= track->rect.x0 && thumb->rect.x1 <= track->rect.x1 &&
                  thumb->rect.y0 >= track->rect.y0 && thumb->rect.y1 <= track->rect.y1, "thumb lies within track");
      F32 expected_length = dim_2f32(track->rect).v[axis] * axes[axis].visible / (600.f + axes[axis].visible);
      ScrollCheck(abs_f32(dim_2f32(thumb->rect).v[axis] - expected_length) <= 2.f, "thumb length reflects visible fraction on both styles");
      UI_Event event = {.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = press};
      scroll_test_frame(ws, &region, axes, press, event, &content);
      ScrollCheck(ui_key_match(ui_active_key(UI_MouseButtonKind_Left), thumb_key) && !ui_pressed(content), "thumb takes press above content");
      Vec2F32 outside = v2f32(1000, 1000);
      UI_ScrollRegionSignal drag = scroll_test_frame(ws, &region, axes, outside, none, &content);
      ScrollCheck(drag.position.v[axis].idx == 600 && drag.position.v[axis2_flip(axis)].idx == 0, "drag outside moves only selected axis to its limit");
      for(U32 frame = 0; frame < 4; frame += 1)
      {
        drag = scroll_test_frame(ws, &region, axes, outside, none, &content);
      }
      ScrollCheck(ui_key_match(ui_active_key(UI_MouseButtonKind_Left), thumb_key), "drag survives pointer leaving region and fade");
      event = (UI_Event){.kind = UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = outside};
      scroll_test_frame(ws, &region, axes, outside, event, &content);
      ScrollCheck(ui_key_match(ui_active_key(UI_MouseButtonKind_Left), ui_key_zero()), "release outside ends drag");
      for(U32 frame = 0; frame < 3; frame += 1)
      {
        scroll_test_frame(ws, &region, axes, mouse, none, &content);
      }
    }
    UI_Event wheel = {.kind = UI_EventKind_Scroll, .pos = mouse, .delta_2f32 = {0, 30}};
    scroll_test_frame(ws, &region, axes, mouse, wheel, &content);
    ScrollCheck(content.scroll.y == 1 && content.scroll.x == 0, "content receives wheel exactly once");
    wheel.modifiers = WM_Modifier_Shift;
    scroll_test_frame(ws, &region, axes, mouse, wheel, &content);
    ScrollCheck(content.scroll.x == 1 && content.scroll.y == 0, "content retains Shift-wheel axis routing");
    // A half-unit position must be visible in the thumb and survive a press.
    axes[1] = (UI_ScrollRegionAxis){ui_scroll_pt(0, 0), r1s64(0, 4), 1};
    UI_Key fraction_bar = ui_key_from_stringf(ui_key_make(1001), "scroll_region_bar_%i", Axis2_Y);
    UI_Key fraction_track = ui_key_from_stringf(fraction_bar, "##_scroll_area_%i", Axis2_Y);
    UI_Key fraction_thumb = ui_key_from_stringf(fraction_track, "##_scroller_%i", Axis2_Y);
    for(U32 frame = 0; frame < 3; frame++)
    {
      scroll_test_frame(ws, &region, axes, mouse, none, &content);
    }
    F32 integer_start = ui_box_from_key(fraction_thumb)->rect.y0;
    axes[1].position.target_off = 0.5f;
    for(U32 frame = 0; frame < 3; frame++)
    {
      scroll_test_frame(ws, &region, axes, mouse, none, &content);
    }
    UI_Box *fraction_box = ui_box_from_key(fraction_thumb);
    ScrollCheck(fraction_box->rect.y0 > integer_start + 5, "fractional position moves the thumb");
    Vec2F32 press = center_2f32(fraction_box->rect);
    for(U32 frame = 0; frame < 3; frame++)
    {
      scroll_test_frame(ws, &region, axes, press, none, &content);
    }
    press = center_2f32(ui_box_from_key(fraction_thumb)->rect);
    UI_Event press_event = {.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = press};
    UI_ScrollRegionSignal pressed = scroll_test_frame(ws, &region, axes, press, press_event, &content);
    ScrollCheck(pressed.position.y.idx == 0 && pressed.position.y.target_off == 0.5f,
                "press without movement preserves fractional position");
    UI_Event release_event = {.kind = UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = press};
    scroll_test_frame(ws, &region, axes, press, release_event, &content);
    // No scroll range: overlays disappear; classic keeps disabled furniture.
    axes[0].range.max = axes[1].range.max = 0;
    scroll_test_frame(ws, &region, axes, mouse, none, &content);
    UI_Key bar_key = ui_key_from_stringf(ui_key_make(1001), "scroll_region_bar_%i", Axis2_Y);
    UI_Box *bar = ui_box_from_key(bar_key);
    B32 touched = !ui_box_is_nil(bar) && bar->last_touched_build_index == test_ui->build_index-1;
    ScrollCheck(touched == (style == UI_ScrollBarStyle_Classic), "empty range follows style visibility policy");
    ui_select_state(saved);
    ui_state_release(test_ui);
  }
  fprintf(stderr, "Scroll region diagnostics: %u failures\n", failures);
#undef ScrollCheck
  return failures == 0;
}

internal void
entry_point(CmdLine *cmdline)
{
  fnt_frame();
  dr_begin_frame(fnt_tag_zero());
  UI_Theme theme = {0};
  ScrollTestWindow window = {.ui = ui_state_alloc(), .theme = &theme};
  ui_select_state(window.ui);
  B32 pass = scroll_region_diagnostics(&window);
  ui_state_release(window.ui);
  exit(!pass);
}
