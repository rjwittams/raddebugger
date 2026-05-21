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

internal void
r_mtl_rect_vertices_push(R_MTL_RectVertex *vertices, U64 *idx, R_Rect2DInst *inst, R_BatchGroup2DParams *params, Vec2S32 texture_size)
{
  Vec2F32 positions[Corner_COUNT] =
  {
    v2f32(inst->dst.x0, inst->dst.y0),
    v2f32(inst->dst.x0, inst->dst.y1),
    v2f32(inst->dst.x1, inst->dst.y0 + inst->shear),
    v2f32(inst->dst.x1, inst->dst.y1 + inst->shear),
  };
  Vec2F32 texcoords[Corner_COUNT] =
  {
    v2f32(inst->src.x0, inst->src.y0),
    v2f32(inst->src.x0, inst->src.y1),
    v2f32(inst->src.x1, inst->src.y0),
    v2f32(inst->src.x1, inst->src.y1),
  };
  Vec2F32 corner_pcts[Corner_COUNT] =
  {
    v2f32(0.f, 0.f),
    v2f32(0.f, 1.f),
    v2f32(1.f, 0.f),
    v2f32(1.f, 1.f),
  };
  U32 indices[] =
  {
    Corner_00, Corner_01, Corner_10,
    Corner_10, Corner_01, Corner_11,
  };
  F32 texture_w = (F32)Max(texture_size.x, 1);
  F32 texture_h = (F32)Max(texture_size.y, 1);
  Vec2F32 xform_2x2_col0 = v2f32(params->xform.v[0][0], params->xform.v[0][1]);
  Vec2F32 xform_2x2_col1 = v2f32(params->xform.v[1][0], params->xform.v[1][1]);
  Vec2F32 xform_scale = v2f32(length_2f32(xform_2x2_col0), length_2f32(xform_2x2_col1));
  Vec2F32 dst_half_size = scale_2f32(dim_2f32(inst->dst), 0.5f);
  Vec2F32 rect_half_size = mul_2f32(dst_half_size, xform_scale);
  for EachIndex(index_idx, ArrayCount(indices))
  {
    U32 corner = indices[index_idx];
    Vec2F32 sdf_sample_pos =
    {
      (2.f*corner_pcts[corner].x - 1.f)*rect_half_size.x,
      (2.f*corner_pcts[corner].y - 1.f)*rect_half_size.y,
    };
    Vec3F32 transformed = xform_3f32(v3f32(positions[corner].x, positions[corner].y, 1.f), params->xform);
    R_MTL_RectVertex *vertex = vertices + *idx;
    vertex->pos = v2f32(transformed.x, transformed.y);
    vertex->texcoord = v2f32(texcoords[corner].x/texture_w, texcoords[corner].y/texture_h);
    vertex->color = inst->colors[corner];
    vertex->sdf_sample_pos = sdf_sample_pos;
    vertex->rect_half_size = rect_half_size;
    vertex->corner_radius = inst->corner_radii[corner]*Max(xform_scale.x, xform_scale.y);
    vertex->border_thickness = inst->border_thickness*Max(xform_scale.x, xform_scale.y);
    vertex->softness = inst->edge_softness*Max(xform_scale.x, xform_scale.y);
    vertex->omit_texture = inst->white_texture_override;
    *idx += 1;
  }
}

internal MTLScissorRect
r_mtl_scissor_from_clip(Rng2F32 clip, Vec2S32 drawable_size, F32 scale)
{
  S32 x0 = Clamp(0, (S32)floor_f32(clip.x0*scale), drawable_size.x);
  S32 y0 = Clamp(0, (S32)floor_f32(clip.y0*scale), drawable_size.y);
  S32 x1 = Clamp(0, (S32)ceil_f32(clip.x1*scale), drawable_size.x);
  S32 y1 = Clamp(0, (S32)ceil_f32(clip.y1*scale), drawable_size.y);
  MTLScissorRect result =
  {
    (NSUInteger)x0,
    (NSUInteger)y0,
    (NSUInteger)Max(x1 - x0, 1),
    (NSUInteger)Max(y1 - y0, 1),
  };
  return result;
}

