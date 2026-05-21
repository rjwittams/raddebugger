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

typedef struct R_MTL_State R_MTL_State;
struct R_MTL_State
{
  Arena *arena;
  id<MTLDevice> device;
  id<MTLCommandQueue> command_queue;
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
internal Vec2S32 r_mtl_drawable_size_from_window(WM_Window window);

#endif // RENDER_METAL_H
