// Licensed under the MIT license (https://opensource.org/license/mit/)

#undef T_Group
#define T_Group "Configuration"

internal void
t_check_config_directory_roundtrip(Arena *arena, TestCtx *ctx, CFG_State *state)
{
  CFG_SchemaTable schemas = {0};
  schemas.slots_count = 16;
  schemas.slots = push_array(arena, CFG_SchemaNode *, schemas.slots_count);
  cfg_schema_table_insert(arena, &schemas, str8_lit("target"),
                         md_tree_from_string(arena, str8_lit("x: { working_directory: @folder path }"))->first);
  String8 roots[] = {str8_lit("/tmp/project/"), str8_lit("C:/project/")};
  String8 moved_roots[] = {str8_lit("/tmp/moved/"), str8_lit("C:/moved/")};
  for(U64 root_idx = 0; root_idx < ArrayCount(roots); root_idx += 1)
  {
    String8 root = roots[root_idx];
    String8 paths[] = {str8_chop(root, 1), root, str8f(arena, "%Ssub/", root), str8_zero()};
    for(U64 path_idx = 0; path_idx < ArrayCount(paths); path_idx += 1)
    {
      CFG_Node *target = cfg_node_new(state, &cfg_nil_node, str8_lit("target"));
      cfg_node_new(state, cfg_node_new(state, target, str8_lit("working_directory")), paths[path_idx]);
      for(U32 pass_idx = 0; pass_idx < 2; pass_idx += 1)
      {
        String8 saved = cfg_string_from_tree(arena, &schemas, root, target);
        CFG_NodePtrList loaded = cfg_node_ptr_list_from_string(arena, state, &schemas, root, saved);
        T_Ok(loaded.count == 1);
        target = loaded.first->v;
        String8 value = cfg_node_child_from_string(target, str8_lit("working_directory"))->first->string;
        if(paths[path_idx].size == 0)
        {
          T_Ok(value.size == 0);
        }
        else
        {
          T_Ok(path_match_normalized(value, paths[path_idx]));
        }
        if(path_idx == 1 && pass_idx == 0)
        {
          CFG_NodePtrList moved = cfg_node_ptr_list_from_string(arena, state, &schemas, moved_roots[root_idx], saved);
          T_Ok(moved.count == 1);
          String8 moved_value = cfg_node_child_from_string(moved.first->v, str8_lit("working_directory"))->first->string;
          T_Ok(path_match_normalized(moved_value, moved_roots[root_idx]));
        }
      }
    }
  }
}

TEST(config_directory_roundtrip)
{
  CFG_State *state = cfg_state_alloc();
  t_check_config_directory_roundtrip(arena, ctx, state);
  cfg_state_release(state);
}

internal void
t_check_binding_roundtrip(Arena *arena, TestCtx *ctx, CFG_State *state)
{
  CFG_SchemaTable schemas = {0};
  schemas.slots_count = 16;
  schemas.slots = push_array(arena, CFG_SchemaNode *, schemas.slots_count);
  CFG_Node *user = cfg_node_new(state, cfg_node_root(), str8_lit("user"));
  CFG_Node *bindings = cfg_node_new(state, user, str8_lit("keybindings"));
  WM_Modifiers modifiers[] = {WM_Modifier_Cmd, WM_Modifier_Ctrl, WM_Modifier_Cmd|WM_Modifier_Ctrl|WM_Modifier_Shift|WM_Modifier_Alt};
  for EachElement(idx, modifiers)
  {
    CFG_Node *binding = cfg_node_new(state, bindings, str8_zero());
    cfg_node_new(state, binding, str8f(arena, "command_%llu", idx));
    cfg_node_new(state, binding, str8_lit("p"));
    if(modifiers[idx] & WM_Modifier_Cmd)   {cfg_node_new(state, binding, str8_lit("cmd"));}
    if(modifiers[idx] & WM_Modifier_Ctrl)  {cfg_node_new(state, binding, str8_lit("ctrl"));}
    if(modifiers[idx] & WM_Modifier_Shift) {cfg_node_new(state, binding, str8_lit("shift"));}
    if(modifiers[idx] & WM_Modifier_Alt)   {cfg_node_new(state, binding, str8_lit("alt"));}
  }
  for(U32 pass = 0; pass < 2; pass += 1)
  {
    String8 saved = cfg_string_from_tree(arena, &schemas, str8_zero(), user);
    cfg_node_release(state, user);
    CFG_NodePtrList loaded = cfg_node_ptr_list_from_string(arena, state, &schemas, str8_zero(), saved);
    T_Ok(loaded.count == 1);
    user = loaded.first->v;
    cfg_node_insert_child(state, cfg_node_root(), cfg_node_root()->last, user);
    CFG_KeyMap *map = cfg_key_map_from_cfg(arena);
    for EachElement(idx, modifiers)
    {
      CFG_Binding binding = {WM_Key_P, modifiers[idx]};
      CFG_KeyMapNodePtrList found = cfg_key_map_node_ptr_list_from_binding(arena, map, binding);
      T_Ok(found.count == 1);
      T_Ok(str8_match(found.first->v->name, str8f(arena, "command_%llu", idx), 0));
    }
  }
}

