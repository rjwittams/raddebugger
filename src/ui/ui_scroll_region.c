// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal UI_ScrollRegionParams
ui_scroll_region_params(Rng2F32 rect, UI_ScrollAxisPolicy x, UI_ScrollAxisPolicy y)
{
  F32 em = ui_top_font_size();
  UI_ScrollRegionParams params = {0};
  params.rect = rect;
  params.style = ui_active_scroll_bar_style();
  params.axis[Axis2_X] = x;
  params.axis[Axis2_Y] = y;
  params.gutter_px = floor_f32(em*1.5f);
  params.overlay_rest_px = em*0.45f;
  params.overlay_hover_px = em*0.9f;
  params.overlay_inset_px = floor_f32(em*0.2f);
  return params;
}

internal UI_ScrollRegion
ui_scroll_region_layout(UI_ScrollRegionParams params)
{
  UI_ScrollRegion region = {0};
  region.params = params;
  // Normalize tiny/empty rectangles before subtracting gutters.
  for EachEnumVal(Axis2, axis)
  {
    region.params.rect.p1.v[axis] = Max(params.rect.p0.v[axis], params.rect.p1.v[axis]);
    region.bar_enabled[axis] = params.axis[axis] == UI_ScrollAxisPolicy_Always;
  }
  // Auto bars only become enabled within this calculation. A gutter on one
  // axis can cause overflow on the other; at most two additions are possible.
  for(U32 pass = 0; pass < 3; pass += 1)
  {
    region.viewport = region.params.rect;
    for EachEnumVal(Axis2, axis)
    {
      Axis2 cross = axis2_flip(axis);
      F32 gutter = (params.style == UI_ScrollBarStyle_Classic && region.bar_enabled[cross]) ? Max(0.f, params.gutter_px) : 0.f;
      region.viewport.p1.v[axis] = Max(region.viewport.p0.v[axis], region.viewport.p1.v[axis] - gutter);
    }
    for EachEnumVal(Axis2, axis)
    {
      if(params.axis[axis] == UI_ScrollAxisPolicy_Auto &&
         params.content_dim_px.v[axis] > dim_2f32(region.viewport).v[axis])
      {
        region.bar_enabled[axis] = 1;
      }
    }
  }
  return region;
}

internal Rng2F32
ui_scroll_region_bar_rect(UI_ScrollRegion *region, Axis2 axis, F32 thickness, B32 other_bar_visible)
{
  Axis2 cross = axis2_flip(axis);
  Rng2F32 rect = region->viewport;
  if(region->params.style == UI_ScrollBarStyle_Classic)
  {
    rect.p0.v[cross] = region->viewport.p1.v[cross];
    rect.p1.v[cross] = region->params.rect.p1.v[cross];
  }
  else
  {
    F32 inset = Max(0.f, region->params.overlay_inset_px);
    rect.p0.v[axis] += inset;
    rect.p1.v[axis] -= inset;
    // Reserve the expanded bar's footprint so hovering doesn't shift the
    // other thumb or create intersecting hit areas at the corner.
    if(other_bar_visible)
    {
      rect.p1.v[axis] -= Max(region->params.overlay_rest_px, region->params.overlay_hover_px) + inset;
    }
    rect.p1.v[cross] -= inset;
    rect.p0.v[cross] = rect.p1.v[cross] - Max(0.f, thickness);
  }
  for EachEnumVal(Axis2, a)
  {
    rect.p0.v[a] = Clamp(region->params.rect.p0.v[a], rect.p0.v[a], region->params.rect.p1.v[a]);
    rect.p1.v[a] = Clamp(rect.p0.v[a], rect.p1.v[a], region->params.rect.p1.v[a]);
  }
  return rect;
}

internal UI_ScrollRegionSignal
ui_scroll_region_build(UI_Box *parent, UI_Key key, UI_ScrollRegion *region,
                       UI_ScrollRegionAxis axes[Axis2_COUNT], UI_BoxFlags content_flags)
{
  UI_ScrollRegionSignal result = {0};
  B32 overlay = region->params.style == UI_ScrollBarStyle_Overlay;
  B32 visible[Axis2_COUNT] = {0};
  for EachEnumVal(Axis2, axis)
  {
    result.position.v[axis] = axes[axis].position;
    visible[axis] = region->bar_enabled[axis] && (!overlay || axes[axis].range.max > axes[axis].range.min);
  }
  UI_Parent(parent)
  {
    // RAD paints siblings in reverse build order. Both styles are built before
    // the content so their thumbs receive hits above that content.
    for EachEnumVal(Axis2, axis)
    {
      if(!visible[axis]) { continue; }
      UI_Key bar_key = ui_key_from_stringf(key, "scroll_region_bar_%i", axis);
      UI_Box *previous_bar = ui_box_from_key(bar_key);
      B32 dragging = 0;
      for(UI_Box *box = ui_box_from_key(ui_active_key(UI_MouseButtonKind_Left)); !ui_box_is_nil(box); box = box->parent)
      {
        if(box == previous_bar) { dragging = 1; break; }
      }
      F32 vis = 1.f;
      F32 thickness = region->params.gutter_px;
      if(overlay)
      {
        Rng2F32 hover_rect = shift_2f32(region->viewport, parent->rect.p0);
        Axis2 cross = axis2_flip(axis);
        B32 hovered = contains_2f32(hover_rect, ui_mouse());
        B32 near_bar = hovered && ui_mouse().v[cross] >= hover_rect.p1.v[cross] - region->params.overlay_hover_px - region->params.overlay_inset_px;
        vis = ui_anim(ui_key_from_string(bar_key, str8_lit("visibility")), hovered || dragging ? 1.f : 0.f,
                      .rate = ui_state->animation_info.scroll_animation_rate);
        F32 expand = ui_anim(ui_key_from_string(bar_key, str8_lit("expansion")), near_bar || dragging ? 1.f : 0.f,
                             .rate = ui_state->animation_info.hot_animation_rate);
        thickness = mix_1f32(region->params.overlay_rest_px, region->params.overlay_hover_px, expand);
      }
      if(vis <= 0.001f && !dragging) { continue; }
      Rng2F32 bar_rect = ui_scroll_region_bar_rect(region, axis, thickness, visible[axis2_flip(axis)]);
      Vec2F32 bar_dim = dim_2f32(bar_rect);
      if(bar_dim.x <= 0 || bar_dim.y <= 0) { continue; }
      UI_Focus(UI_FocusKind_Off)
      {
        ui_set_next_rect(bar_rect);
        ui_set_next_child_layout_axis(axis);
        UI_Box *bar = ui_build_box_from_key(UI_BoxFlag_Floating, bar_key);
        UI_Parent(bar)
        {
          // Fixed geometry applies to the widget container only. A scoped
          // fixed size/position would also force every thumb and track child
          // to that rectangle, bypassing their proportional layout.
          ui_set_next_fixed_width(bar_dim.x);
          ui_set_next_fixed_height(bar_dim.y);
          result.position.v[axis] = ui_scroll_bar_styled(axis, ui_px(bar_dim.v[axis2_flip(axis)], 1.f),
                                                        region->params.style, vis, axes[axis].position,
                                                        axes[axis].range, axes[axis].visible);
        }
      }
    }
    UI_Rect(region->viewport)
    {
      result.content_box = ui_build_box_from_key(content_flags|UI_BoxFlag_Clip, key);
    }
  }
  return result;
}
