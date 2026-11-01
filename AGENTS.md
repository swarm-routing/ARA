# AGENTS.md — guide for AI coding agents

Operational guide for LLM agents working in `swarm-routing/ARA`. Read this
before changing anything. Org-wide conventions:
[`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md). Shape and rules mirror
[AntHocNet's AGENTS.md](https://github.com/danieljoppi/AntHocNet/blob/main/AGENTS.md), the source repository.

## What this repo is

**Status: empty, populated in Phase 3.** The layout below is the plan; update this file in the PR that creates it.

ARA, the Ant-Colony-Based Routing Algorithm (Güneş, Sorges & Bouazizi, 2002),
as a standalone hexagonal protocol: a simulator-agnostic `core/` and an ns-3
`ns3/` adapter (contrib module `ara`) linking against `swarm-core`. It
replaces AntHocNet's `ara` comparator arm.

```
core/       simulator-agnostic algorithm + unit tests (no ns-3 headers)
ns3/        contrib module `ara`: header codec, adapter, helper, examples, test suite
docs/       fidelity sheet (paper section -> implementation), parameters with sources
```

## Build & verify

CI is [`reusable-ns3-ci.yml`](https://github.com/swarm-routing/.github/blob/main/.github/workflows/reusable-ns3-ci.yml):
the module builds inside `ghcr.io/swarm-routing/ns3-dev:3.43-{debug,sanitized}`
(warm ns-3 tree; only the module compiles) and runs its test.py suites. The
same locally:

```bash
docker run --rm -v "$PWD:/src/ara:ro" ghcr.io/swarm-routing/ns3-dev:3.43-debug \
    ns3-build-contrib --test swarm-core=/src/ns3-swarm-core ara=/src/ara/ns3
```

(mount a checkout of `swarm-routing/ns3-swarm-core` at `/src/ns3-swarm-core`).
CI passes `dependencies: swarm-core=swarm-routing/ns3-swarm-core@main`.

## Golden rules (invariants — do not break)

Org-wide (1–9), then this repo's own.

1. **SPDX header on every original source file.** `.h`, `.cc`, `.cpp`, `.py`
   (and scripts, workflows) start with
   `// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)` +
   `// SPDX-License-Identifier: MIT` (`#` comments where needed). CI checks it.
2. **No GPL code in an MIT repo.** Never copy from ns-3's `src/aodv`, `src/olsr`
   or any other ns-3 module; implement against public ns-3 interfaces. From
   AntHocNet, only the `MIT OR GPL-2.0-only` paths (`core/`, `tools/`,
   `ns3/tools/`, `docs/tools/`) may be extracted as-is. Its `ns3/` modules
   (the `adaptive` arms, `routing-bench`, `oracle`) are **GPL-2.0-only**:
   bringing one here needs the copyright holder's explicit relicensing
   decision recorded in an issue first. `ns3/baselines/aomdv` and `gpsr` are
   third-party GPL and never come here.
3. **Wire formats are POD + versioned.** Control headers are
   simulator-independent POD structs with bounds-checked little-endian
   readers/writers, a leading version byte, and deterministic roundtrip + fuzz
   tests. Changing a field or its units bumps the version byte.
4. **No contrib module named `core`** (collides with ns-3's `src/core`). The
   shared module is `swarm-core` (`#include "ns3/swarm-core-module.h"`).
5. **Determinism.** A fixed `--seed=<N>` gives byte-identical output. All
   randomness through the simulator's seeded streams or an injected RNG port;
   never libc `rand()`, wall-clock time, or unordered-container iteration order
   in anything that reaches an output.
6. **Comparisons are paired.** Per-seed deltas across >= 20 seeds, emitting the
   canonical `##CONFIG##`, `##RUN##`, `##BENCH##`, `##DIAG##` lines. A
   comparator arm (a protocol we did not design) counts only after its three
   gates: a fidelity sheet from its paper, the per-PR delivery smoke, and an
   anchor reproducing the paper's own trend.
7. **Keep bounded structures bounded** (visited paths, dedup histories, queues
   carry a configured cap).
8. **CI hygiene.** Actions pinned by full commit SHA with a `# vX.Y.Z` comment;
   buildx caches scoped per profile; transient artifacts `retention-days` <= 7.
9. **Nothing shipped depends on `.claude/`.** Skills hold procedures; scripts
   that anything shipped needs live in `tools/` (AntHocNet ADR-0014).
10. **`core/` never includes a simulator header**; the adapter only converts headers, executes `RouteDecision`s and owns timers/queues (AntHocNet golden rules 1–2).
11. **Paper fidelity is written down.** Every parameter default cites its paper section or is marked a repo choice; deviations live in `docs/fidelity.md`.
12. **AntHocNet's `ara` arm is GPL-2.0-only.** Reimplement from the paper and the fidelity notes, or get the relicensing decision recorded in an issue before reusing its code (rule 2).

## Conventions

- Issues follow the org taxonomy ([CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md#issues), from
  [AntHocNet ADR-0013](https://github.com/danieljoppi/AntHocNet/blob/main/docs/adr/0013-track-bugs-and-findings-as-issues.md)): one type label
  (`bug`/`enhancement`/`chore`/`documentation`/`verification`, plus `epic` for
  umbrellas), area label(s) (`protocol`/`adapter`/`ns3`/`benchmark`/
  `observability`/`packaging`/`learn-site`/`literature`), one `model:*`
  recommendation, one `priority:P1|P2|P3` on non-epic issues, a
  `release:vX.Y.Z` label only when a roadmap gates the issue. Labels come from
  `labels.json` via the `sync-labels` workflow.
- Issue bodies: **Context** (evidence, numbers, links) · **Scope** ·
  **Acceptance criteria** (checkboxes) · **Depends on** (cross-repo form
  `swarm-routing/<repo>#N`) · **References**. Children of an epic say "Part of
  #N" and are linked as sub-issues. Comments you post end with the Claude Code
  footer.
- Issues migrated from AntHocNet carry `migrated` and start with *Migrated from
  `danieljoppi/AntHocNet#N`*.
- Make minimal, reviewable changes; update the relevant doc/ADR when you change
  a documented decision.

## Git workflow

- Branch from `main` (`claude/<short-topic>`), one focused change per PR.
- **Always open a PR to `main`** once the branch is pushed — every change is
  tracked through one.
- **PRs are squash-merged** (the only merge method enabled). The PR title is
  the commit on `main`, so it is a lowercase Conventional Commit
  (`feat:`/`fix:`/`docs:`/`chore:`/`ci:`…, optional scope).
- Push with `git push -u origin <branch>`; merge when CI is green.

## Where to look

| You want to… | Go to |
|--------------|-------|
| The arm being promoted (reference only, GPL) | AntHocNet `ns3/baselines/adaptive/model/ara-routing-protocol.{h,cc}`, AntHocNet #586 (closed) |
| Shared primitives | `swarm-routing/ns3-swarm-core` |
| Benchmark against AODV/OLSR/AntHocNet | `swarm-routing/ns3-tools` arenas, results into `swarm-routing/benchmarks` |
| The org's invariants, labels, issue shape | [`swarm-routing/.github` CONTRIBUTING.md](https://github.com/swarm-routing/.github/blob/main/CONTRIBUTING.md) |
| The reference implementation and its history | [`danieljoppi/AntHocNet`](https://github.com/danieljoppi/AntHocNet) (`AGENTS.md`, `CONTEXT.md`, `docs/adr/`) |
| Pick up open work | this repo's issues — highest open `priority:P*` first; newest open `handoff` issue for work in flight |
