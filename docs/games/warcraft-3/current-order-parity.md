# Current order and issued-order events

`G_GetIssuedOrderId` stores the last published order for issued-order trigger
responses. `GetIssuedOrderId` must retain that event identity. Lua and JASS
`GetUnitCurrentOrder` instead query `G_GetCurrentOrderId`.

The first confirmed correction is ordinary point Move: while the unit's
production walk behavior is active, the current query returns Move (851986).
After that behavior reaches its destination or changes to terminal hold/idle,
the current query returns 0. The last issued-order record remains unchanged.
Other ability orders retain their existing behavior; their completion semantics
require separate original measurements before extending this correction.

## Evidence

Three original 3.0.0.24268 SD lab-map runs created hfoo at (0,0), issued Move to
(512,0) at timer elapsed 1, and sampled at 0, .5, 1, 1.5, 2, 3 and 5 seconds.
Original current order was 851986 at 1/1.5/2 and 0 at 3/5. Before correction,
OpenRealm returned 851986 at 3/5 because its Lua/JASS query read the issued-order
event cache. HP, owner, timer callbacks and order acceptance matched.

The correction changes the query owner rather than erasing event history or
emitting a synthetic Stop. It adds no persistent state or serializer fields.
The regression `wc3_mapscript.lua_completed_move_has_no_current_order_but_preserves_issued_event`
executes the real Lua IssuePointOrder path and movement callback, then verifies
both the completed current query and preserved issued event.

Position parity remains unresolved: the original's repeated endpoint was
(502.173096,-0.029907), while OpenRealm snapped to (512,0). Original x at 1.5
and 2 was 129.594727 and 264.588867, versus 135 and 270. The coordinator reports
these differences without broadening tolerances or treating this one fix as
complete movement compatibility.

The local `wc3-parity` coordinator retains paired map hashes, raw logs, three
original runs, before/after OpenRealm results and comparison reports. Its harness
is external to this engine repository and runs the same map bytes in each backend.

## Validation limits

The focused Lua suite passed Classic and TFT (64 tests, 255 assertions per mode).
The movement suite's seven failed assertions belong to its existing
`unload_all_round_trip_resumes_remaining_cargo` save fixture using native `/tmp`;
they are separate from current-order behavior. The Windows full `make test` build
still fails on `tests/test_net.c` including unavailable `arpa/inet.h`.
