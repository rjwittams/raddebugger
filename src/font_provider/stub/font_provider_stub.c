// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

fp_hook void
fp_init(void)
{
}

fp_hook FP_Handle
fp_font_open(String8 path)
{
  FP_Handle result = {0};
  return result;
}

fp_hook FP_Handle
fp_font_open_from_static_data_string(String8 *data_ptr)
{
  FP_Handle result = {0};
  return result;
}

fp_hook void
fp_font_close(FP_Handle handle)
{
}

fp_hook FP_Metrics
fp_metrics_from_font(FP_Handle font)
{
  FP_Metrics result = {0};
  result.design_units_per_em = 1;
  return result;
}

fp_hook ASAN_NO_ADDR FP_RasterResult
fp_raster(Arena *arena, FP_Handle font, F32 size, FP_RasterFlags flags, String8 string)
{
  FP_RasterResult result = {0};
  return result;
}
