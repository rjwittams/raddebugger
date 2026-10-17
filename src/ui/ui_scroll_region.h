// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef UI_SCROLL_REGION_H
#define UI_SCROLL_REGION_H

// Always reserves a classic gutter even without overflow. Use it for content
// whose extent depends on viewport width (wrapped text, terminal grids).
// Auto is for an independently measured content extent, in pixels.
typedef enum UI_ScrollAxisPolicy
{
  UI_ScrollAxisPolicy_Off,
  UI_ScrollAxisPolicy_Always,
  UI_ScrollAxisPolicy_Auto,
} UI_ScrollAxisPolicy;

typedef struct UI_ScrollRegionParams UI_ScrollRegionParams;
struct UI_ScrollRegionParams
{
  Rng2F32 rect; // available rectangle, local to the parent
  UI_ScrollBarStyle style;
  UI_ScrollAxisPolicy axis[Axis2_COUNT];
  Vec2F32 content_dim_px; // used only by Auto axes; not scroll-position units
  F32 gutter_px;
  F32 overlay_rest_px;
  F32 overlay_hover_px;
  F32 overlay_inset_px;
};

typedef struct UI_ScrollRegion UI_ScrollRegion;
struct UI_ScrollRegion
{
  UI_ScrollRegionParams params;
  Rng2F32 viewport;
  B32 bar_enabled[Axis2_COUNT];
};

typedef struct UI_ScrollRegionAxis UI_ScrollRegionAxis;
struct UI_ScrollRegionAxis
{
  UI_ScrollPt position;
  Rng1S64 range; // inclusive legal positions, NOT the content's index range
  S64 visible;  // visible extent, in the same units as range and position
};

typedef struct UI_ScrollRegionSignal UI_ScrollRegionSignal;
struct UI_ScrollRegionSignal
{
  UI_Box *content_box;
  UI_ScrollPt2 position; // requested positions; content owns their application
};

// Defaults read UI style/font metrics, never shell settings. Layout itself is
// pure, so content can measure against viewport before building the region.
internal UI_ScrollRegionParams ui_scroll_region_params(Rng2F32 rect, UI_ScrollAxisPolicy x, UI_ScrollAxisPolicy y);
internal UI_ScrollRegion ui_scroll_region_layout(UI_ScrollRegionParams params);
internal Rng2F32 ui_scroll_region_bar_rect(UI_ScrollRegion *region, Axis2 axis, F32 thickness, B32 other_bar_visible);
// Creates bars and a clipped content box together, in the required paint/hit
// order. key is the stable content identity. Does not consume wheel/keyboard
// input or change content offsets. Callers signal content_box themselves.
internal UI_ScrollRegionSignal ui_scroll_region_build(UI_Box *parent, UI_Key key, UI_ScrollRegion *region,
                                                      UI_ScrollRegionAxis axes[Axis2_COUNT], UI_BoxFlags content_flags);

#endif