r_hook void
r_init(CmdLine *cmdln)
{
  Arena *arena = arena_alloc();
  r_mtl_state = push_array(arena, R_MTL_State, 1);
  r_mtl_state->arena = arena;
  r_mtl_state->device = MTLCreateSystemDefaultDevice();
  if(r_mtl_state->device != 0)
  {
    r_mtl_state->command_queue = [r_mtl_state->device newCommandQueue];

    NSString *shader_source =
      @"#include <metal_stdlib>\n"
       "using namespace metal;\n"
       "struct RectVertex {\n"
       "  packed_float2 pos;\n"
       "  packed_float2 texcoord;\n"
       "  packed_float4 color;\n"
       "  packed_float2 sdf_sample_pos;\n"
       "  packed_float2 rect_half_size;\n"
       "  float corner_radius;\n"
       "  float border_thickness;\n"
       "  float softness;\n"
       "  float omit_texture;\n"
       "};\n"
       "struct RectUniforms {\n"
       "  packed_float2 viewport_size;\n"
       "  float opacity;\n"
       "};\n"
       "struct VertexOut {\n"
       "  float4 position [[position]];\n"
       "  float2 texcoord;\n"
       "  float4 color;\n"
       "  float2 sdf_sample_pos;\n"
       "  float2 rect_half_size;\n"
       "  float corner_radius;\n"
       "  float border_thickness;\n"
       "  float softness;\n"
       "  float omit_texture;\n"
       "};\n"
       "float rect_sdf(float2 sample_pos, float2 rect_half_size, float r) {\n"
       "  return length(max(abs(sample_pos) - rect_half_size + r, 0.0f)) - r;\n"
       "}\n"
       "vertex VertexOut rect_vertex(uint vertex_id [[vertex_id]],\n"
       "                             const device RectVertex *vertices [[buffer(0)]],\n"
       "                             constant RectUniforms &uniforms [[buffer(1)]]) {\n"
       "  RectVertex in_vertex = vertices[vertex_id];\n"
       "  VertexOut out;\n"
       "  float2 ndc = float2(2.0f*in_vertex.pos.x/uniforms.viewport_size.x - 1.0f,\n"
       "                       1.0f - 2.0f*in_vertex.pos.y/uniforms.viewport_size.y);\n"
       "  out.position = float4(ndc, 0.0f, 1.0f);\n"
       "  out.texcoord = in_vertex.texcoord;\n"
       "  out.color = float4(in_vertex.color);\n"
       "  out.sdf_sample_pos = in_vertex.sdf_sample_pos;\n"
       "  out.rect_half_size = in_vertex.rect_half_size;\n"
       "  out.corner_radius = in_vertex.corner_radius;\n"
       "  out.border_thickness = in_vertex.border_thickness;\n"
       "  out.softness = in_vertex.softness;\n"
       "  out.omit_texture = in_vertex.omit_texture;\n"
       "  return out;\n"
       "}\n"
       "fragment float4 rect_fragment(VertexOut in [[stage_in]],\n"
       "                              texture2d<float> tex [[texture(0)]],\n"
       "                              sampler tex_sampler [[sampler(0)]],\n"
       "                              constant RectUniforms &uniforms [[buffer(1)]]) {\n"
       "  float4 sample_color = in.omit_texture > 0.5f ? float4(1.0f) : tex.sample(tex_sampler, in.texcoord);\n"
       "  float soft_span = max(2.0f*in.softness, 0.001f);\n"
       "  float border_sdf_t = 1.0f;\n"
       "  if(in.border_thickness > 0.0f) {\n"
       "    float2 inner_half_size = in.rect_half_size - float2(in.softness*2.0f) - in.border_thickness;\n"
       "    float border_sdf_s = rect_sdf(in.sdf_sample_pos, inner_half_size, max(in.corner_radius-in.border_thickness, 0.0f));\n"
       "    border_sdf_t = smoothstep(0.0f, soft_span, border_sdf_s);\n"
       "  }\n"
       "  if(border_sdf_t < 0.001f) {\n"
       "    discard_fragment();\n"
       "  }\n"
       "  float corner_sdf_t = 1.0f;\n"
       "  if(in.corner_radius > 0.0f || in.softness > 0.75f) {\n"
       "    float corner_sdf_s = rect_sdf(in.sdf_sample_pos, in.rect_half_size - float2(in.softness*2.0f), in.corner_radius);\n"
       "    corner_sdf_t = 1.0f - smoothstep(0.0f, soft_span, corner_sdf_s);\n"
       "  }\n"
       "  float4 result = sample_color * in.color;\n"
       "  result.a *= uniforms.opacity;\n"
       "  result.a *= corner_sdf_t;\n"
       "  result.a *= border_sdf_t;\n"
       "  result.rgb *= uniforms.opacity;\n"
       "  return result;\n"
       "}\n";
    NSError *error = 0;
    id<MTLLibrary> library = [r_mtl_state->device newLibraryWithSource:shader_source options:0 error:&error];
    if(library != 0)
    {
      id<MTLFunction> vertex_function = [library newFunctionWithName:@"rect_vertex"];
      id<MTLFunction> fragment_function = [library newFunctionWithName:@"rect_fragment"];
      MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
      descriptor.vertexFunction = vertex_function;
      descriptor.fragmentFunction = fragment_function;
      descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
      descriptor.colorAttachments[0].blendingEnabled = YES;
      descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
      descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
      descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
      descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
      descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
      descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
      r_mtl_state->rect_pipeline = [r_mtl_state->device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    }

    for EachIndex(idx, R_Tex2DSampleKind_COUNT)
    {
      MTLSamplerDescriptor *sampler_descriptor = [MTLSamplerDescriptor new];
      sampler_descriptor.sAddressMode = MTLSamplerAddressModeRepeat;
      sampler_descriptor.tAddressMode = MTLSamplerAddressModeRepeat;
      sampler_descriptor.minFilter = (idx == R_Tex2DSampleKind_Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest);
      sampler_descriptor.magFilter = (idx == R_Tex2DSampleKind_Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest);
      r_mtl_state->samplers[idx] = [r_mtl_state->device newSamplerStateWithDescriptor:sampler_descriptor];
    }

    U32 white_pixels[] = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
    r_mtl_state->white_texture = r_mtl_tex2d_from_handle(r_tex2d_alloc(R_ResourceKind_Static, v2s32(2, 2), R_Tex2DFormat_RGBA8, white_pixels));
  }
}

r_hook R_Handle
r_window_equip(WM_Window window)
{
  R_MTL_Window *result = 0;
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
    result->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    result->layer.framebufferOnly = YES;
    result->contents_scale = r_mtl_contents_scale_from_window(window);
    result->layer.contentsScale = result->contents_scale;
    result->drawable_size = r_mtl_drawable_size_from_window(window);
    result->layer.drawableSize = CGSizeMake(result->drawable_size.x, result->drawable_size.y);
    [content_view setWantsLayer:YES];
    [content_view setLayer:result->layer];
  }
  return r_mtl_handle_from_window(result);
}

