# CONTEXT.md — orientation and history

Background an agent or a new contributor needs that does not belong in
`AGENTS.md`: domain notes, current state, glossary, open questions. Read the
section a task needs, not the whole file.

## Current state

As of 2026-11-13 (`main` after #98, #101, #104; v0.1.0 released 2026-11-11,
ungated). `AGENTS.md` "What this repo is" has the layout; this is where the
work stands.

- **Implemented.** `core/` has the three slices of #5: route discovery
  ([G02] §3.1: FANT/BANT, trails, evaporation, next hop, discovery retries,
  the ant wire header), route maintenance by data (§3.2: Δϕ toward both ends
  on every hop) and loop prevention with route failure (§3.2, §3.3: duplicate
  errors, another link or backtracking to the previous hop, a new discovery
  at the source). The ns-3 module `ara` carries all three out on swarm-core's
  `SwarmRoutingBase` (#96, #98); the core reports through swarm-core's
  `IRouterObserver`. Every rule names its `docs/fidelity.md` row.
- **What remains of #5: the anchor** (the third gate of org rule 6).
  No plan is cleared for dispatch. `tools/bench/campaigns/anchor.yaml`, the
  first pre-registration (`source_sha` c7d6acb, #101; it reads ARA minus AODV,
  swarm-routing/ns3-tools#152), is superseded and was never dispatched: it
  names swarm-core 0919dbf, which its cells would not have built. It is kept
  unchanged as the record. swarm-core's `core/` changed again
  (swarm-routing/ns3-swarm-core#104), so the pin moved to 6966421, in
  `benchmark-cell.yml` too. The anchor is pre-registered anew in a follow-up
  PR, as a new plan at the `source_sha` that merges that pin bump and at
  swarm-core 6966421 (maintainer decision 2026-11-13, #105).
  `tools/bench/check_plan_deps.py` must pass on that plan when it is
  pre-registered and again right before the dispatch.
- **Gates.** Fidelity sheet: written (`docs/fidelity.md`). Per-PR delivery
  smoke: in `.github/workflows/ns3.yml`. Anchor: pending, so no number from
  this repository counts as a benchmark result yet (README "Release status").
- **Next releases.** `docs/roadmap.md`: v0.2.0 "gated" waits for the anchor
  verdict; v0.3.0 holds the paper-review and performance issues.

## Glossary

## Open questions
