// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef FONT_PROVIDER_INC_H
#define FONT_PROVIDER_INC_H

////////////////////////////////
//~ rjf: Backend Constants

#define FP_BACKEND_STUB 0
#define FP_BACKEND_DWRITE 1
#define FP_BACKEND_FREETYPE 2
#define FP_BACKEND_CORETEXT 3

////////////////////////////////
//~ rjf: Decide On Backend

#if !defined(FP_BACKEND)
# if OS_WINDOWS
#  define FP_BACKEND FP_BACKEND_DWRITE
# elif OS_LINUX
#  define FP_BACKEND FP_BACKEND_FREETYPE
# elif OS_MAC
#  define FP_BACKEND FP_BACKEND_CORETEXT
# endif
#endif

////////////////////////////////
//~ rjf: Main Includes

#include "font_provider.h"

////////////////////////////////
//~ rjf: Backend Includes

#if FP_BACKEND == FP_BACKEND_STUB
# include "stub/font_provider_stub.h"
#elif FP_BACKEND == FP_BACKEND_DWRITE
# include "dwrite/font_provider_dwrite.h"
#elif FP_BACKEND == FP_BACKEND_FREETYPE
# include "freetype/font_provider_freetype.h"
#elif FP_BACKEND == FP_BACKEND_CORETEXT
# include "mac/font_provider/mac_font_provider.h"
#else
# error Font provider backend not specified.
#endif

#endif // FONT_PROVIDER_INC_H