r_hook void
r_window_unequip(WM_Window window, R_Handle window_equip)
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
    mtl_window->layer = 0;
    SLLStackPush(r_mtl_state->free_window, mtl_window);
  }
}

r_hook R_Handle
r_tex2d_alloc(R_ResourceKind kind, Vec2S32 size, R_Tex2DFormat format, void *data)
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
  descriptor.storageMode = MTLStorageModeShared;
  texture->texture = [r_mtl_state->device newTextureWithDescriptor:descriptor];
  if(data != 0 && texture->texture != 0)
  {
    Rng2S32 region = r2s32p(0, 0, size.x, size.y);
    r_fill_tex2d_region(r_mtl_handle_from_tex2d(texture), region, data);
  }
  return r_mtl_handle_from_tex2d(texture);
}

r_hook void
r_tex2d_release(R_Handle handle)
{
  R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
  if(texture != 0)
  {
    texture->texture = 0;
    SLLStackPush(r_mtl_state->free_tex2d, texture);
  }
}

r_hook R_ResourceKind
r_kind_from_tex2d(R_Handle handle)
{
  R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
  R_ResourceKind result = R_ResourceKind_Static;
  if(texture != 0)
  {
    result = texture->kind;
  }
  return result;
}

r_hook Vec2S32
r_size_from_tex2d(R_Handle handle)
{
  R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
  Vec2S32 result = {1, 1};
  if(texture != 0)
  {
    result = texture->size;
  }
  return result;
}

r_hook R_Tex2DFormat
r_format_from_tex2d(R_Handle handle)
{
  R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
  R_Tex2DFormat result = R_Tex2DFormat_RGBA8;
  if(texture != 0)
  {
    result = texture->format;
  }
  return result;
}

r_hook void
r_fill_tex2d_region(R_Handle handle, Rng2S32 subrect, void *data)
{
  R_MTL_Tex2D *texture = r_mtl_tex2d_from_handle(handle);
  if(texture != 0 && texture->texture != 0 && data != 0)
  {
    U64 bytes_per_pixel = r_mtl_bytes_per_pixel_from_tex2d_format(texture->format);
    MTLRegion region = MTLRegionMake2D(subrect.x0, subrect.y0, subrect.x1 - subrect.x0, subrect.y1 - subrect.y0);
    [texture->texture replaceRegion:region
                         mipmapLevel:0
                           withBytes:data
                         bytesPerRow:(subrect.x1 - subrect.x0)*bytes_per_pixel];
  }
}

r_hook R_Handle
r_buffer_alloc(R_ResourceKind kind, U64 size, void *data)
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
  if(data != 0)
  {
    buffer->buffer = [r_mtl_state->device newBufferWithBytes:data length:size options:options];
  }
  else
  {
    buffer->buffer = [r_mtl_state->device newBufferWithLength:size options:options];
  }
  return r_mtl_handle_from_buffer(buffer);
}

