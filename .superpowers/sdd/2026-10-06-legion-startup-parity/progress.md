# SDD ledger — plan: docs/superpowers/plans/2026-10-06-legion-startup-parity.md

Task 1: in progress
- Baseline evidence: `wc3-parity/artifacts/legion-ui03/visual/stderr.log` shows local map load progressing at 0% through 62 seconds, map registration from ~66 to ~107 seconds, followed by `CL_Disconnect: Connection to host timed out.`
- Root cause: `CL_TIMEOUT_MSEC` is 10000; `CL_CheckTimeout()` only consults packet time. `CL_LoadingFrame()` services the in-process listen server once per load step but does not signal its liveness to the timeout check.
- Full-map observer also recorded 24 playing slots and multiboard errors from an older binary (`10ecaa24...`). Slot fix exists in baseline commit `55cd3a44`; rerun with current binary before deciding if this remains a defect.
- Current user-mode map and race preference question is pending asynchronously; proceed with one standard race and several authored computer slots meanwhile.
