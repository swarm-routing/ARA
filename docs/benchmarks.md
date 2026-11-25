# Benchmarks

ARA's change tests and anchors run in this repository; released versions are
recorded in [swarm-routing/benchmarks](https://github.com/swarm-routing/benchmarks)
(its ADR-0002). Each page below is a pre-registered run, its verdict, and its
raw cells in `results/cells/<generation>/`.

| Page | What it is | Verdict |
|---|---|---|
| [v0.2.0 anchor](benchmarks/v0.2.0-anchor.md) | The paper's trend ([G02] §4.2), ARA minus AODV, 4 pause times × 20 paired seeds; the default and `BufferDuringDiscovery` on; the switch's A/B | **misses**: written infeasibility verdict ([#5](https://github.com/swarm-routing/ARA/issues/5)) |
| [v0.3.0 anchor](benchmarks/v0.3.0-anchor.md) | The 2003 version's trend ([GS03] §5.3), ARA 2003 (every 2003 switch on) minus AODV, Broch's traffic, 4 pause times × 20 paired seeds | **misses**: PDR −15.94 pp [−18.84, −13.05] at pause 0 and −13.83 [−16.14, −11.52] at 30 s, outside the 5 pp margin; NRL below AODV's; written infeasibility verdict ([#125](https://github.com/swarm-routing/ARA/issues/125)) |
| [q sweep](benchmarks/q-sweep.md) | The evaporation rate at the 2003 chapter's two readings, q = 0.1 against 0.9: the anchor's pause 0 and 30 s and the two smoke families × 20 paired seeds, AODV beside ARA | q = 0.9 is −1.94 pp PDR [−3.48, −0.39] on the near-static mesh; not resolved in the other cells (CIs span zero); ARA minus AODV stays below zero at pause 0 and 30 s with either; the default stays 0.1 pending [#140](https://github.com/swarm-routing/ARA/issues/140) |
