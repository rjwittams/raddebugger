// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal R_Handle
r_mtl_handle_from_window(R_MTL_Window *window)
{
  R_Handle result = {(U64)window};
  return result;
}

internal R_MTL_Window *
r_mtl_window_from_handle(R_Handle handle)
{
  R_MTL_Window *result = (R_MTL_Window *)handle.u64[0];
  return result;
}

internal R_Handle
r_mtl_handle_from_tex2d(R_MTL_Tex2D *texture)
{
  R_Handle result = {(U64)texture};
  return result;
}

internal R_MTL_Tex2D *
r_mtl_tex2d_from_handle(R_Handle handle)
{
  R_MTL_Tex2D *result = (R_MTL_Tex2D *)handle.u64[0];
  return result;
}

internal R_Handle
r_mtl_handle_from_buffer(R_MTL_Buffer *buffer)
{
  R_Handle result = {(U64)buffer};
  return result;
}

internal R_MTL_Buffer *
r_mtl_buffer_from_handle(R_Handle handle)
{
  R_MTL_Buffer *result = (R_MTL_Buffer *)handle.u64[0];
  return result;
}

internal MTLPixelFormat
r_mtl_pixel_format_from_tex2d_format(R_Tex2DFormat format)
{
  MTLPixelFormat result = MTLPixelFormatInvalid;
  switch(format)
  {
    case R_Tex2DFormat_R8:     { result = MTLPixelFormatR8Unorm; } break;
    case R_Tex2DFormat_RG8:    { result = MTLPixelFormatRG8Unorm; } break;
    case R_Tex2DFormat_RGBA8:  { result = MTLPixelFormatRGBA8Unorm; } break;
    case R_Tex2DFormat_BGRA8:  { result = MTLPixelFormatBGRA8Unorm; } break;
    case R_Tex2DFormat_R16:    { result = MTLPixelFormatR16Unorm; } break;
    case R_Tex2DFormat_RGBA16: { result = MTLPixelFormatRGBA16Unorm; } break;
    case R_Tex2DFormat_R32:    { result = MTLPixelFormatR32Float; } break;
    case R_Tex2DFormat_RG32:   { result = MTLPixelFormatRG32Float; } break;
    case R_Tex2DFormat_RGBA32: { result = MTLPixelFormatRGBA32Float; } break;
    default:                   { result = MTLPixelFormatRGBA8Unorm; } break;
  }
  return result;
}

internal U64
r_mtl_bytes_per_pixel_from_tex2d_format(R_Tex2DFormat format)
{
  U64 result = 4;
  switch(format)
  {
    case R_Tex2DFormat_R8:     { result = 1; } break;
    case R_Tex2DFormat_RG8:    { result = 2; } break;
    case R_Tex2DFormat_RGBA8:  { result = 4; } break;
    case R_Tex2DFormat_BGRA8:  { result = 4; } break;
    case R_Tex2DFormat_R16:    { result = 2; } break;
    case R_Tex2DFormat_RGBA16: { result = 8; } break;
    case R_Tex2DFormat_R32:    { result = 4; } break;
    case R_Tex2DFormat_RG32:   { result = 8; } break;
    case R_Tex2DFormat_RGBA32: { result = 16; } break;
    default:                   { result = 4; } break;
  }
  return result;
}

internal NSUInteger
r_mtl_resource_options_from_kind(R_ResourceKind kind)
{
  NSUInteger result = MTLResourceStorageModeShared;
  if(kind == R_ResourceKind_Static) { result = MTLResourceStorageModePrivate; }
  return result;
}

internal U64
r_mtl_blit_texture_bytes_per_row(U64 row_bytes)
{
  U64 result = AlignPow2(row_bytes, 256);
  return result;
}

internal id<MTLBuffer>
r_mtl_static_upload_buffer_from_size(U64 size)
{
  id<MTLBuffer> result = 0;
  if(size != 0)
  {
    if(r_mtl_state->static_upload_buffer == 0 || r_mtl_state->static_upload_buffer_cap < size)
    {
      [r_mtl_state->static_upload_buffer release];
      r_mtl_state->static_upload_buffer_cap = ClampBot(MB(1), size);
      r_mtl_state->static_upload_buffer = [r_mtl_state->device newBufferWithLength:r_mtl_state->static_upload_buffer_cap options:MTLResourceStorageModeShared];
      [r_mtl_state->static_upload_buffer setLabel:@"RAD static upload buffer"];
    }
    result = r_mtl_state->static_upload_buffer;
  }
  return result;
}

internal void
r_mtl_wait_for_upload_slot(U64 slot_idx)
{
  id<MTLCommandBuffer> command_buffer = r_mtl_state->upload_buffer_command_buffers[slot_idx];
  if(command_buffer != 0)
  {
    if(command_buffer.status != MTLCommandBufferStatusCompleted &&
       command_buffer.status != MTLCommandBufferStatusError)
    {
      fprintf(stderr, "raddebugger: Metal upload slot %llu still in flight; waiting before reuse.\n", slot_idx);
      [command_buffer waitUntilCompleted];
    }
    [command_buffer release];
    r_mtl_state->upload_buffer_command_buffers[slot_idx] = 0;
  }
}

internal void
r_mtl_log_ns_error(char *context, NSError *error)
{
  if(error != 0)
  {
    fprintf(stderr, "raddebugger: Metal %s: %s\n", context, [[error localizedDescription] UTF8String]);
  }
}

internal void
r_mtl_log_command_buffer_error(id<MTLCommandBuffer> command_buffer)
{
  if(command_buffer.status == MTLCommandBufferStatusError)
  {
    NSError *error = command_buffer.error;
    if(error != 0)
    {
      fprintf(stderr, "raddebugger: Metal command buffer failure: %s\n", [[error localizedDescription] UTF8String]);
    }
    else
    {
      fprintf(stderr, "raddebugger: Metal command buffer failure.\n");
    }
  }
}

