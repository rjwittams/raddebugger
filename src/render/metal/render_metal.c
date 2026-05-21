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

internal Vec2S32
r_mtl_drawable_size_from_window(WM_Window window)
{
  Rng2F32 rect = wm_client_rect_from_window(window);
  F32 scale = wm_dpi_from_window(window)/96.f;
  Vec2S32 result =
  {
    (S32)ceil_f32((rect.x1 - rect.x0)*scale),
    (S32)ceil_f32((rect.y1 - rect.y0)*scale),
  };
  result.x = Max(result.x, 1);
  result.y = Max(result.y, 1);
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
    result->layer.contentsScale = [mac_window->ns_window backingScaleFactor];
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
    MAC_WM_Window *mac_window = mac_wm_window_from_handle(window);
    Vec2S32 drawable_size = r_mtl_drawable_size_from_window(window);
    if(drawable_size.x != mtl_window->drawable_size.x || drawable_size.y != mtl_window->drawable_size.y)
    {
      mtl_window->drawable_size = drawable_size;
      mtl_window->layer.contentsScale = [mac_window->ns_window backingScaleFactor];
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
      [encoder endEncoding];
      [command_buffer presentDrawable:drawable];
      [command_buffer commit];
    }
  }
}
