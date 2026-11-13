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
  `tools/bench/campaigns/anchor.yaml` is pre-registered at `source_sha`
  c7d6acb (#101) and reads ARA minus AODV (swarm-routing/ns3-tools#152). It
  has not been cleared for dispatch: the plan names swarm-core 0919dbf, while
  `benchmark-cell.yml` on `main` builds 501dd7e. Which commit the anchor runs
  on is the maintainer's decision (#105);
  `tools/bench/check_plan_deps.py` shows the disagreement and fails until a
  plan and the workflow agree.
- **Gates.** Fidelity sheet: written (`docs/fidelity.md`). Per-PR delivery
  smoke: in `.github/workflows/ns3.yml`. Anchor: pending, so no number from
  this repository counts as a benchmark result yet (README "Release status").
- **Next releases.** `docs/roadmap.md`: v0.2.0 "gated" waits for the anchor
  verdict; v0.3.0 holds the paper-review and performance issues.

## Glossary

## Open questions