internal id<MTLRenderPipelineState>
r_mtl_render_pipeline_from_library_ex(id<MTLLibrary> library, NSString *vertex_name, NSString *fragment_name, MTLPixelFormat color_pixel_format, MTLPixelFormat depth_pixel_format, B32 blend)
{
  id<MTLRenderPipelineState> result = 0;
  id<MTLFunction> vertex_function = [library newFunctionWithName:vertex_name];
  id<MTLFunction> fragment_function = [library newFunctionWithName:fragment_name];
  if(vertex_function != 0 && fragment_function != 0)
  {
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = vertex_function;
    descriptor.fragmentFunction = fragment_function;
    descriptor.colorAttachments[0].pixelFormat = color_pixel_format;
    descriptor.depthAttachmentPixelFormat = depth_pixel_format;
    descriptor.colorAttachments[0].blendingEnabled = blend;
    if(blend)
    {
      descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
      descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
      descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
      descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
      descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
      descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorZero;
    }
    NSError *error = 0;
    result = [r_mtl_state->device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    r_mtl_log_ns_error((char *)[[NSString stringWithFormat:@"pipeline %@/%@", vertex_name, fragment_name] UTF8String], error);
    [descriptor release];
  }
  else
  {
    fprintf(stderr, "raddebugger: Metal missing shader function %s/%s\n", [vertex_name UTF8String], [fragment_name UTF8String]);
  }
  [vertex_function release];
  [fragment_function release];
  return result;
}

internal id<MTLRenderPipelineState>
r_mtl_render_pipeline_from_library(id<MTLLibrary> library, NSString *vertex_name, NSString *fragment_name, MTLPixelFormat pixel_format)
{
  id<MTLRenderPipelineState> result = r_mtl_render_pipeline_from_library_ex(library, vertex_name, fragment_name, pixel_format, MTLPixelFormatInvalid, 1);
  return result;
}

internal void
r_mtl_retire_object(id object)
{
  if(object != 0)
  {
    R_MTL_RetiredObject *node = r_mtl_state->free_retired_object;
    if(node != 0)
    {
      SLLStackPop(r_mtl_state->free_retired_object);
    }
    else
    {
      node = push_array_no_zero(r_mtl_state->arena, R_MTL_RetiredObject, 1);
    }
    node->object = object;
    SLLStackPush(r_mtl_state->retired_object[r_mtl_state->retire_idx], node);
  }
}

internal void
r_mtl_drain_retired_slot(U64 slot_idx)
{
  for(R_MTL_RetiredObject *node = r_mtl_state->retired_object[slot_idx], *next = 0; node != 0; node = next)
  {
    next = node->next;
    [node->object release];
    node->object = 0;
    SLLStackPush(r_mtl_state->free_retired_object, node);
  }
  r_mtl_state->retired_object[slot_idx] = 0;
  for(R_MTL_Tex2D *tex = r_mtl_state->retired_tex2d[slot_idx], *next = 0; tex != 0; tex = next)
  {
    next = tex->next;
    if(tex->texture != 0)
    {
      [tex->texture release];
    }
    MemoryZeroStruct(tex);
    SLLStackPush(r_mtl_state->free_tex2d, tex);
  }
  r_mtl_state->retired_tex2d[slot_idx] = 0;
  for(R_MTL_Buffer *buf = r_mtl_state->retired_buffer[slot_idx], *next = 0; buf != 0; buf = next)
  {
    next = buf->next;
    if(buf->buffer != 0)
    {
      [buf->buffer release];
    }
    MemoryZeroStruct(buf);
    SLLStackPush(r_mtl_state->free_buffer, buf);
  }
  r_mtl_state->retired_buffer[slot_idx] = 0;
}

internal void
r_mtl_window_resize_targets(R_MTL_Window *window)
{
  if(window != 0 && r_mtl_state->device != 0)
  {
    r_mtl_retire_object(window->stage_color);
    r_mtl_retire_object(window->stage_scratch_color);
    r_mtl_retire_object(window->geo3d_color);
    r_mtl_retire_object(window->geo3d_depth);

    S32 width = Max(window->drawable_size.x, 1);
    S32 height = Max(window->drawable_size.y, 1);
    MTLTextureDescriptor *stage_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                                width:width
                                                                                               height:height
                                                                                            mipmapped:NO];
    stage_descriptor.usage = MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    stage_descriptor.storageMode = MTLStorageModePrivate;
    window->stage_color = [r_mtl_state->device newTextureWithDescriptor:stage_descriptor];
    window->stage_scratch_color = [r_mtl_state->device newTextureWithDescriptor:stage_descriptor];
    [window->stage_color setLabel:@"RAD stage color"];
    [window->stage_scratch_color setLabel:@"RAD stage scratch color"];

    MTLTextureDescriptor *geo3d_color_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                                      width:width
                                                                                                     height:height
                                                                                                  mipmapped:NO];
    geo3d_color_descriptor.usage = MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    geo3d_color_descriptor.storageMode = MTLStorageModePrivate;
    window->geo3d_color = [r_mtl_state->device newTextureWithDescriptor:geo3d_color_descriptor];
    [window->geo3d_color setLabel:@"RAD Geo3D color"];

    MTLTextureDescriptor *geo3d_depth_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                                      width:width
                                                                                                     height:height
                                                                                                  mipmapped:NO];
    geo3d_depth_descriptor.usage = MTLTextureUsageRenderTarget;
    geo3d_depth_descriptor.storageMode = MTLStorageModePrivate;
    window->geo3d_depth = [r_mtl_state->device newTextureWithDescriptor:geo3d_depth_descriptor];
    [window->geo3d_depth setLabel:@"RAD Geo3D depth"];
  }
}

