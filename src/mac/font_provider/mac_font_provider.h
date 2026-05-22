// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_FONT_PROVIDER_H
#define MAC_FONT_PROVIDER_H

#pragma push_macro("internal")
#pragma push_macro("global")
#pragma push_macro("FileInfo")
#undef internal
#undef global
#define FileInfo MACOS_SDK_FileInfo
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#undef FileInfo
#pragma pop_macro("FileInfo")
#pragma pop_macro("global")
#pragma pop_macro("internal")
#if defined(nil)
# undef nil
#endif

typedef struct MAC_FP_Font MAC_FP_Font;
struct MAC_FP_Font
{
  MAC_FP_Font *next;
  CGDataProviderRef provider;
  CGFontRef cg_font;
};

typedef struct MAC_FP_State MAC_FP_State;
struct MAC_FP_State
{
  Arena *arena;
  MAC_FP_Font *free_font;
};

global MAC_FP_State *mac_fp_state = 0;

internal MAC_FP_Font *mac_fp_font_from_handle(FP_Handle handle);
internal FP_Handle mac_fp_handle_from_font(MAC_FP_Font *font);
internal MAC_FP_Font *mac_fp_font_alloc(void);
internal void mac_fp_font_release(MAC_FP_Font *font);

#endif // MAC_FONT_PROVIDER_H
