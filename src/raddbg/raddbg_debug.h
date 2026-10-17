#ifndef RADDBG_DEBUG_H
#define RADDBG_DEBUG_H

internal String8 rd_snapshot_json_string(Arena *arena, String8 value);
internal String8 rd_debug_snapshot(Arena *arena);
internal String8 rd_debug_eval(Arena *arena, String8 request);

#endif // RADDBG_DEBUG_H