internal id<MTLBuffer>
r_mtl_upload_buffer_reserve(U64 size, U64 align, U64 *out_offset, void **out_ptr)
{
  id<MTLBuffer> result = 0;
  if(size != 0)
  {
    U64 idx = r_mtl_state->upload_buffer_idx % ArrayCount(r_mtl_state->upload_buffers);
    align = ClampBot(1, align);
    U64 pos = AlignPow2(r_mtl_state->upload_buffer_pos, align);
    U64 need_cap = pos + size;
    if(r_mtl_state->upload_buffers[idx] == 0 || r_mtl_state->upload_buffer_caps[idx] < need_cap)
    {
      U64 new_cap = ClampBot(MB(1), need_cap);
      r_mtl_retire_object(r_mtl_state->upload_buffers[idx]);
      r_mtl_state->upload_buffers[idx] = [r_mtl_state->device newBufferWithLength:new_cap options:MTLResourceStorageModeShared];
      [r_mtl_state->upload_buffers[idx] setLabel:@"RAD transient upload buffer"];
      r_mtl_state->upload_buffer_caps[idx] = new_cap;
    }
    result = r_mtl_state->upload_buffers[idx];
    r_mtl_state->upload_buffer_pos = pos + size;
    *out_offset = pos;
    *out_ptr = (U8 *)[result contents] + pos;
  }
  return result;
}

internal id<MTLBuffer>
r_mtl_upload_buffer(void *data, U64 size, U64 align, U64 *out_offset)
{
  void *ptr = 0;
  id<MTLBuffer> result = r_mtl_upload_buffer_reserve(size, align, out_offset, &ptr);
  if(ptr != 0)
  {
    MemoryCopy(ptr, data, size);
  }
  return result;
}

internal F32
r_mtl_contents_scale_from_window(WM_Window window)
{
  MAC_WM_Window *mac_window = mac_wm_window_from_handle(window);
  F32 result = 1.f;
  if(mac_window != 0)
  {
    result = (F32)[mac_window->ns_window backingScaleFactor];
  }
  result = ClampBot(1.f, result);
  return result;
}

internal Vec2S32
r_mtl_drawable_size_from_window(WM_Window window)
{
  Rng2F32 rect = wm_client_rect_from_window(window);
  F32 scale = r_mtl_contents_scale_from_window(window);
  Vec2S32 result =
  {
    (S32)ceil_f32((rect.x1 - rect.x0)*scale),
    (S32)ceil_f32((rect.y1 - rect.y0)*scale),
  };
  result.x = Max(result.x, 1);
  result.y = Max(result.y, 1);
  return result;
}

internal B32
r_mtl_scissor_from_clip(Rng2F32 clip, Vec2S32 drawable_size, F32 scale, MTLScissorRect *out)
{
  S32 x0 = Clamp(0, (S32)floor_f32(clip.x0*scale), drawable_size.x);
  S32 y0 = Clamp(0, (S32)floor_f32(clip.y0*scale), drawable_size.y);
  S32 x1 = Clamp(0, (S32)ceil_f32(clip.x1*scale), drawable_size.x);
  S32 y1 = Clamp(0, (S32)ceil_f32(clip.y1*scale), drawable_size.y);
  B32 result = (x0 < x1 && y0 < y1);
  if(result)
  {
    out->x = (NSUInteger)x0;
    out->y = (NSUInteger)y0;
    out->width = (NSUInteger)(x1 - x0);
    out->height = (NSUInteger)(y1 - y0);
  }
  return result;
}

internal R_MTL_BlurUniforms
r_mtl_blur_uniforms_from_params(R_PassParams_Blur *params, Vec2F32 viewport_dim)
{
  R_MTL_BlurUniforms result = {0};
  result.rect = params->rect;
  result.viewport_size = viewport_dim;
  MemoryCopyArray(result.corner_radii.v, params->corner_radii);

  F32 weights[ArrayCount(result.kernel)*2] = {0};
  F32 blur_size = Min(params->blur_size, ArrayCount(weights));
  U64 blur_count = (U64)round_f32(blur_size);
  F32 stdev = (blur_size-1.f)/2.f;
  F32 one_over_root_2pi_stdev2 = 1/sqrt_f32(2*pi32*stdev*stdev);
  F32 euler32 = 2.718281828459045f;
  weights[0] = 1.f;
  if(stdev > 0.f)
  {
    for(U64 idx = 0; idx < blur_count; idx += 1)
    {
      F32 kernel_x = (F32)idx;
      weights[idx] = one_over_root_2pi_stdev2*pow_f32(euler32, -kernel_x*kernel_x/(2.f*stdev*stdev));
    }
  }
  if(weights[0] > 1.f)
  {
    MemoryZeroArray(weights);
    weights[0] = 1.f;
  }
  else
  {
    for(U64 idx = 1; idx < blur_count; idx += 2)
    {
      F32 w0 = weights[idx + 0];
      F32 w1 = weights[idx + 1];
      F32 w = w0 + w1;
      F32 t = w1 / w;
      result.kernel[(idx+1)/2] = v4f32(w, (F32)idx + t, 0, 0);
    }
  }
  result.kernel[0].x = weights[0];
  result.blur_count = 1 + (U32)(blur_count / 2);
  return result;
}

