// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal MAC_FP_Font *
mac_fp_font_from_handle(FP_Handle handle)
{
  MAC_FP_Font *result = (MAC_FP_Font *)handle.u64[0];
  return result;
}

internal FP_Handle
mac_fp_handle_from_font(MAC_FP_Font *font)
{
  FP_Handle result = {(U64)font};
  return result;
}

internal MAC_FP_Font *
mac_fp_font_alloc(void)
{
  MAC_FP_Font *font = mac_fp_state->free_font;
  if(font != 0)
  {
    SLLStackPop(mac_fp_state->free_font);
    MemoryZeroStruct(font);
  }
  else
  {
    font = push_array(mac_fp_state->arena, MAC_FP_Font, 1);
  }
  return font;
}

internal void
mac_fp_font_release(MAC_FP_Font *font)
{
  if(font != 0)
  {
    if(font->cg_font != 0)
    {
      CGFontRelease(font->cg_font);
    }
    if(font->provider != 0)
    {
      CGDataProviderRelease(font->provider);
    }
    MemoryZeroStruct(font);
    SLLStackPush(mac_fp_state->free_font, font);
  }
}

fp_hook void
fp_init(void)
{
  Arena *arena = arena_alloc();
  mac_fp_state = push_array(arena, MAC_FP_State, 1);
  mac_fp_state->arena = arena;
}

fp_hook FP_Handle
fp_font_open(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  String8 path_copy = push_str8_copy(scratch.arena, path);
  MAC_FP_Font *font = mac_fp_font_alloc();
  font->provider = CGDataProviderCreateWithFilename((char *)path_copy.str);
  if(font->provider != 0)
  {
    font->cg_font = CGFontCreateWithDataProvider(font->provider);
  }
  if(font->cg_font == 0)
  {
    mac_fp_font_release(font);
    font = 0;
  }
  FP_Handle result = mac_fp_handle_from_font(font);
  scratch_end(scratch);
  return result;
}

fp_hook FP_Handle
fp_font_open_from_static_data_string(String8 *data_ptr)
{
  MAC_FP_Font *font = mac_fp_font_alloc();
  if(data_ptr != 0 && data_ptr->str != 0 && data_ptr->size != 0)
  {
    font->provider = CGDataProviderCreateWithData(0, data_ptr->str, data_ptr->size, 0);
    if(font->provider != 0)
    {
      font->cg_font = CGFontCreateWithDataProvider(font->provider);
    }
  }
  if(font->cg_font == 0)
  {
    mac_fp_font_release(font);
    font = 0;
  }
  FP_Handle result = mac_fp_handle_from_font(font);
  return result;
}

fp_hook void
fp_font_close(FP_Handle handle)
{
  MAC_FP_Font *font = mac_fp_font_from_handle(handle);
  mac_fp_font_release(font);
}

fp_hook FP_Metrics
fp_metrics_from_font(FP_Handle handle)
{
  MAC_FP_Font *font = mac_fp_font_from_handle(handle);
  FP_Metrics result = {0};
  if(font != 0 && font->cg_font != 0)
  {
    result.design_units_per_em = (F32)CGFontGetUnitsPerEm(font->cg_font);
    result.ascent              = (F32)CGFontGetAscent(font->cg_font);
    result.descent             = abs_f32((F32)CGFontGetDescent(font->cg_font));
    result.line_gap            = (F32)CGFontGetLeading(font->cg_font);
    result.capital_height      = (F32)CGFontGetCapHeight(font->cg_font);
  }
  return result;
}

