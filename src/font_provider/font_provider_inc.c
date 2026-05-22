// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#include "font_provider.c"

#if FP_BACKEND == FP_BACKEND_STUB
# include "stub/font_provider_stub.c"
#elif FP_BACKEND == FP_BACKEND_DWRITE
# include "dwrite/font_provider_dwrite.c"
#elif FP_BACKEND == FP_BACKEND_FREETYPE
# include "freetype/font_provider_freetype.c"
#elif FP_BACKEND == FP_BACKEND_CORETEXT
# include "mac/font_provider/mac_font_provider.c"
#else
# error Font provider backend not specified.
#endif