r_hook void
r_init(CmdLine *cmdln)
{
  Arena *arena = arena_alloc();
  r_mtl_state = push_array(arena, R_MTL_State, 1);
  r_mtl_state->arena = arena;
  r_mtl_state->device_rw_mutex = rw_mutex_alloc();
  r_mtl_state->device = MTLCreateSystemDefaultDevice();
  if(r_mtl_state->device != 0)
  {
    r_mtl_state->command_queue = [r_mtl_state->device newCommandQueue];
    r_mtl_state->static_upload_buffer_cap = MB(1);
    r_mtl_state->static_upload_buffer = [r_mtl_state->device newBufferWithLength:r_mtl_state->static_upload_buffer_cap options:MTLResourceStorageModeShared];
    [r_mtl_state->static_upload_buffer setLabel:@"RAD static upload buffer"];
    r_mtl_state->command_buffer_completion_handler = [^(id<MTLCommandBuffer> completed_buffer)
    {
      r_mtl_log_command_buffer_error(completed_buffer);
    } copy];

    NSString *shader_source = [[NSString alloc] initWithBytes:r_mtl_g_shader_src.str length:r_mtl_g_shader_src.size encoding:NSUTF8StringEncoding];
      NSError *error = 0;
      id<MTLLibrary> library = [r_mtl_state->device newLibraryWithSource:shader_source options:0 error:&error];
      r_mtl_log_ns_error("library creation", error);
      if(library != 0)
      {
        r_mtl_state->rect_pipeline = r_mtl_render_pipeline_from_library(library, @"rect_vertex", @"rect_fragment", MTLPixelFormatRGBA16Float);
        r_mtl_state->blur_pipeline = r_mtl_render_pipeline_from_library(library, @"blur_vertex", @"blur_fragment", MTLPixelFormatRGBA16Float);
        r_mtl_state->mesh_pipeline = r_mtl_render_pipeline_from_library_ex(library, @"mesh_vertex", @"mesh_fragment", MTLPixelFormatRGBA8Unorm, MTLPixelFormatDepth32Float, 1);
        r_mtl_state->geo3d_composite_pipeline = r_mtl_render_pipeline_from_library(library, @"fullscreen_vertex", @"composite_fragment", MTLPixelFormatRGBA16Float);
        r_mtl_state->finalize_pipeline = r_mtl_render_pipeline_from_library(library, @"fullscreen_vertex", @"finalize_fragment", MTLPixelFormatBGRA8Unorm_sRGB);
        [library release];
      }

    MTLDepthStencilDescriptor *depth_stencil_descriptor = [MTLDepthStencilDescriptor new];
    depth_stencil_descriptor.depthCompareFunction = MTLCompareFunctionLess;
    depth_stencil_descriptor.depthWriteEnabled = YES;
    r_mtl_state->mesh_depth_stencil = [r_mtl_state->device newDepthStencilStateWithDescriptor:depth_stencil_descriptor];
    [depth_stencil_descriptor release];

    for EachIndex(idx, R_Tex2DSampleKind_COUNT)
    {
      MTLSamplerDescriptor *sampler_descriptor = [MTLSamplerDescriptor new];
      sampler_descriptor.sAddressMode = MTLSamplerAddressModeRepeat;
      sampler_descriptor.tAddressMode = MTLSamplerAddressModeRepeat;
      sampler_descriptor.minFilter = (idx == R_Tex2DSampleKind_Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest);
      sampler_descriptor.magFilter = (idx == R_Tex2DSampleKind_Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest);
      r_mtl_state->samplers[idx] = [r_mtl_state->device newSamplerStateWithDescriptor:sampler_descriptor];
      [sampler_descriptor release];
    }

    U32 white_pixels[] = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
    r_mtl_state->white_texture = r_mtl_tex2d_from_handle(r_tex2d_alloc(R_ResourceKind_Static, v2s32(2, 2), R_Tex2DFormat_RGBA8, white_pixels));
  }
}

r_hook R_Handle
r_window_equip(WM_Window window)
{
  R_MTL_Window *result = 0;
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    if(r_mtl_state->device != 0)
    {
      result = r_mtl_state->free_window;
      if(result != 0)
      {
        SLLStackPop(r_mtl_state->free_window);
      }
      else
      {
        result = push_array_no_zero(r_mtl_state->arena, R_MTL_Window, 1);
      }
      MemoryZeroStruct(result);

      MAC_WM_Window *mac_window = mac_wm_window_from_handle(window);
      NSView *content_view = [mac_window->ns_window contentView];
      result->layer = [CAMetalLayer layer];
      result->layer.device = r_mtl_state->device;
      result->layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
      result->layer.framebufferOnly = YES;
      result->stage_clear_pass = [MTLRenderPassDescriptor new];
      result->stage_pass = [MTLRenderPassDescriptor new];
      result->blur_pass = [MTLRenderPassDescriptor new];
      result->geo_pass = [MTLRenderPassDescriptor new];
      result->composite_pass = [MTLRenderPassDescriptor new];
      result->final_pass = [MTLRenderPassDescriptor new];
      result->contents_scale = r_mtl_contents_scale_from_window(window);
      result->layer.contentsScale = result->contents_scale;
      result->drawable_size = r_mtl_drawable_size_from_window(window);
      result->layer.drawableSize = CGSizeMake(result->drawable_size.x, result->drawable_size.y);
      r_mtl_window_resize_targets(result);
      [content_view setWantsLayer:YES];
      [content_view setLayer:result->layer];
    }
  }
  return r_mtl_handle_from_window(result);
}

r_hook void
r_window_unequip(WM_Window window, R_Handle window_equip)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Window *mtl_window = r_mtl_window_from_handle(window_equip);
    if(mtl_window != 0)
    {
      MAC_WM_Window *mac_window = mac_wm_window_from_handle(window);
      NSView *content_view = [mac_window->ns_window contentView];
      if([content_view layer] == mtl_window->layer)
      {
        [content_view setLayer:0];
      }
      r_mtl_retire_object(mtl_window->stage_color);
      r_mtl_retire_object(mtl_window->stage_scratch_color);
      r_mtl_retire_object(mtl_window->geo3d_color);
      r_mtl_retire_object(mtl_window->geo3d_depth);
      [mtl_window->stage_clear_pass release];
      [mtl_window->stage_pass release];
      [mtl_window->blur_pass release];
      [mtl_window->geo_pass release];
      [mtl_window->composite_pass release];
      [mtl_window->final_pass release];
      mtl_window->layer = 0;
      mtl_window->stage_color = 0;
      mtl_window->stage_scratch_color = 0;
      mtl_window->geo3d_color = 0;
      mtl_window->geo3d_depth = 0;
      mtl_window->stage_clear_pass = 0;
      mtl_window->stage_pass = 0;
      mtl_window->blur_pass = 0;
      mtl_window->geo_pass = 0;
      mtl_window->composite_pass = 0;
      mtl_window->final_pass = 0;
      SLLStackPush(r_mtl_state->free_window, mtl_window);
    }
  }
}

