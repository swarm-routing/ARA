# Benchmarks

ARA's change tests and anchors run in this repository; released versions are
recorded in [swarm-routing/benchmarks](https://github.com/swarm-routing/benchmarks)
(its ADR-0002). Each page below is a pre-registered run, its verdict, and its
raw cells in `results/cells/<generation>/`.

| Page | What it is | Verdict |
|---|---|---|
| [v0.2.0 anchor](benchmarks/v0.2.0-anchor.md) | The paper's trend ([G02] §4.2), ARA minus AODV, 4 pause times × 20 paired seeds; the default and `BufferDuringDiscovery` on; the switch's A/B | **misses**: written infeasibility verdict ([#5](https://github.com/swarm-routing/ARA/issues/5)) |
