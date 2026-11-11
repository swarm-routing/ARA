## v0.1.0 (2026-11-11)
### Release status

v0.1.0 is cut before the comparator gates (maintainer decision 2026-11-10). It has **not** cleared the three gates of org golden rule 6 (a fidelity sheet from its source, the per-PR delivery smoke, and an anchor reproducing the source's own trend). The anchor is pending. No result from this release counts as a benchmark number, and it does not enter the benchmarks suite until a release clears the gates; that gated release is proposed as v0.2.0 (swarm-routing/.github ROADMAP "Releases"). It ships `core/` (MIT OR GPL-2.0-only, with `core/LICENSE`), the ns-3 contrib module `ara` (`ns3/`, GPL-2.0-only) and `docs/`, as they stand on `main` at the release commit.


### Feat

- **core**: route maintenance by data, Δϕ toward both ends on every hop (#44)
- **ns3**: the ara contrib module on swarm-core: codec, adapter, helper, arm, tests (#40)
- **core**: route discovery on swarm-core: fant/bant, trails, evaporation, retries (#33)
