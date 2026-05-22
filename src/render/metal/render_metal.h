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

typedef struct R_MTL_Window R_MTL_Window;
struct R_MTL_Window
{
  R_MTL_Window *next;
  CAMetalLayer *layer;
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

typedef struct R_MTL_RectVertex R_MTL_RectVertex;
struct R_MTL_RectVertex
{
  Vec2F32 pos;
  Vec2F32 texcoord;
  Vec4F32 color;
  Vec2F32 sdf_sample_pos;
  Vec2F32 rect_half_size;
  F32 corner_radius;
  F32 border_thickness;
  F32 softness;
  F32 omit_texture;
};

typedef struct R_MTL_RectUniforms R_MTL_RectUniforms;
struct R_MTL_RectUniforms
{
  Vec2F32 viewport_size;
  F32 opacity;
};

typedef struct R_MTL_State R_MTL_State;
struct R_MTL_State
{
  Arena *arena;
  id<MTLDevice> device;
  id<MTLCommandQueue> command_queue;
  id<MTLRenderPipelineState> rect_pipeline;
  id<MTLSamplerState> samplers[R_Tex2DSampleKind_COUNT];
  R_MTL_Tex2D *white_texture;
  R_MTL_Window *free_window;
  R_MTL_Tex2D *free_tex2d;
  R_MTL_Buffer *free_buffer;
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
internal F32 r_mtl_contents_scale_from_window(WM_Window window);
internal Vec2S32 r_mtl_drawable_size_from_window(WM_Window window);
internal void r_mtl_rect_vertices_push(R_MTL_RectVertex *vertices, U64 *idx, R_Rect2DInst *inst, R_BatchGroup2DParams *params, Vec2S32 texture_size);
internal MTLScissorRect r_mtl_scissor_from_clip(Rng2F32 clip, Vec2S32 drawable_size, F32 scale);

#endif // RENDER_METAL_H
