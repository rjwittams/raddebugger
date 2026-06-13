// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#undef T_Group
#define T_Group "Text"

internal B32
t_txt_token_array_has_kind(TXT_TokenArray tokens, TXT_TokenKind kind)
{
  B32 result = 0;
  for(U64 idx = 0; idx < tokens.count; idx += 1)
  {
    if(tokens.v[idx].kind == kind)
    {
      result = 1;
      break;
    }
  }
  return result;
}

TEST(arm64_disasm_lexer_keeps_x64_hash_behavior)
{
  TXT_LangKind arm64_lang_kind = txt_lang_kind_from_arch(Arch_arm64);
  T_Ok(arm64_lang_kind != TXT_LangKind_Null);

  TXT_LangLexFunctionType *arm64_lex = txt_lex_function_from_lang_kind(arm64_lang_kind);
  T_Ok(arm64_lex != 0);

  TXT_TokenArray arm64_tokens = arm64_lex(arena, 0, str8_lit("add x0, x1, #0x10"));
  T_Ok(!t_txt_token_array_has_kind(arm64_tokens, TXT_TokenKind_Error));

  TXT_TokenArray x64_tokens = txt_token_array_from_string__disasm_x64_intel(arena, 0, str8_lit("add rax, #0x10"));
  T_Ok(t_txt_token_array_has_kind(x64_tokens, TXT_TokenKind_Error));
}
