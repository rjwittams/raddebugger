// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_SHELL_H
#define MAC_SHELL_H

#pragma push_macro("internal")
#pragma push_macro("global")
#pragma push_macro("FileInfo")
#undef internal
#undef global
#define FileInfo MACOS_SDK_FileInfo
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>
#import <AppKit/NSAlert.h>
#import <AppKit/NSOpenPanel.h>
#import <AppKit/NSWorkspace.h>
#undef FileInfo
#pragma pop_macro("FileInfo")
#pragma pop_macro("global")
#pragma pop_macro("internal")
#if defined(nil)
# undef nil
#endif

internal NSString *mac_sh_ns_string_from_string8(Arena *arena, String8 string);

#endif // MAC_SHELL_H
