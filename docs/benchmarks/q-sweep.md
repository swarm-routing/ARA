# The q sweep: 0.1 against 0.9

**Status: complete (2026-11-13). q = 0.9 delivers less on the near-static
mesh; elsewhere its effect is not resolved; the anchor's comparison with AODV
stays below zero with either.** The authors' 2003 chapter decreases the
pheromone every second "with a factor of (1 − q) = 0.1" (Günes & Spaniol, IFIP
Net-Con 2003, <https://doi.org/10.1007/978-0-387-35703-4_9>, §4.2), which reads
as q = 0.1, the repo's default, or literally as q = 0.9. Against q = 0.1,
q = 0.9 is −1.94 pp PDR [−3.48, −0.39] on the near-static mesh. In the other
three cells the PDR intervals span zero: that is no difference shown, not
equivalence shown (at pause 30 s the interval reaches −6.58 pp). NRL is
+0.12 [+0.04, +0.20] at pause 0. Neither reading brings ARA's PDR to AODV's at
pause 0 or 30 s, so the ambiguity does not explain the v0.2.0 anchor's miss.
The default stays 0.1 pending the maintainer's decision on
[#140](https://github.com/swarm-routing/ARA/issues/140).

[← Benchmark index](../benchmarks.md) · [Fidelity sheet](../fidelity.md#parameters) · [The v0.2.0 anchor](v0.2.0-anchor.md)

## What was fixed before dispatch

`tools/bench/campaigns/q-sweep.yaml` was committed in #142 (at 3436592) before
any dispatch, and `tools/bench/check_plan_deps.py` passed on it right before.

- **Commit and image.** `source_sha` 093b42c (main with #134, which made
  `core::Config`'s fields ns-3 attributes); ns3-dev 3.48-optimized by digest,
  the anchor's image; swarm-core 6966421, routing-bench 3d279b8.
- **Arms.** `q01` (the default) and `q09`
  (`--ns3::ara::RoutingProtocol::EvaporationRate=0.9`), differing only in that
  argument (`isolates: args`).
- **Cells.** anchor-3.yaml's gated cells (pause 0 and 30 s, 900 s, 20 seeds in
  two blocks of 10) and smoke.yaml's two families (`grid-rwp`, `static-mesh`,
  300 s, 20 seeds). AODV ran beside ARA in every cell (`reference: aodv`).
- **Read.** `ns3-campaign verdicts` (q09 minus q01 per cell, and each arm's ARA
  minus AODV) and `anchor_extra.py`. No pass rule.
- **Expected, stated before dispatch.** PDR down and routing packets up with
  q = 0.9; q01's anchor cells byte-identical to anchor-3's.

## Results

Provenance (`python3 -I tools/bench/check_plan_deps.py tools/bench/campaigns/q-sweep.yaml --cells results/cells/q-sweep`):

```
ok   8 cells, 12 ##PROV## lines: swarm-core@e20aad3f92dfd253d7c6411f648b90a32eee5038, routing-bench@3d279b83392a00288050574cee4288f30205658b
plan deps: ok
```

`python3 ../ns3-tools/python/ns3_tool/campaign.py verdicts tools/bench/campaigns/q-sweep.yaml --dir results/cells/q-sweep`
(or `ns3-campaign verdicts ...` with ns3-tools installed):

```
q09	anchor-p0	ara	paired 95% CI (n=20): dPDR -0.42 [-3.26,+2.42]pp p=0.433  d_d99 -0.30 [-0.85,+0.25]ms p=0.297  d_NRL +0.12 [+0.04,+0.20] p=0.0157 -> PAIRED-WORSE
q09	anchor-p0	aodv	paired 95% CI (n=20): dPDR +0.00 [+0.00,+0.00]pp p=n/a  d_d99 +0.00 [+0.00,+0.00]ms p=n/a  d_NRL +0.00 [+0.00,+0.00] p=n/a -> PAIRED-NOISE
q09	anchor-p30	ara	paired 95% CI (n=20): dPDR -2.86 [-6.58,+0.86]pp p=0.113  d_d99 -0.85 [-1.80,-0.10]ms p=0.0842  d_NRL -0.00 [-0.08,+0.08] p=0.926 -> PAIRED-IMPROVED
q09	anchor-p30	aodv	paired 95% CI (n=20): dPDR +0.00 [+0.00,+0.00]pp p=n/a  d_d99 +0.00 [+0.00,+0.00]ms p=n/a  d_NRL +0.00 [+0.00,+0.00] p=n/a -> PAIRED-NOISE
q09	grid-rwp	ara	paired 95% CI (n=20): dPDR -1.38 [-4.82,+2.05]pp p=0.729  d_d99 -1.25 [-2.20,-0.25]ms p=0.0328  d_NRL +0.04 [-0.10,+0.18] p=0.401 -> PAIRED-IMPROVED
q09	grid-rwp	aodv	paired 95% CI (n=20): dPDR +0.00 [+0.00,+0.00]pp p=n/a  d_d99 +0.00 [+0.00,+0.00]ms p=n/a  d_NRL +0.00 [+0.00,+0.00] p=n/a -> PAIRED-NOISE
q09	static-mesh	ara	paired 95% CI (n=20): dPDR -1.94 [-3.48,-0.39]pp p=0.00891  d_d99 -0.05 [-0.20,+0.10]ms p=0.773  d_NRL +0.01 [+0.00,+0.01] p=0.0213 -> PAIRED-WORSE
q09	static-mesh	aodv	paired 95% CI (n=20): dPDR +0.00 [+0.00,+0.00]pp p=n/a  d_d99 +0.00 [+0.00,+0.00]ms p=n/a  d_NRL +0.00 [+0.00,+0.00] p=n/a -> PAIRED-NOISE
q01	anchor-p0	ara - aodv	paired 95% CI (n=20): dPDR -12.68 [-15.21,-10.15]pp p=1.91e-06  d_d99 -449.90 [-469.35,-429.85]ms p=9.54e-05  d_NRL -50.58 [-52.14,-49.02] p=1.91e-06 -> PAIRED-MIXED
q01	anchor-p30	ara - aodv	paired 95% CI (n=20): dPDR -11.56 [-14.78,-8.33]pp p=1.91e-06  d_d99 -439.25 [-452.90,-424.95]ms p=1.91e-06  d_NRL -48.06 [-49.17,-46.95] p=1.91e-06 -> PAIRED-MIXED
q01	grid-rwp	ara - aodv	paired 95% CI (n=20): dPDR -15.95 [-19.10,-12.79]pp p=9.56e-05  d_d99 -587.30 [-629.45,-542.35]ms p=9.56e-05  d_NRL -28.19 [-29.14,-27.24] p=9.56e-05 -> PAIRED-MIXED
q01	static-mesh	ara - aodv	paired 95% CI (n=20): dPDR +4.76 [+4.07,+5.44]pp p=9.44e-05  d_d99 -228.50 [-260.85,-199.25]ms p=9.54e-05  d_NRL -19.20 [-20.53,-17.87] p=1.91e-06 -> PAIRED-IMPROVED
q09	anchor-p0	ara - aodv	paired 95% CI (n=20): dPDR -13.10 [-16.09,-10.11]pp p=9.56e-05  d_d99 -450.20 [-469.60,-430.25]ms p=9.56e-05  d_NRL -50.46 [-52.00,-48.92] p=1.91e-06 -> PAIRED-MIXED
q09	anchor-p30	ara - aodv	paired 95% CI (n=20): dPDR -14.42 [-18.42,-10.41]pp p=9.56e-05  d_d99 -440.10 [-453.90,-425.55]ms p=9.56e-05  d_NRL -48.06 [-49.19,-46.94] p=1.91e-06 -> PAIRED-MIXED
q09	grid-rwp	ara - aodv	paired 95% CI (n=20): dPDR -17.33 [-20.82,-13.84]pp p=1.91e-06  d_d99 -588.55 [-630.85,-543.50]ms p=1.91e-06  d_NRL -28.15 [-29.14,-27.16] p=1.91e-06 -> PAIRED-MIXED
q09	static-mesh	ara - aodv	paired 95% CI (n=20): dPDR +2.82 [+1.41,+4.23]pp p=0.00206  d_d99 -228.55 [-260.90,-199.20]ms p=9.54e-05  d_NRL -19.19 [-20.52,-17.87] p=1.91e-06 -> PAIRED-IMPROVED
```

`NS3_TOOL_PY=../ns3-tools/python/ns3_tool python3 -I tools/bench/anchor_extra.py results/cells/q-sweep/*.txt`
(ARA minus AODV per arm; the files without `-q09` are q01; `NS3_TOOL_PY`
names the directory holding ns3-tools' `stats_util.py`, the script's default
being `../ns3-tools/python/ns3_tool`):

```
anchor-p0-q-sweep-q09	ara - aodv	ctrlTx ara 13787.5 aodv 379446.8: d -365659.3 [-376110.7,-355207.9] (n=20) p=1.91e-06  nrl_bytes ara 1.0773 aodv 27.4811: d -26.4037 [-27.2129,-25.5945] (n=20) p=1.91e-06
anchor-p0-q-sweep	ara - aodv	ctrlTx ara 13190.1 aodv 379446.8: d -366256.7 [-376722.9,-355790.5] (n=20) p=1.91e-06  nrl_bytes ara 1.0204 aodv 27.4811: d -26.4607 [-27.2798,-25.6415] (n=20) p=1.91e-06
anchor-p30-q-sweep-q09	ara - aodv	ctrlTx ara 12480.2 aodv 361133.0: d -348652.8 [-356877.7,-340427.9] (n=20) p=1.91e-06  nrl_bytes ara 0.9930 aodv 26.0917: d -25.0987 [-25.6861,-24.5113] (n=20) p=1.91e-06
anchor-p30-q-sweep	ara - aodv	ctrlTx ara 13051.4 aodv 361133.0: d -348081.7 [-355970.5,-340192.8] (n=20) p=1.91e-06  nrl_bytes ara 0.9944 aodv 26.0917: d -25.0973 [-25.6784,-24.5163] (n=20) p=1.91e-06
grid-rwp-q-sweep-q09	ara - aodv	ctrlTx ara 11268.4 aodv 118379.2: d -107110.9 [-110301.0,-103920.7] (n=20) p=1.91e-06  nrl_bytes ara 1.8290 aodv 16.8236: d -14.9946 [-15.5092,-14.4799] (n=20) p=1.91e-06
grid-rwp-q-sweep	ara - aodv	ctrlTx ara 11268.6 aodv 118379.2: d -107110.6 [-110254.7,-103966.4] (n=20) p=1.91e-06  nrl_bytes ara 1.8113 aodv 16.8236: d -15.0123 [-15.5070,-14.5176] (n=20) p=1.91e-06
static-mesh-q-sweep-q09	ara - aodv	ctrlTx ara 1036.0 aodv 80970.4: d -79934.4 [-84708.7,-75160.1] (n=20) p=1.91e-06  nrl_bytes ara 0.1155 aodv 10.0766: d -9.9611 [-10.6608,-9.2614] (n=20) p=1.91e-06
static-mesh-q-sweep	ara - aodv	ctrlTx ara 1036.0 aodv 80970.4: d -79934.3 [-84708.6,-75160.0] (n=20) p=1.91e-06  nrl_bytes ara 0.1131 aodv 10.0766: d -9.9635 [-10.6631,-9.2639] (n=20) p=1.91e-06
```

## Reading

- **Delivery.** q = 0.9 against q = 0.1 is −1.94 pp [−3.48, −0.39] on the
  near-static mesh. There, ARA's `# diag` lines show flows with no delivery at
  all in 5 of the 20 seeds with q = 0.9 (one flow in 4 seeds, two in 1), and in
  none with q = 0.1. In the other three cells the CI spans zero (−0.42, −2.86,
  −1.38 pp): no difference shown, and none ruled out.
- **Routing load.** NRL is +0.12 [+0.04, +0.20] at pause 0 and +0.01 on the
  mesh; elsewhere the CIs span zero. The routing-packet means move by a few
  percent either way.
- **Delay.** p99 delay is about a millisecond lower with q = 0.9 at pause 30 s
  and on `grid-rwp`, which is why `verdicts` labels those two cells
  PAIRED-IMPROVED while PDR is not better.
- **AODV** is identical across the arms (PAIRED-NOISE in every cell): only
  ARA's attribute differs.
- **The anchor.** ARA minus AODV PDR at pause 0 and 30 s is −12.68 and
  −11.56 pp with q = 0.1, and −13.10 and −14.42 pp with q = 0.9. Neither
  reading reaches the anchor's gates.
- **The prediction.** PDR down with q = 0.9 was expected everywhere; it is
  shown only on the mesh. Why the effect is not resolved in the mobile cells is
  not measured here. One hypothesis, untested: data refreshes a flow's trail
  every second, so where routes break often, mobility removes trails before
  evaporation does; on the mesh, where trails would otherwise persist, a trail
  left a second without data can fall below the 0.01 threshold.

## The default path, at the anchor's scale

q01's anchor cells reproduce `results/cells/anchor-3/` exactly. The 40
`##RUN##` lines of each cell match, and so do all 360 marker lines of each
apart from `##PERF##`, `##PROV##`, `##ARGS##` and `##CONFIG##`. Since
anchor-3's commit (71d2166), #112 (its switch off by default) and #134 (the
attributes) changed nothing on the default path, over 20 seeds × 2 protocols
× 2 cells.

## What follows

- **Decision.** The maintainer decides on #140 whether the default moves. The
  recommendation there: keep 0.1.
- **Next A/Bs.** #125's A/Bs and the 2003 anchor use q = 0.1, as #125's
  recommendations planned once this sweep was read.
