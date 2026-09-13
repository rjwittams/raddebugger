// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BUILD_CONSOLE_INTERFACE 1
#define R_BACKEND R_BACKEND_STUB
#define WM_STUB 1
#include "base/base_inc.h"
#include "window_manager/window_manager_inc.h"
#include "render/render_inc.h"
#include "font_provider/font_provider_inc.h"
#include "font_cache/font_cache.h"
#include "draw/draw.h"
#include "base/base_inc.c"
#include "window_manager/window_manager_inc.c"
#include "render/render_inc.c"
#include "font_provider/font_provider_inc.c"
#include "font_cache/font_cache.c"
#include "draw/draw.c"

internal void
entry_point(CmdLine *cmdline)
{
  fnt_frame();
  dr_begin_frame(fnt_tag_zero());
  String8 fonts[] = {str8_lit("data/segoeui.ttf"),
                    str8_lit("data/Inconsolata-Regular.ttf"),
                    str8_lit("data/JetBrainsMono-Regular.ttf")};
  F32 sizes[] = {13, 17, 17.5f};
  FNT_RasterFlags flags[] = {0, FNT_RasterFlag_Smooth, FNT_RasterFlag_Hinted};
  String8 samples[] = {str8_lit("/M"), str8_lit("/MMMMMMMMMMMMMMMM"),
                      str8_lit("/iiiiiiiiiiiiiiii"), str8_lit("/Users/robert/")};
  U32 failures = 0;
  U32 cases = 0;
  for(U64 font_idx = 0; font_idx < ArrayCount(fonts); font_idx += 1)
  {
    FNT_Tag font = fnt_tag_from_path(fonts[font_idx]);
    for(U64 size_idx = 0; size_idx < ArrayCount(sizes); size_idx += 1)
    for(U64 flags_idx = 0; flags_idx < ArrayCount(flags); flags_idx += 1)
    for(U64 sample_idx = 0; sample_idx < ArrayCount(samples); sample_idx += 1)
    for(U32 scale = 1; scale <= 2; scale += 1)
    {
      F32 size = sizes[size_idx];
      String8 string = samples[sample_idx];
      dr_set_raster_scale((F32)scale);
      FNT_Run drawn = dr_fnt_run_from_string(font, size, 0, 0, flags[flags_idx], string);
      B32 pass = (drawn.dim.x > 0 && drawn.pieces.count > 0);
      F32 drawn_x = 0;
      U64 offset = 0;
      for(U64 piece_idx = 0; piece_idx <= drawn.pieces.count; piece_idx += 1)
      {
        F32 measured_x = fnt_dim_from_tag_size_string(font, size, 0, 0, str8_prefix(string, offset)).x;
        U64 hit = fnt_char_pos_from_tag_size_string_p(font, size, 0, 0, string, drawn_x);
        pass = pass && abs_f32(measured_x - drawn_x) < 0.01f && hit == offset;
        if(piece_idx < drawn.pieces.count)
        {
          drawn_x += drawn.pieces.v[piece_idx].advance;
          offset += drawn.pieces.v[piece_idx].decode_size;
        }
      }
      pass = pass && offset == string.size;
      cases += 1;
      failures += !pass;
      if(!pass)
      {
        F32 measured_x = fnt_dim_from_tag_size_string(font, size, 0, 0, string).x;
        fprintf(stderr, "FAIL font=%.*s size=%.1f flags=%u scale=%u text=%.*s measured=%.3f drawn=%.3f\n",
                str8_varg(fonts[font_idx]), size, flags[flags_idx], scale,
                str8_varg(string), measured_x, drawn.dim.x);
      }
    }
  }
  printf("Text positions: %u cases, %u failures\n", cases, failures);
  exit(failures != 0);
}
