// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#include "disasm/disasm.c"
#if defined(X64_H)
# include "x64/disasm/x64_disasm.c"
#endif
#if defined(ARM64_H)
# include "arm64/disasm/arm64_disasm.c"
#endif
