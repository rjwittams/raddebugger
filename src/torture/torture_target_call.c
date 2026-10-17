// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#undef T_Group
#define T_Group "Darwin Target Call/TLS"

thread_static E_TLSVAddrResolution t_platform_tls_resolution;

internal E_TLSVAddrResolution
t_tls_vaddr_from_platform_vaddr(E_Space space, U64 platform_tls_vaddr)
{
  return t_platform_tls_resolution;
}

TEST(platform_tls_resolution_retry_policy)
{
  T_Ok(d_platform_tls_resolution_needs_request(0, 0, D_PlatformTLSResolutionStatus_Null));

  T_Ok(!d_platform_tls_resolution_needs_request(1, 0, D_PlatformTLSResolutionStatus_Pending));
  T_Ok(!d_platform_tls_resolution_needs_request(1, 0, D_PlatformTLSResolutionStatus_Resolved));
  T_Ok(!d_platform_tls_resolution_needs_request(1, 0, D_PlatformTLSResolutionStatus_Failed));

  T_Ok(d_platform_tls_resolution_needs_request(1, 1, D_PlatformTLSResolutionStatus_Pending));
  T_Ok(!d_platform_tls_resolution_needs_request(1, 1, D_PlatformTLSResolutionStatus_Resolved));
  T_Ok(d_platform_tls_resolution_needs_request(1, 1, D_PlatformTLSResolutionStatus_Failed));
}

TEST(platform_tls_resolution_event_round_trip)
{
  D_Event event = {0};
  event.kind = D_EventKind_PlatformTLSResolution;
  event.msg_id = 42;
  event.entity = d_handle_zero();
  event.platform_tls_status = D_PlatformTLSResolutionStatus_Failed;
  event.vaddr_rng = r1u64(0x12340000, 0);
  event.string = str8_lit("target call was interrupted");

  String8 serialized = d_serialized_string_from_event(arena, &event, 4096);
  D_Event round_trip = d_event_from_serialized_string(arena, serialized);
  T_Ok(round_trip.kind == event.kind);
  T_Ok(round_trip.msg_id == event.msg_id);
  T_Ok(round_trip.platform_tls_status == event.platform_tls_status);
  T_Ok(round_trip.vaddr_rng.min == event.vaddr_rng.min);
  T_Ok(round_trip.vaddr_rng.max == event.vaddr_rng.max);
  T_Ok(str8_match(round_trip.string, event.string, 0));
}

TEST(platform_tls_resolution_eval_statuses)
{
  E_BaseCtx *old_base_ctx = e_base_ctx;
  E_InterpretCtx *old_interpret_ctx = e_interpret_ctx;
  E_Cache *old_cache = e_cache;

  E_Cache *test_cache = e_cache_alloc();
  e_select_cache(test_cache);

  E_BaseCtx base_ctx = {0};
  base_ctx.tls_vaddr_from_platform_vaddr = t_tls_vaddr_from_platform_vaddr;
  e_select_base_ctx(&base_ctx);

  U64 tls_base = 0x4000;
  E_InterpretCtx interpret_ctx = {0};
  interpret_ctx.tls_base = &tls_base;
  e_select_interpret_ctx(&interpret_ctx, 0, 0);

  E_OpList oplist = {0};
  e_oplist_push_op(arena, &oplist, RDI_EvalOp_TLSOff, e_value_u64(0x20));
  String8 bytecode = e_bytecode_from_oplist(arena, &oplist);

  t_platform_tls_resolution = (E_TLSVAddrResolution){E_TLSVAddrResolutionStatus_Unhandled, 0};
  E_Interpretation unhandled = e_interpret(bytecode);
  T_Ok(unhandled.code == E_InterpretationCode_Good);
  T_Ok(unhandled.value.u64 == 0x4020);

  t_platform_tls_resolution = (E_TLSVAddrResolution){E_TLSVAddrResolutionStatus_Pending, 0};
  E_Interpretation pending = e_interpret(bytecode);
  T_Ok(pending.code == E_InterpretationCode_BadTLSBase);

  t_platform_tls_resolution = (E_TLSVAddrResolution){E_TLSVAddrResolutionStatus_Failed, 0};
  E_Interpretation failed = e_interpret(bytecode);
  T_Ok(failed.code == E_InterpretationCode_BadTLSBase);

  t_platform_tls_resolution = (E_TLSVAddrResolution){E_TLSVAddrResolutionStatus_Resolved, 0x8000};
  E_Interpretation resolved = e_interpret(bytecode);
  T_Ok(resolved.code == E_InterpretationCode_Good);
  T_Ok(resolved.value.u64 == 0x8000);

  t_platform_tls_resolution = (E_TLSVAddrResolution){E_TLSVAddrResolutionStatus_Resolved, 0};
  E_Interpretation resolved_zero = e_interpret(bytecode);
  T_Ok(resolved_zero.code == E_InterpretationCode_BadTLSBase);

  e_base_ctx = old_base_ctx;
  e_interpret_ctx = old_interpret_ctx;
  e_select_cache(old_cache);
  e_cache_release(test_cache);
}