fp_hook ASAN_NO_ADDR FP_RasterResult
fp_raster(Arena *arena, FP_Handle handle, F32 size, FP_RasterFlags flags, String8 string)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(&arena, 1);
  MAC_FP_Font *font = mac_fp_font_from_handle(handle);
  FP_RasterResult result = {0};
  if(font != 0 && font->cg_font != 0)
  {
    F32 pixel_size = (96.f/72.f) * size;
    CTFontRef ct_font = CTFontCreateWithGraphicsFont(font->cg_font, pixel_size, 0, 0);
    if(ct_font != 0)
    {
      String32 string32 = str32_from_8(scratch.arena, string);
      U64 glyph_count = string32.size;
      UniChar *characters = push_array_no_zero(scratch.arena, UniChar, glyph_count);
      CGGlyph *glyphs = push_array_no_zero(scratch.arena, CGGlyph, glyph_count);
      CGSize *advances = push_array_no_zero(scratch.arena, CGSize, glyph_count);
      CGPoint *positions = push_array_no_zero(scratch.arena, CGPoint, glyph_count);
      for(U64 idx = 0; idx < glyph_count; idx += 1)
      {
        U32 cp = string32.str[idx];
        characters[idx] = (UniChar)(cp <= 0xffff ? cp : '?');
      }

      B32 got_glyphs = CTFontGetGlyphsForCharacters(ct_font, characters, glyphs, glyph_count);
      if(got_glyphs)
      {
        CTFontGetAdvancesForGlyphs(ct_font, kCTFontOrientationDefault, glyphs, advances, glyph_count);

        F32 ascent = (F32)ceil_f64(CTFontGetAscent(ct_font));
        F32 descent = (F32)ceil_f64(CTFontGetDescent(ct_font));
        F32 line_gap = (F32)ceil_f64(CTFontGetLeading(ct_font));
        F32 advance = 0;
        for(U64 idx = 0; idx < glyph_count; idx += 1)
        {
          positions[idx] = CGPointMake(advance, descent + line_gap);
          advance += (F32)advances[idx].width;
        }

        Vec2S16 atlas_dim = {(S16)ceil_f32(advance + 2.f), (S16)ceil_f32(ascent + descent + line_gap + 2.f)};
        if(atlas_dim.x > 0 && atlas_dim.y > 0)
        {
          U64 atlas_size = (U64)atlas_dim.x * (U64)atlas_dim.y * 4;
          U8 *atlas = push_array(arena, U8, atlas_size);
          CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
          CGContextRef ctx = CGBitmapContextCreate(atlas, atlas_dim.x, atlas_dim.y, 8,
                                                   (size_t)atlas_dim.x*4, color_space,
                                                   kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
          if(ctx != 0)
          {
            B32 smooth = !!(flags & FP_RasterFlag_Smooth);
            B32 hinted = !!(flags & FP_RasterFlag_Hinted);
            CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
            CGContextSetRGBFillColor(ctx, 1.f, 1.f, 1.f, 1.f);
            CGContextSetAllowsAntialiasing(ctx, 1);
            CGContextSetShouldAntialias(ctx, 1);
            CGContextSetAllowsFontSmoothing(ctx, 1);
            CGContextSetShouldSmoothFonts(ctx, smooth);
            CGContextSetAllowsFontSubpixelPositioning(ctx, 1);
            CGContextSetShouldSubpixelPositionFonts(ctx, !hinted);
            CGContextSetAllowsFontSubpixelQuantization(ctx, 1);
            CGContextSetShouldSubpixelQuantizeFonts(ctx, hinted);
            CTFontDrawGlyphs(ct_font, glyphs, positions, glyph_count, ctx);

            U64 alpha_sum = 0;
            for(U64 idx = 3; idx < atlas_size; idx += 4)
            {
              alpha_sum += atlas[idx];
            }
            if(alpha_sum != 0)
            {
              result.atlas_dim = atlas_dim;
              result.atlas = atlas;
            }
            result.advance = ceil_f32(advance);

            CGContextRelease(ctx);
          }
          if(color_space != 0)
          {
            CGColorSpaceRelease(color_space);
          }
        }
      }
      CFRelease(ct_font);
    }
  }
  scratch_end(scratch);
  ProfEnd();
  return result;
}