TEST(config_command_control_bindings)
{
  CFG_Ctx *previous = cfg_ctx;
  CFG_State *state = cfg_state_alloc();
  cfg_ctx_select(cfg_state_ctx(state));
  t_check_binding_roundtrip(arena, ctx, state);
  cfg_ctx_select(previous);
  cfg_state_release(state);
}

TEST(mac_default_bindings)
{
  struct {String8 name; CFG_Binding binding;} bindings[ArrayCount(rd_default_binding_table) + ArrayCount(rd_mac_extra_binding_table)] = {0};
  for EachElement(idx, rd_default_binding_table)
  {
    CFG_Binding original = rd_default_binding_table[idx].binding;
    String8 name = rd_default_binding_table[idx].string;
    CFG_Binding other = rd_binding_from_default(name, original, 0);
    T_Ok(MemoryMatchStruct(&original, &other));
    bindings[idx].name = name;
    bindings[idx].binding = rd_binding_from_default(name, original, 1);
  }
  for EachElement(idx, rd_mac_extra_binding_table)
  {
    U64 dst = ArrayCount(rd_default_binding_table) + idx;
    bindings[dst].name = rd_mac_extra_binding_table[idx].string;
    bindings[dst].binding = rd_mac_extra_binding_table[idx].binding;
  }
  for EachElement(a, bindings) for(U64 b = a+1; b < ArrayCount(bindings); b += 1)
  {
    T_Ok(!MemoryMatchStruct(&bindings[a].binding, &bindings[b].binding) || str8_match(bindings[a].name, bindings[b].name, 0));
  }
  struct {String8 name; CFG_Binding binding;} expected[] =
  {
    {str8_lit("open_palette"), {WM_Key_P, WM_Modifier_Cmd|WM_Modifier_Shift}},
    {str8_lit("move_left_chunk_select"), {WM_Key_Left, WM_Modifier_Alt|WM_Modifier_Shift}},
    {str8_lit("move_home"), {WM_Key_Left, WM_Modifier_Cmd}},
    {str8_lit("move_up_whole"), {WM_Key_Up, WM_Modifier_Cmd}},
    {str8_lit("next_tab"), {WM_Key_Tab, WM_Modifier_Ctrl}},
    {str8_lit("redo"), {WM_Key_Z, WM_Modifier_Cmd|WM_Modifier_Shift}},
    {str8_lit("exit"), {WM_Key_Q, WM_Modifier_Cmd}},
    {str8_lit("minimize_window"), {WM_Key_M, WM_Modifier_Cmd}},
  };
  for EachElement(e, expected)
  {
    B32 found = 0;
    for EachElement(b, bindings)
    {
      found |= str8_match(expected[e].name, bindings[b].name, 0) && MemoryMatchStruct(&expected[e].binding, &bindings[b].binding);
    }
    T_Ok(found);
  }
}
