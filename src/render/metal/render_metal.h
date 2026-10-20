// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef RENDER_METAL_H
#define RENDER_METAL_H

#pragma push_macro("internal")
#pragma push_macro("global")
#pragma push_macro("FileInfo")
#undef internal
#undef global
#define FileInfo MACOS_SDK_FileInfo
#import <AppKit/NSView.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#undef FileInfo
#pragma pop_macro("FileInfo")
#pragma pop_macro("global")
#pragma pop_macro("internal")
#if defined(nil)
# undef nil
#endif

#include "render/metal/generated/render_metal.meta.h"

typedef struct R_MTL_Window R_MTL_Window;
struct R_MTL_Window
{
  R_MTL_Window *next;
  CAMetalLayer *layer;
  id<MTLTexture> stage_color;
  id<MTLTexture> stage_scratch_color;
  id<MTLTexture> geo3d_color;
  id<MTLTexture> geo3d_depth;
  MTLRenderPassDescriptor *stage_clear_pass;
  MTLRenderPassDescriptor *stage_pass;
  MTLRenderPassDescriptor *blur_pass;
  MTLRenderPassDescriptor *geo_pass;
  MTLRenderPassDescriptor *composite_pass;
  MTLRenderPassDescriptor *final_pass;
  Vec2S32 drawable_size;
  F32 contents_scale;
};

typedef struct R_MTL_Tex2D R_MTL_Tex2D;
struct R_MTL_Tex2D
{
  R_MTL_Tex2D *next;
  id<MTLTexture> texture;
  R_ResourceKind kind;
  R_Tex2DFormat format;
  Vec2S32 size;
};

typedef struct R_MTL_Buffer R_MTL_Buffer;
struct R_MTL_Buffer
{
  R_MTL_Buffer *next;
  id<MTLBuffer> buffer;
  R_ResourceKind kind;
  U64 size;
};

typedef struct R_MTL_RetiredObject R_MTL_RetiredObject;
struct R_MTL_RetiredObject
{
  R_MTL_RetiredObject *next;
  id object;
};

typedef struct R_MTL_RectUniforms R_MTL_RectUniforms;
struct R_MTL_RectUniforms
{
  Vec2F32 viewport_size;
  F32 opacity;
  F32 _padding0_;
  Mat4x4F32 texture_sample_channel_map;
  Vec2F32 texture_size;
  Vec2F32 xform_scale;
  Vec4F32 xform[3];
};

typedef struct R_MTL_BlurUniforms R_MTL_BlurUniforms;
struct R_MTL_BlurUniforms
{
  Rng2F32 rect;
  Vec4F32 corner_radii;
  Vec2F32 direction;
  Vec2F32 viewport_size;
  U32 blur_count;
  U32 _padding0_[3];
  Vec4F32 kernel[32];
};

typedef struct R_MTL_FinalizeUniforms R_MTL_FinalizeUniforms;
struct R_MTL_FinalizeUniforms
{
  Vec2F32 viewport_size;
};

typedef struct R_MTL_MeshUniforms R_MTL_MeshUniforms;
struct R_MTL_MeshUniforms
{
  Mat4x4F32 xform;
};

typedef struct R_MTL_State R_MTL_State;
struct R_MTL_State
{
  Arena *arena;
  RWMutex device_rw_mutex;
  id<MTLDevice> device;
  id<MTLCommandQueue> command_queue;
  void (^command_buffer_completion_handler)(id<MTLCommandBuffer>);
  id<MTLRenderPipelineState> rect_pipeline;
  id<MTLRenderPipelineState> blur_pipeline;
  id<MTLRenderPipelineState> mesh_pipeline;
  id<MTLRenderPipelineState> geo3d_composite_pipeline;
  id<MTLRenderPipelineState> finalize_pipeline;
  id<MTLDepthStencilState> mesh_depth_stencil;
  id<MTLSamplerState> samplers[R_Tex2DSampleKind_COUNT];
  R_MTL_Tex2D *white_texture;
  id<MTLBuffer> static_upload_buffer;
  U64 static_upload_buffer_cap;
  id<MTLBuffer> upload_buffers[3];
  id<MTLCommandBuffer> upload_buffer_command_buffers[3];
  U64 upload_buffer_caps[3];
  U64 upload_buffer_pos;
  U64 upload_buffer_idx;
  R_MTL_Window *free_window;
  R_MTL_Tex2D *free_tex2d;
  R_MTL_Buffer *free_buffer;
  R_MTL_RetiredObject *free_retired_object;
  R_MTL_Tex2D *retired_tex2d[3];
  R_MTL_Buffer *retired_buffer[3];
  R_MTL_RetiredObject *retired_object[3];
  U64 retire_idx;
};

global R_MTL_State *r_mtl_state = 0;

internal R_Handle r_mtl_handle_from_window(R_MTL_Window *window);
internal R_MTL_Window *r_mtl_window_from_handle(R_Handle handle);
internal R_Handle r_mtl_handle_from_tex2d(R_MTL_Tex2D *texture);
internal R_MTL_Tex2D *r_mtl_tex2d_from_handle(R_Handle handle);
internal R_Handle r_mtl_handle_from_buffer(R_MTL_Buffer *buffer);
internal R_MTL_Buffer *r_mtl_buffer_from_handle(R_Handle handle);
internal MTLPixelFormat r_mtl_pixel_format_from_tex2d_format(R_Tex2DFormat format);
internal U64 r_mtl_bytes_per_pixel_from_tex2d_format(R_Tex2DFormat format);
internal NSUInteger r_mtl_resource_options_from_kind(R_ResourceKind kind);
internal U64 r_mtl_blit_texture_bytes_per_row(U64 row_bytes);
internal id<MTLBuffer> r_mtl_static_upload_buffer_from_size(U64 size);
internal void r_mtl_log_ns_error(char *context, NSError *error);
internal id<MTLRenderPipelineState> r_mtl_render_pipeline_from_library(id<MTLLibrary> library, NSString *vertex_name, NSString *fragment_name, MTLPixelFormat pixel_format);
internal id<MTLRenderPipelineState> r_mtl_render_pipeline_from_library_ex(id<MTLLibrary> library, NSString *vertex_name, NSString *fragment_name, MTLPixelFormat color_pixel_format, MTLPixelFormat depth_pixel_format, B32 blend);
internal void r_mtl_retire_object(id object);
internal void r_mtl_drain_retired_slot(U64 slot_idx);
internal void r_mtl_window_resize_targets(R_MTL_Window *window);
internal id<MTLBuffer> r_mtl_upload_buffer(void *data, U64 size, U64 align, U64 *out_offset);
internal F32 r_mtl_contents_scale_from_window(WM_Window window);
internal Vec2S32 r_mtl_drawable_size_from_window(WM_Window window);
internal B32 r_mtl_scissor_from_clip(Rng2F32 clip, Vec2S32 drawable_size, F32 scale, MTLScissorRect *out);
internal R_MTL_BlurUniforms r_mtl_blur_uniforms_from_params(R_PassParams_Blur *params, Vec2F32 viewport_dim);

#endif // RENDER_METAL_H