r_hook R_Handle
r_tex2d_alloc(R_ResourceKind kind, Vec2S32 size, R_Tex2DFormat format, void *data)
{
  R_Handle result = {0};
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_state->free_tex2d;
    if(texture != 0)
    {
      SLLStackPop(r_mtl_state->free_tex2d);
    }
    else
    {
      texture = push_array_no_zero(r_mtl_state->arena, R_MTL_Tex2D, 1);
    }
    MemoryZeroStruct(texture);
    texture->kind = kind;
    texture->format = format;
    texture->size = size;

    MTLPixelFormat pixel_format = r_mtl_pixel_format_from_tex2d_format(format);
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixel_format
                                                                                         width:Max(size.x, 1)
                                                                                        height:Max(size.y, 1)
                                                                                     mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = (kind == R_ResourceKind_Static ? MTLStorageModePrivate : MTLStorageModeShared);
    texture->texture = [r_mtl_state->device newTextureWithDescriptor:descriptor];
    if(data != 0 && texture->texture != 0)
    {
      U64 bytes_per_pixel = r_mtl_bytes_per_pixel_from_tex2d_format(texture->format);
      if(kind == R_ResourceKind_Static)
      {
        U64 row_bytes = size.x*bytes_per_pixel;
        U64 blit_row_bytes = r_mtl_blit_texture_bytes_per_row(row_bytes);
        U64 staging_size = blit_row_bytes*size.y;
        id<MTLBuffer> staging_buffer = r_mtl_static_upload_buffer_from_size(staging_size);
        if(staging_buffer != 0)
        {
          U8 *dst = (U8 *)[staging_buffer contents];
          U8 *src = (U8 *)data;
          for(S64 y = 0; y < size.y; y += 1)
          {
            MemoryCopy(dst + y*blit_row_bytes, src + y*row_bytes, row_bytes);
          }
          id<MTLCommandBuffer> command_buffer = [r_mtl_state->command_queue commandBuffer];
          id<MTLBlitCommandEncoder> encoder = [command_buffer blitCommandEncoder];
          MTLSize blit_size = MTLSizeMake(size.x, size.y, 1);
          MTLOrigin origin = MTLOriginMake(0, 0, 0);
          [encoder copyFromBuffer:staging_buffer
                      sourceOffset:0
                 sourceBytesPerRow:blit_row_bytes
               sourceBytesPerImage:staging_size
                        sourceSize:blit_size
                         toTexture:texture->texture
                  destinationSlice:0
                  destinationLevel:0
                 destinationOrigin:origin];
          [encoder endEncoding];
          [command_buffer commit];
          [command_buffer waitUntilCompleted];
          r_mtl_log_command_buffer_error(command_buffer);
        }
      }
      else
      {
        MTLRegion region = MTLRegionMake2D(0, 0, size.x, size.y);
        [texture->texture replaceRegion:region
                             mipmapLevel:0
                               withBytes:data
                             bytesPerRow:size.x*bytes_per_pixel];
      }
    }
    result = r_mtl_handle_from_tex2d(texture);
  }
  return result;
}

r_hook void
r_tex2d_release(R_Handle handle)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
    if(texture != 0)
    {
      SLLStackPush(r_mtl_state->retired_tex2d[r_mtl_state->retire_idx], texture);
    }
  }
}

r_hook R_ResourceKind
r_kind_from_tex2d(R_Handle handle)
{
  R_ResourceKind result = R_ResourceKind_Static;
  MutexScopeR(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
    if(texture != 0)
    {
      result = texture->kind;
    }
  }
  return result;
}

r_hook Vec2S32
r_size_from_tex2d(R_Handle handle)
{
  Vec2S32 result = {1, 1};
  MutexScopeR(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
    if(texture != 0)
    {
      result = texture->size;
    }
  }
  return result;
}

r_hook R_Tex2DFormat
r_format_from_tex2d(R_Handle handle)
{
  R_Tex2DFormat result = R_Tex2DFormat_RGBA8;
  MutexScopeR(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
    if(texture != 0)
    {
      result = texture->format;
    }
  }
  return result;
}

r_hook void
r_fill_tex2d_region(R_Handle handle, Rng2S32 subrect, void *data)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
    if(texture != 0 && texture->texture != 0 && texture->kind == R_ResourceKind_Dynamic && data != 0)
    {
      U64 bytes_per_pixel = r_mtl_bytes_per_pixel_from_tex2d_format(texture->format);
      MTLRegion region = MTLRegionMake2D(subrect.x0, subrect.y0, subrect.x1 - subrect.x0, subrect.y1 - subrect.y0);
      [texture->texture replaceRegion:region
                           mipmapLevel:0
                             withBytes:data
                           bytesPerRow:(subrect.x1 - subrect.x0)*bytes_per_pixel];
    }
  }
}

