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

internal B32
t_txt_token_array_has_string_kind(String8 data, TXT_TokenArray tokens, String8 string, TXT_TokenKind kind)
{
  B32 result = 0;
  for(U64 idx = 0; idx < tokens.count; idx += 1)
  {
    TXT_Token token = tokens.v[idx];
    if(token.kind == kind && str8_match(str8_substr(data, token.range), string, 0))
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

TEST(swift_lexer_from_extension_handles_attributes)
{
  String8 data = str8_lit("@inline(never)\npublic func tick(_ value: Int32) -> Int32 {\n  return value + 1\n}\n");
  TXT_LangKind swift_lang_kind = txt_lang_kind_from_extension(str8_lit("swift"));
  T_Ok(swift_lang_kind != TXT_LangKind_Null);

  TXT_LangLexFunctionType *swift_lex = txt_lex_function_from_lang_kind(swift_lang_kind);
  T_Ok(swift_lex != 0);

  TXT_TokenArray tokens = swift_lex(arena, 0, data);
  T_Ok(!t_txt_token_array_has_kind(tokens, TXT_TokenKind_Error));
  T_Ok(t_txt_token_array_has_string_kind(data, tokens, str8_lit("public"), TXT_TokenKind_Keyword));
  T_Ok(t_txt_token_array_has_string_kind(data, tokens, str8_lit("func"), TXT_TokenKind_Keyword));
  T_Ok(t_txt_token_array_has_string_kind(data, tokens, str8_lit("@"), TXT_TokenKind_Symbol));
}
