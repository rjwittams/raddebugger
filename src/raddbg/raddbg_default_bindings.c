// Licensed under the MIT license (https://opensource.org/license/mit/)

// Adapt upstream defaults; saved bindings are always interpreted literally.
internal CFG_Binding
rd_binding_from_default(String8 name, CFG_Binding binding, B32 mac)
{
  if(mac)
  {
    if((binding.modifiers & WM_Modifier_Ctrl) && binding.key != WM_Key_Tab && binding.key != WM_Key_Insert)
    {
      binding.modifiers = (binding.modifiers & ~WM_Modifier_Ctrl) | WM_Modifier_Cmd;
    }
    String8 base_name = name;
    if(name.size >= 7 && str8_match(str8_skip(name, name.size-7), str8_lit("_select"), 0))
    {
      base_name = str8_chop(name, 7);
    }
    struct {String8 name; WM_Key key; WM_Modifiers modifiers;} overrides[] =
    {
      {str8_lit("move_left_chunk"),  WM_Key_Left,      WM_Modifier_Alt},
      {str8_lit("move_right_chunk"), WM_Key_Right,     WM_Modifier_Alt},
      {str8_lit("move_up_chunk"),    WM_Key_Up,        WM_Modifier_Alt},
      {str8_lit("move_down_chunk"),  WM_Key_Down,      WM_Modifier_Alt},
      {str8_lit("delete_chunk"),     WM_Key_Delete,    WM_Modifier_Alt},
      {str8_lit("backspace_chunk"),  WM_Key_Backspace, WM_Modifier_Alt},
      {str8_lit("move_up_reorder"),  WM_Key_Up,        WM_Modifier_Ctrl|WM_Modifier_Alt},
      {str8_lit("move_down_reorder"),WM_Key_Down,      WM_Modifier_Ctrl|WM_Modifier_Alt},
      {str8_lit("go_back"),          WM_Key_LeftBracket, WM_Modifier_Cmd},
      {str8_lit("go_forward"),       WM_Key_RightBracket,WM_Modifier_Cmd},
      {str8_lit("redo"),             WM_Key_Z,         WM_Modifier_Cmd|WM_Modifier_Shift},
      {str8_lit("toggle_fullscreen"),WM_Key_F,         WM_Modifier_Ctrl|WM_Modifier_Cmd},
    };
    for EachElement(idx, overrides)
    {
      if(str8_match(base_name, overrides[idx].name, 0))
      {
        binding.key = overrides[idx].key;
        binding.modifiers = overrides[idx].modifiers | (binding.modifiers & WM_Modifier_Shift);
        break;
      }
    }
  }
  return binding;
}

global struct {String8 string; CFG_Binding binding;} rd_mac_extra_binding_table[] =
{
  {str8_lit_comp("exit"),                   {WM_Key_Q,     WM_Modifier_Cmd}},
  {str8_lit_comp("minimize_window"),        {WM_Key_M,     WM_Modifier_Cmd}},
  {str8_lit_comp("move_home"),              {WM_Key_Left,  WM_Modifier_Cmd}},
  {str8_lit_comp("move_end"),               {WM_Key_Right, WM_Modifier_Cmd}},
  {str8_lit_comp("move_up_whole"),          {WM_Key_Up,    WM_Modifier_Cmd}},
  {str8_lit_comp("move_down_whole"),        {WM_Key_Down,  WM_Modifier_Cmd}},
  {str8_lit_comp("move_home_select"),       {WM_Key_Left,  WM_Modifier_Cmd|WM_Modifier_Shift}},
  {str8_lit_comp("move_end_select"),        {WM_Key_Right, WM_Modifier_Cmd|WM_Modifier_Shift}},
  {str8_lit_comp("move_up_whole_select"),   {WM_Key_Up,    WM_Modifier_Cmd|WM_Modifier_Shift}},
  {str8_lit_comp("move_down_whole_select"), {WM_Key_Down,  WM_Modifier_Cmd|WM_Modifier_Shift}},
};