r_hook R_Handle
r_buffer_alloc(R_ResourceKind kind, U64 size, void *data)
{
  R_Handle result = {0};
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Buffer *buffer = r_mtl_state->free_buffer;
    if(buffer != 0)
    {
      SLLStackPop(r_mtl_state->free_buffer);
    }
    else
    {
      buffer = push_array_no_zero(r_mtl_state->arena, R_MTL_Buffer, 1);
    }
    MemoryZeroStruct(buffer);
    buffer->kind = kind;
    buffer->size = size;
    NSUInteger options = r_mtl_resource_options_from_kind(kind);
    if(kind == R_ResourceKind_Static && data != 0)
    {
      buffer->buffer = [r_mtl_state->device newBufferWithLength:size options:options];
      id<MTLBuffer> staging_buffer = r_mtl_static_upload_buffer_from_size(size);
      if(buffer->buffer != 0 && staging_buffer != 0)
      {
        MemoryCopy([staging_buffer contents], data, size);
        id<MTLCommandBuffer> command_buffer = [r_mtl_state->command_queue commandBuffer];
        id<MTLBlitCommandEncoder> encoder = [command_buffer blitCommandEncoder];
        [encoder copyFromBuffer:staging_buffer
                   sourceOffset:0
                       toBuffer:buffer->buffer
              destinationOffset:0
                           size:size];
        [encoder endEncoding];
        [command_buffer commit];
        [command_buffer waitUntilCompleted];
        r_mtl_log_command_buffer_error(command_buffer);
      }
    }
    else if(data != 0)
    {
      buffer->buffer = [r_mtl_state->device newBufferWithBytes:data length:size options:options];
    }
    else
    {
      buffer->buffer = [r_mtl_state->device newBufferWithLength:size options:options];
    }
    result = r_mtl_handle_from_buffer(buffer);
  }
  return result;
}

r_hook void
r_buffer_release(R_Handle handle)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Buffer *buffer = r_mtl_buffer_from_handle(handle);
    if(buffer != 0)
    {
      SLLStackPush(r_mtl_state->retired_buffer[r_mtl_state->retire_idx], buffer);
    }
  }
}

r_hook void
r_begin_frame(void)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    r_mtl_state->upload_buffer_idx += 1;
    r_mtl_state->retire_idx = r_mtl_state->upload_buffer_idx % ArrayCount(r_mtl_state->retired_object);
    r_mtl_wait_for_upload_slot(r_mtl_state->retire_idx);
    r_mtl_drain_retired_slot(r_mtl_state->retire_idx);
    r_mtl_state->upload_buffer_pos = 0;
  }
}

r_hook void
r_end_frame(void)
{
}

r_hook void
r_window_begin_frame(WM_Window window, R_Handle window_equip)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Window *mtl_window = r_mtl_window_from_handle(window_equip);
    if(mtl_window != 0)
    {
      F32 contents_scale = r_mtl_contents_scale_from_window(window);
      Vec2S32 drawable_size = r_mtl_drawable_size_from_window(window);
      if(contents_scale != mtl_window->contents_scale ||
         drawable_size.x != mtl_window->drawable_size.x ||
         drawable_size.y != mtl_window->drawable_size.y)
      {
        mtl_window->contents_scale = contents_scale;
        mtl_window->drawable_size = drawable_size;
        mtl_window->layer.contentsScale = contents_scale;
        mtl_window->layer.drawableSize = CGSizeMake(drawable_size.x, drawable_size.y);
        r_mtl_window_resize_targets(mtl_window);
      }
    }
  }
}

r_hook void
r_window_end_frame(WM_Window window, R_Handle window_equip)
{
}

