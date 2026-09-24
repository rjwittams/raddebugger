#if R_BACKEND == R_BACKEND_METAL
Test(metal_blur_kernel_bounds)
{
  F32 sizes[] = {64, 0, 1, 2, 3, 10, 32, 62, 62.49f, 62.5f, 63, 63.49f, 63.5f, 128};
  R_PassParams_Blur params = {0};
  params.rect = r2f32p(1, 2, 80, 90);
  params.corner_radii[0] = 4;
  Vec2F32 viewport = v2f32(100, 100);
  for(U64 size_idx = 0; size_idx < ArrayCount(sizes); size_idx++)
  {
    params.blur_size = sizes[size_idx];
    R_MTL_BlurUniforms uniforms = r_mtl_blur_uniforms_from_params(&params, viewport);
    TestCheck(uniforms.blur_count >= 1 && uniforms.blur_count <= ArrayCount(uniforms.kernel));
    TestCheck(MemoryMatch(&uniforms.rect, &params.rect, sizeof(params.rect)));
    TestCheck(uniforms.corner_radii.x == 4 && uniforms.viewport_size.x == 100);
    for(U32 idx = 0; idx < Min(uniforms.blur_count, ArrayCount(uniforms.kernel)); idx++)
    {
      TestCheck(uniforms.kernel[idx].x >= 0 && uniforms.kernel[idx].x <= 1);
      TestCheck(uniforms.kernel[idx].y >= 0 && uniforms.kernel[idx].y <= 62);
    }
    if(sizes[size_idx] >= 63)
    {
      params.blur_size = 63;
      R_MTL_BlurUniforms capped = r_mtl_blur_uniforms_from_params(&params, viewport);
      TestCheck(MemoryMatch(&uniforms, &capped, sizeof(capped)));
    }
  }
}
#endif
