// Licensed under the MIT license (https://opensource.org/license/mit/)

#undef T_Group
#define T_Group "Filesystem Evaluation"

internal void
t_check_folder_picker_paths(Arena *arena, TestCtx *ctx)
{
#if OS_LINUX || OS_MAC
  String8 inputs[] = {str8_lit("/"), str8_lit("/u"), str8_lit(""), str8_lit("u")};
  for(U64 input_idx = 0; input_idx < ArrayCount(inputs); input_idx += 1)
  {
    String8 filter = inputs[input_idx];
    E_Eval root = e_eval_from_string(str8f(arena, "folder:\"%S\"", filter));
    E_TypeExpandRule *rule = e_expand_rule_from_type_key(root.irtree.type_key);
    E_TypeExpandInfo info = rule->info(arena, root, filter);
    T_Ok(info.expr_count != 0);
    E_Eval *children = push_array(arena, E_Eval, info.expr_count);
    rule->range(arena, info.user_data, root, filter, r1u64(0, info.expr_count), children);
    B32 found_usr = 0;
    for(U64 idx = 0; idx < info.expr_count; idx += 1)
    {
      E_Eval child = children[idx];
      String8 path = rd_file_path_from_eval(arena, child);
      if(str8_match(filter, str8_lit("/u"), 0) || str8_match(filter, str8_lit("u"), 0))
      {
        T_Ok(!str8_match(path, str8_lit("/bin"), 0));
      }
      if(str8_match(path, str8_lit("/usr"), 0))
      {
        found_usr = 1;
        T_Ok(e_type_key_match(child.irtree.type_key, e_type_key_folder()));
      }
    }
    T_Ok(found_usr);
  }
#elif OS_WINDOWS
  E_Eval roots = e_eval_from_string(str8_lit("folder:\"\""));
  T_Ok(e_type_key_match(roots.irtree.type_key, e_type_key_folder()));
  T_Ok(e_string_from_id(roots.value.u64).size == 0);
  E_TypeExpandRule *rule = e_expand_rule_from_type_key(roots.irtree.type_key);
  E_TypeExpandInfo info = rule->info(arena, roots, str8_zero());
  T_Ok(info.expr_count != 0);
  E_Eval *drives = push_array(arena, E_Eval, info.expr_count);
  rule->range(arena, info.user_data, roots, str8_zero(), r1u64(0, info.expr_count), drives);
  String8 current_drive = str8_prefix(ctx->artifacts_path, 2);
  B32 found_drive = 0;
  for(U64 idx = 0; idx < info.expr_count; idx += 1)
  {
    String8 path = rd_file_path_from_eval(arena, drives[idx]);
    if(str8_match(path, current_drive, StringMatchFlag_CaseInsensitive))
    {
      found_drive = 1;
      T_Ok(e_type_key_match(drives[idx].irtree.type_key, e_type_key_folder()));
    }
  }
  T_Ok(found_drive);
#endif

  // Use the test's own artifacts for file/folder navigation and filtering.
  T_Ok(t_make_dir(str8_lit("parent")));
  T_Ok(t_make_dir(str8_lit("parent/child")));
  T_Ok(t_write_file(str8_lit("parent/child/item.txt"), str8_lit("fixture")));
  String8 parent = t_make_file_path(arena, str8_lit("parent"));
  String8 child_path = t_make_file_path(arena, str8_lit("parent/child"));
  String8 item_path = t_make_file_path(arena, str8_lit("parent/child/item.txt"));
  String8 suffixes[] = {str8_lit("/"), str8_lit("/ch"), str8_lit("/child"), str8_lit("/child/")};
  for(U64 idx = 0; idx < ArrayCount(suffixes); idx += 1)
  {
    String8 input = str8f(arena, "%S%S", parent, suffixes[idx]);
    E_Eval folder = e_eval_from_string(str8f(arena, "folder:\"%S\"", escaped_from_raw_str8(arena, input)));
    T_Ok(e_type_key_match(folder.irtree.type_key, e_type_key_folder()));
    B32 inside_child = (idx == 3);
    T_Ok(path_match_normalized(rd_file_path_from_eval(arena, folder), inside_child ? child_path : parent));
    E_TypeExpandRule *expand = e_expand_rule_from_type_key(folder.irtree.type_key);
    E_TypeExpandInfo expanded = expand->info(arena, folder, input);
    T_Ok(expanded.expr_count == 1);
    E_Eval entry = {0};
    expand->range(arena, expanded.user_data, folder, input, r1u64(0, 1), &entry);
    T_Ok(e_type_key_match(entry.irtree.type_key, inside_child ? e_type_key_file() : e_type_key_folder()));
    T_Ok(path_match_normalized(rd_file_path_from_eval(arena, entry), inside_child ? item_path : child_path));
  }
}

TEST(folder_picker_paths)
{
  E_BaseCtx *old_base_ctx = e_base_ctx;
  E_IRCtx *old_ir_ctx = e_ir_ctx;
  E_InterpretCtx *old_interpret_ctx = e_interpret_ctx;
  E_Cache *old_cache = e_cache;
  E_Cache *cache = e_cache_alloc();
  e_select_cache(cache);
  E_BaseCtx base_ctx = {0};
  E_IRCtx ir_ctx = {0};
  E_InterpretCtx interpret_ctx = {0};
  e_select_base_ctx(&base_ctx);
  e_select_ir_ctx(&ir_ctx);
  e_select_interpret_ctx(&interpret_ctx, 0, 0);

  t_check_folder_picker_paths(arena, ctx);

  e_base_ctx = old_base_ctx;
  e_ir_ctx = old_ir_ctx;
  e_interpret_ctx = old_interpret_ctx;
  e_select_cache(old_cache);
  e_cache_release(cache);
}