r_hook void
r_buffer_release(R_Handle handle)
{
  R_MTL_Buffer *buffer = r_mtl_buffer_from_handle(handle);
  if(buffer != 0)
  {
    buffer->buffer = 0;
    SLLStackPush(r_mtl_state->free_buffer, buffer);
  }
}

r_hook void
r_begin_frame(void)
{
}

r_hook void
r_end_frame(void)
{
}

r_hook void
r_window_begin_frame(WM_Window window, R_Handle window_equip)
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
  R_MTL_Window *mtl_window = r_mtl_window_from_handle(window_equip);
  if(mtl_window != 0 && r_mtl_state->command_queue != 0)
  {
    id<CAMetalDrawable> drawable = [mtl_window->layer nextDrawable];
    if(drawable != 0)
    {
      MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
      pass.colorAttachments[0].texture = drawable.texture;
      pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      pass.colorAttachments[0].clearColor = MTLClearColorMake(0.06, 0.06, 0.065, 1.0);

      id<MTLCommandBuffer> command_buffer = [r_mtl_state->command_queue commandBuffer];
      id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:pass];
      if(r_mtl_state->rect_pipeline != 0)
      {
        Temp scratch = scratch_begin(0, 0);
        Rng2F32 viewport_rect = wm_client_rect_from_window(window);
        Vec2F32 viewport_dim = dim_2f32(viewport_rect);
        R_MTL_RectUniforms uniforms =
        {
          viewport_dim,
          1.f,
        };
        F32 scale = r_mtl_contents_scale_from_window(window);
        [encoder setRenderPipelineState:r_mtl_state->rect_pipeline];
        for(R_PassNode *pass_n = passes->first; pass_n != 0; pass_n = pass_n->next)
        {
          R_Pass *render_pass = &pass_n->v;
          switch(render_pass->kind)
          {
            default:{}break;
            case R_PassKind_UI:
            {
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
                  U64 vertex_count = inst_count*6;
                  R_MTL_RectVertex *vertices = push_array_no_zero(scratch.arena, R_MTL_RectVertex, vertex_count);
                  U64 vertex_idx = 0;
                  for(R_BatchNode *batch_n = batches->first; batch_n != 0; batch_n = batch_n->next)
                  {
                    R_Rect2DInst *insts = (R_Rect2DInst *)batch_n->v.v;
                    U64 batch_inst_count = batch_n->v.byte_count / batches->bytes_per_inst;
                    for(U64 inst_idx = 0; inst_idx < batch_inst_count; inst_idx += 1)
                    {
                      r_mtl_rect_vertices_push(vertices, &vertex_idx, insts + inst_idx, group_params, texture->size);
                    }
                  }
                  id<MTLBuffer> vertex_buffer = [r_mtl_state->device newBufferWithBytes:vertices
                                                                                  length:vertex_count*sizeof(R_MTL_RectVertex)
                                                                                 options:MTLResourceStorageModeShared];
                  R_MTL_RectUniforms group_uniforms = uniforms;
                  group_uniforms.opacity = 1.f - group_params->transparency;
                  id<MTLBuffer> uniform_buffer = [r_mtl_state->device newBufferWithBytes:&group_uniforms
                                                                                   length:sizeof(group_uniforms)
                                                                                  options:MTLResourceStorageModeShared];
                  if(group_params->clip.x0 != 0 ||
                     group_params->clip.y0 != 0 ||
                     group_params->clip.x1 != 0 ||
                     group_params->clip.y1 != 0)
                  {
                    MTLScissorRect scissor = r_mtl_scissor_from_clip(group_params->clip, mtl_window->drawable_size, scale);
                    [encoder setScissorRect:scissor];
                  }
                  else
                  {
                    MTLScissorRect scissor = {0, 0, (NSUInteger)mtl_window->drawable_size.x, (NSUInteger)mtl_window->drawable_size.y};
                    [encoder setScissorRect:scissor];
                  }
                  [encoder setVertexBuffer:vertex_buffer offset:0 atIndex:0];
                  [encoder setVertexBuffer:uniform_buffer offset:0 atIndex:1];
                  [encoder setFragmentBuffer:uniform_buffer offset:0 atIndex:1];
                  [encoder setFragmentTexture:texture->texture atIndex:0];
                  [encoder setFragmentSamplerState:r_mtl_state->samplers[group_params->tex_sample_kind] atIndex:0];
                  [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertex_count];
                }
              }
            }break;
          }
        }
        scratch_end(scratch);
      }
      [encoder endEncoding];
      [command_buffer presentDrawable:drawable];
      [command_buffer commit];
    }
  }
}