r_hook void
r_window_submit(WM_Window window, R_Handle window_equip, R_PassList *passes)
{
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Window *mtl_window = r_mtl_window_from_handle(window_equip);
    if(mtl_window != 0 && r_mtl_state->command_queue != 0 && mtl_window->stage_color != 0)
    {
      id<CAMetalDrawable> drawable = [mtl_window->layer nextDrawable];
      if(drawable != 0)
      {
        id<MTLCommandBuffer> command_buffer = [r_mtl_state->command_queue commandBuffer];
        Rng2F32 viewport_rect = wm_client_rect_from_window(window);
        Vec2F32 viewport_dim = dim_2f32(viewport_rect);
        F32 scale = r_mtl_contents_scale_from_window(window);

      MTLRenderPassDescriptor *stage_clear_pass = mtl_window->stage_clear_pass;
      stage_clear_pass.colorAttachments[0].texture = mtl_window->stage_color;
      stage_clear_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      stage_clear_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      stage_clear_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.06, 0.065, 1.0);
      id<MTLRenderCommandEncoder> clear_encoder = [command_buffer renderCommandEncoderWithDescriptor:stage_clear_pass];
      [clear_encoder endEncoding];

      for(R_PassNode *pass_n = passes->first; pass_n != 0; pass_n = pass_n->next)
      {
        R_Pass *render_pass = &pass_n->v;
        switch(render_pass->kind)
        {
          default:{}break;
          case R_PassKind_UI:
          {
            if(r_mtl_state->rect_pipeline != 0)
            {
              MTLRenderPassDescriptor *stage_pass = mtl_window->stage_pass;
              stage_pass.colorAttachments[0].texture = mtl_window->stage_color;
              stage_pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
              stage_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
              id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:stage_pass];
              [encoder setRenderPipelineState:r_mtl_state->rect_pipeline];

              R_PassParams_UI *params = render_pass->params_ui;
              for(R_BatchGroup2DNode *group_n = params->rects.first; group_n != 0; group_n = group_n->next)
              {
                R_BatchList *batches = &group_n->batches;
                R_BatchGroup2DParams *group_params = &group_n->params;
                R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(group_params->tex);
                if(texture == 0 || texture->texture == 0)
                {
                  texture = r_mtl_state->white_texture;
                }
                if(texture != 0 && texture->texture != 0 && batches->byte_count != 0)
                {
                  U64 inst_count = batches->byte_count / batches->bytes_per_inst;
                  U64 insts_offset = 0;
                  void *insts_ptr = 0;
                  id<MTLBuffer> insts_buffer = r_mtl_upload_buffer_reserve(batches->byte_count, 16, &insts_offset, &insts_ptr);
                  U8 *insts = (U8 *)insts_ptr;
                  for(R_BatchNode *batch_n = batches->first; batch_n != 0; batch_n = batch_n->next)
                  {
                    MemoryCopy(insts, batch_n->v.v, batch_n->v.byte_count);
                    insts += batch_n->v.byte_count;
                  }
                  R_MTL_RectUniforms group_uniforms = {0};
                  group_uniforms.viewport_size = viewport_dim;
                  group_uniforms.opacity = 1.f - group_params->transparency;
                  group_uniforms.texture_sample_channel_map = r_sample_channel_map_from_tex2dformat(texture->format);
                  group_uniforms.texture_size = v2f32((F32)Max(texture->size.x, 1), (F32)Max(texture->size.y, 1));
                  Vec2F32 xform_2x2_col0 = v2f32(group_params->xform.v[0][0], group_params->xform.v[0][1]);
                  Vec2F32 xform_2x2_col1 = v2f32(group_params->xform.v[1][0], group_params->xform.v[1][1]);
                  group_uniforms.xform_scale = v2f32(length_2f32(xform_2x2_col0), length_2f32(xform_2x2_col1));
                  group_uniforms.xform[0] = v4f32(group_params->xform.v[0][0], group_params->xform.v[1][0], group_params->xform.v[2][0], 0);
                  group_uniforms.xform[1] = v4f32(group_params->xform.v[0][1], group_params->xform.v[1][1], group_params->xform.v[2][1], 0);
                  group_uniforms.xform[2] = v4f32(group_params->xform.v[0][2], group_params->xform.v[1][2], group_params->xform.v[2][2], 0);
                  U64 uniform_offset = 0;
                  id<MTLBuffer> uniform_buffer = r_mtl_upload_buffer(&group_uniforms, sizeof(group_uniforms), 256, &uniform_offset);
                  if(group_params->clip.x0 != 0 ||
                     group_params->clip.y0 != 0 ||
                     group_params->clip.x1 != 0 ||
                     group_params->clip.y1 != 0)
                  {
                    MTLScissorRect scissor = {0};
                    if(!r_mtl_scissor_from_clip(group_params->clip, mtl_window->drawable_size, scale, &scissor))
                    {
                      continue;
                    }
                    else
                    {
                      [encoder setScissorRect:scissor];
                    }
                  }
                  else
                  {
                    MTLScissorRect scissor = {0, 0, (NSUInteger)mtl_window->drawable_size.x, (NSUInteger)mtl_window->drawable_size.y};
                    [encoder setScissorRect:scissor];
                  }
                  [encoder setVertexBuffer:insts_buffer offset:insts_offset atIndex:0];
                  [encoder setVertexBuffer:uniform_buffer offset:uniform_offset atIndex:1];
                  [encoder setFragmentBuffer:uniform_buffer offset:uniform_offset atIndex:1];
                  [encoder setFragmentTexture:texture->texture atIndex:0];
                  [encoder setFragmentSamplerState:r_mtl_state->samplers[group_params->tex_sample_kind] atIndex:0];
                  [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:inst_count];
                }
              }
              [encoder endEncoding];
            }
          }break;
          case R_PassKind_Blur:
          {
            if(r_mtl_state->blur_pipeline != 0)
            {
              R_PassParams_Blur *params = render_pass->params_blur;
              R_MTL_BlurUniforms uniforms = r_mtl_blur_uniforms_from_params(params, viewport_dim);
              MTLScissorRect scissor = {0};
              B32 has_scissor = 0;
              if(params->clip.x0 != 0 ||
                 params->clip.y0 != 0 ||
                 params->clip.x1 != 0 ||
                 params->clip.y1 != 0)
              {
                has_scissor = r_mtl_scissor_from_clip(params->clip, mtl_window->drawable_size, scale, &scissor);
              }
              else
              {
                scissor = (MTLScissorRect){0, 0, (NSUInteger)mtl_window->drawable_size.x, (NSUInteger)mtl_window->drawable_size.y};
                has_scissor = 1;
              }
              if(has_scissor)
              {
                for(Axis2 axis = (Axis2)0; axis < Axis2_COUNT; axis = (Axis2)(axis + 1))
                {
                  id<MTLTexture> src = (axis == Axis2_X ? mtl_window->stage_color : mtl_window->stage_scratch_color);
                  id<MTLTexture> dst = (axis == Axis2_X ? mtl_window->stage_scratch_color : mtl_window->stage_color);
                  uniforms.direction = (axis == Axis2_X ? v2f32(1.f/(F32)Max(mtl_window->drawable_size.x, 1), 0) : v2f32(0, 1.f/(F32)Max(mtl_window->drawable_size.y, 1)));
                  U64 uniform_offset = 0;
                  id<MTLBuffer> uniform_buffer = r_mtl_upload_buffer(&uniforms, sizeof(uniforms), 256, &uniform_offset);

                  MTLRenderPassDescriptor *blur_pass = mtl_window->blur_pass;
                  blur_pass.colorAttachments[0].texture = dst;
                  blur_pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
                  blur_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                  id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:blur_pass];
                  [encoder setRenderPipelineState:r_mtl_state->blur_pipeline];
                  [encoder setScissorRect:scissor];
                  [encoder setVertexBuffer:uniform_buffer offset:uniform_offset atIndex:0];
                  [encoder setFragmentBuffer:uniform_buffer offset:uniform_offset atIndex:0];
                  [encoder setFragmentTexture:src atIndex:0];
                  [encoder setFragmentSamplerState:r_mtl_state->samplers[R_Tex2DSampleKind_Linear] atIndex:0];
                  [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
                  [encoder endEncoding];
                }
              }
            }
          }break;
          case R_PassKind_Geo3D:
          {
            if(r_mtl_state->mesh_pipeline != 0 &&
               r_mtl_state->geo3d_composite_pipeline != 0 &&
               mtl_window->geo3d_color != 0 &&
               mtl_window->geo3d_depth != 0)
            {
              R_PassParams_Geo3D *params = render_pass->params_geo3d;
              R_BatchGroup3DMap *mesh_group_map = &params->mesh_batches;

              MTLRenderPassDescriptor *geo_pass = mtl_window->geo_pass;
              geo_pass.colorAttachments[0].texture = mtl_window->geo3d_color;
              geo_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
              geo_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
              geo_pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
              geo_pass.depthAttachment.texture = mtl_window->geo3d_depth;
              geo_pass.depthAttachment.loadAction = MTLLoadActionClear;
              geo_pass.depthAttachment.storeAction = MTLStoreActionDontCare;
              geo_pass.depthAttachment.clearDepth = 1.0;
              id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:geo_pass];
              [encoder setRenderPipelineState:r_mtl_state->mesh_pipeline];
              [encoder setDepthStencilState:r_mtl_state->mesh_depth_stencil];

              Vec2F32 geo_viewport_dim = dim_2f32(params->viewport);
              MTLViewport geo_viewport =
              {
                params->viewport.x0*scale,
                params->viewport.y0*scale,
                geo_viewport_dim.x*scale,
                geo_viewport_dim.y*scale,
                0,
                1,
              };
              [encoder setViewport:geo_viewport];
              MTLScissorRect full_scissor = {0, 0, (NSUInteger)mtl_window->drawable_size.x, (NSUInteger)mtl_window->drawable_size.y};
              [encoder setScissorRect:full_scissor];

              R_MTL_MeshUniforms uniforms = {0};
              uniforms.xform = mul_4x4f32(params->projection, params->view);
              U64 uniform_offset = 0;
              id<MTLBuffer> uniform_buffer = r_mtl_upload_buffer(&uniforms, sizeof(uniforms), 256, &uniform_offset);
              [encoder setVertexBuffer:uniform_buffer offset:uniform_offset atIndex:1];

              for(U64 slot_idx = 0; slot_idx < mesh_group_map->slots_count; slot_idx += 1)
              {
                for(R_BatchGroup3DMapNode *n = mesh_group_map->slots[slot_idx]; n != 0; n = n->next)
                {
                  R_BatchGroup3DParams *group_params = &n->params;
                  R_MTL_Buffer *mesh_vertices = r_mtl_buffer_from_handle(group_params->mesh_vertices);
                  R_MTL_Buffer *mesh_indices = r_mtl_buffer_from_handle(group_params->mesh_indices);
                  if(group_params->mesh_geo_topology == R_GeoTopologyKind_Triangles &&
                     mesh_vertices != 0 && mesh_vertices->buffer != 0 &&
                     mesh_indices != 0 && mesh_indices->buffer != 0)
                  {
                    [encoder setVertexBuffer:mesh_vertices->buffer offset:0 atIndex:0];
                    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                        indexCount:mesh_indices->size/sizeof(U32)
                                         indexType:MTLIndexTypeUInt32
                                       indexBuffer:mesh_indices->buffer
                                 indexBufferOffset:0];
                  }
                }
              }
              [encoder endEncoding];

              MTLScissorRect composite_scissor = {0};
              B32 has_composite_scissor = 0;
              if(params->clip.x0 != 0 ||
                 params->clip.y0 != 0 ||
                 params->clip.x1 != 0 ||
                 params->clip.y1 != 0)
              {
                has_composite_scissor = r_mtl_scissor_from_clip(params->clip, mtl_window->drawable_size, scale, &composite_scissor);
              }
              else
              {
                composite_scissor = full_scissor;
                has_composite_scissor = 1;
              }
              if(has_composite_scissor)
              {
                R_MTL_FinalizeUniforms composite_uniforms = {viewport_dim};
                U64 composite_uniform_offset = 0;
                id<MTLBuffer> composite_uniform_buffer = r_mtl_upload_buffer(&composite_uniforms, sizeof(composite_uniforms), 256, &composite_uniform_offset);

                MTLRenderPassDescriptor *composite_pass = mtl_window->composite_pass;
                composite_pass.colorAttachments[0].texture = mtl_window->stage_color;
                composite_pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
                composite_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                encoder = [command_buffer renderCommandEncoderWithDescriptor:composite_pass];
                [encoder setRenderPipelineState:r_mtl_state->geo3d_composite_pipeline];
                [encoder setScissorRect:composite_scissor];
                [encoder setVertexBuffer:composite_uniform_buffer offset:composite_uniform_offset atIndex:0];
                [encoder setFragmentTexture:mtl_window->geo3d_color atIndex:0];
                [encoder setFragmentSamplerState:r_mtl_state->samplers[R_Tex2DSampleKind_Nearest] atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
                [encoder endEncoding];
              }
            }
          }break;
        }
      }

      if(r_mtl_state->finalize_pipeline != 0)
      {
        R_MTL_FinalizeUniforms uniforms = {viewport_dim};
        U64 uniform_offset = 0;
        id<MTLBuffer> uniform_buffer = r_mtl_upload_buffer(&uniforms, sizeof(uniforms), 256, &uniform_offset);
        MTLRenderPassDescriptor *final_pass = mtl_window->final_pass;
        final_pass.colorAttachments[0].texture = drawable.texture;
        final_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        final_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        final_pass.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.06, 0.065, 1.0);
        id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:final_pass];
        [encoder setRenderPipelineState:r_mtl_state->finalize_pipeline];
        [encoder setVertexBuffer:uniform_buffer offset:uniform_offset atIndex:0];
        [encoder setFragmentTexture:mtl_window->stage_color atIndex:0];
        [encoder setFragmentSamplerState:r_mtl_state->samplers[R_Tex2DSampleKind_Nearest] atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [encoder endEncoding];
      }
        [command_buffer addCompletedHandler:r_mtl_state->command_buffer_completion_handler];
        [command_buffer presentDrawable:drawable];
        [command_buffer commit];
        U64 upload_slot_idx = r_mtl_state->upload_buffer_idx % ArrayCount(r_mtl_state->upload_buffer_command_buffers);
        id<MTLCommandBuffer> old_command_buffer = r_mtl_state->upload_buffer_command_buffers[upload_slot_idx];
        if(old_command_buffer != 0)
        {
          [old_command_buffer release];
        }
        r_mtl_state->upload_buffer_command_buffers[upload_slot_idx] = [command_buffer retain];
      }
    }
  }
}
