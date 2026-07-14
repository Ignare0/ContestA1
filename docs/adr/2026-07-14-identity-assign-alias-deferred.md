# ADR: Defer full-width identity-assign net alias to Phase 3

**Status:** Accepted  
**Date:** 2026-07-14  
**Context:** A1 simulator Phase 2 scheduler / continuous assignment

## Context

Phase 2 settles continuous assignments only after the Active queue drains (processes run-to-completion; updates do not preempt the running process). This matches iverilog 12.0 observables for expression continuous assigns (e.g. `assign y = ~a`, part-selects): a blocking write followed by an immediate read in the same process sees the **old** wire value until `#0` (modeled as a same-slot pending queue after settle/wake) resumes.

iverilog treats a **full-width identity** continuous assign (`assign y = a` with matching width, no operators) as a **net alias**: the same process can observe the new value on `y` immediately after writing `a`. Module port connections are essentially identity continuous assigns; Phase 3 hierarchy/port lowering will need alias (or shared `SignalId`) behavior both for golden alignment and performance.

## Decision

- Phase 2 does **not** special-case identity assigns as aliases. They go through the normal continuous-assign settle path (same-process immediate read may see a stale value).
- Phase 3 port binding / hierarchy lowering will unify full-width identity connects as aliases (shared storage / equivalent observability to iverilog).

## Consequences

- Phase 2 fixtures must not rely on identity-assign immediate visibility; use `#0` or `#delay` when sampling wires after driving RHS regs.
- No Phase 2 API or IR change is required for this deferral; document the known difference so it is not mistaken for a scheduler bug.
- Phase 3 plans must include identity-alias lowering as part of port binding, not as a surprise fix during basic01 debug.
