#if R_BACKEND == R_BACKEND_METAL
Test(metal_blur_kernel_bounds)
{
  R_MTL_BlurUniforms uniforms = {0};
  // One centre entry leaves the remaining kernel entries for paired samples.
  F32 offset_max = 2.f*(ArrayCount(uniforms.kernel)-1);
  F32 size_max = offset_max+1.f;
  F32 sizes[] = {size_max+1.f, 0, 1, 2, 3, 10, (F32)ArrayCount(uniforms.kernel),
                 offset_max, offset_max+0.49f, offset_max+0.5f,
                 size_max, size_max+0.49f, size_max+0.5f, 2.f*(size_max+1.f)};
  R_PassParams_Blur params = {0};
  params.rect = r2f32p(1, 2, 80, 90);
  params.corner_radii[0] = 4;
  Vec2F32 viewport = v2f32(100, 100);
  for(U64 size_idx = 0; size_idx < ArrayCount(sizes); size_idx++)
  {
    params.blur_size = sizes[size_idx];
    uniforms = r_mtl_blur_uniforms_from_params(&params, viewport);
    TestCheck(uniforms.blur_count >= 1 && uniforms.blur_count <= ArrayCount(uniforms.kernel));
    TestCheck(MemoryMatch(&uniforms.rect, &params.rect, sizeof(params.rect)));
    TestCheck(uniforms.corner_radii.x == 4 && uniforms.viewport_size.x == 100);
    for(U32 idx = 0; idx < Min(uniforms.blur_count, ArrayCount(uniforms.kernel)); idx++)
    {
      TestCheck(uniforms.kernel[idx].x >= 0 && uniforms.kernel[idx].x <= 1);
      TestCheck(uniforms.kernel[idx].y >= 0 && uniforms.kernel[idx].y <= offset_max);
    }
    if(sizes[size_idx] >= size_max)
    {
      params.blur_size = size_max;
      R_MTL_BlurUniforms capped = r_mtl_blur_uniforms_from_params(&params, viewport);
      TestCheck(MemoryMatch(&uniforms, &capped, sizeof(capped)));
    }
  }
}
#endif
