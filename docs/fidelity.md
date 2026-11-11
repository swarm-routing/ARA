# ARA: fidelity sheet

What the paper specifies, what this implementation does, and where they differ.
Every row cites its source (`[KEY] §x`, `Table N`, `Fig. N`, `eq. N`, `p. N`),
or says `repo choice: <reason>`, or `unknown`. Checked by
`swarm-routing/.github` `tools/fidelity/fidelity.py check`; the section,
figure and equation locators are checked against the sources' extracted text by
`fidelity.py verify`, and the page locators of [R05] against the dissertation's
printed page numbers.

`core/` is being written in slices under [#5](https://github.com/swarm-routing/ARA/issues/5).
The first slice is route discovery: the FANT/BANT rows, evaporation, the
next-hop choice, the discovery retries and the ant wire header, with their
constants in `core/ara-config.h`. The second is route maintenance by data
(row "Route maintenance by data", with Δϕ). The ns-3 module `ara` (`ns3/`) carries these slices out on
swarm-core's `SwarmRoutingBase` with no constant of its own: a data packet with
no trail is dropped at once, by the adapter as by the core (row "Data packets
waiting for a route"). The other rows are not implemented yet; for
them the **Implementation** column records what the fresh implementation is to
do, and **Status** says how that plan stands against the paper. Section, equation and figure numbers are the
paper's; [G02]'s seven pages are the proceedings pages 79–85. Page numbers for
[R05] are the dissertation's printed page numbers (PDF page = printed + 13).

Where AntHocNet's `ara` arm set a value the paper does not give, the Notes say
`arm: <value>`. That is a record of what the old arm did, so the fresh `core/`
does not inherit it by accident; it is never a source for a paper value.

The maintainer's decision of 2026-11-10 on
[#4](https://github.com/swarm-routing/ARA/issues/4) made the arm's values the
repo choices for the protocol constants the paper does not give, for
continuity with v2.5.0's runs (Δϕ and q are to be swept later), with one
exception: the arm's hold queue for data during discovery is not adopted
(buffering is ARA2's, #15). Those rows say `repo choice: maintainer decision
2026-11-10, …` and keep the `arm:` note as the record of where the value came
from.

## Sources

| Key | Citation | Access |
|---|---|---|
| G02 | M. Güneş, U. Sorges, I. Bouazizi, *ARA – The Ant-Colony Based Routing Algorithm for MANETs*, Proc. International Conference on Parallel Processing Workshops (IWAHN 2002, Vancouver, 18–21 August 2002), pp. 79–85, 2002, DOI 10.1109/ICPPW.2002.1039715 (Crossref, Semantic Scholar and OpenAlex agree on title, authors, year, venue and pages; `lit_search.py confirm`, 2026-11-10) | in hand |
| R05 | M. H. Roth, *Termite: A Swarm Intelligent Routing Algorithm for Mobile Wireless Ad-Hoc Networks*, PhD dissertation, Cornell University, 2005; §3.5.2 restates ARA (pp. 59–60) and the 2003 "ARA2" (pp. 60–62). Secondary | in hand |
| AR12 | M. Arif, T. Rani, *ACO Based Routing for MANETs*, International Journal of Wireless & Mobile Networks 4(2):163–174, 2012, arXiv:1205.1604 (OpenAlex; arXiv has no Crossref record); §4.2 restates ARA, §4.3 its EARA variant. Secondary | in hand |
| GKB03 | M. Güneş, M. Kähmer, I. Bouazizi, *Ant-Routing-Algorithm (ARA) for Mobile Multi-Hop Ad-Hoc Networks – New Features and Results*, Second Mediterranean Workshop on Ad-Hoc Networks (Med-Hoc-Net 2003), Mahdia, pp. 9–20 (no DOI). The follow-up ("ARA2"); **not held** (no copy online; ask the authors). No row cites it directly | author |

Also read, and setting nothing: S. A. Thakare, S. A. Ladhake, *Performance
Comparison of ARA Protocol with DSR and AODV Protocol*, Software Engineering
3(1):17–20, 2012 (in the library) restates [G02] §3 without any constant.
A Crossref search for other ARA papers by Güneş (2026-11-10) found none
besides [G02]; [GKB03] is the only known follow-up. The traffic reference
[G02] cites, Broch et al., MobiCom 1998 (DOI 10.1145/288235.288256), is not in
the library (listed in its `MANUAL-DOWNLOADS.md`).

## Which version of ARA

Two versions exist:

- **ARA (2002)**, [G02] §3: the protocol this sheet describes, and the one
  whose ns-2 results ([G02] §4, Figs. 4–9) give the anchor.
- **ARA2 (2003)**, [GKB03]: probabilistic data forwarding, continuous
  pheromone decay, ant priority over data, backward-ant flooding, data
  buffering during discovery, neighbour eavesdropping. We do not hold the
  paper; what we know of it is Roth's summary, [R05] pp. 60–62, which says
  several of these are given without implementation detail.

Every row below is the **2002 version** unless it says otherwise. AntHocNet's
`ara` arm was written from [G02] but took several behaviours that [R05]
attributes to ARA2 (see *Differences from AntHocNet's `ara` arm*). Because the
anchor comes from [G02], the fresh `core/` implements the 2002 version and
treats any ARA2 behaviour as a stated deviation. Decided in #15 (2026-11-09),
with the four readings below.

The secondary sources do not agree with each other on two of those readings:
[AR12] §4.2 has data forwarded stochastically and §4.3 has the initial
pheromone computed from the hop count, where [R05] pp. 59–60 has deterministic
forwarding and a constant. [G02] does not settle the forwarding rule, but its
§3.1 does say the initial pheromone depends on the hop count. #15 chose
[R05]'s reading for both; the rows below mark the constant as a deviation
from [G02].

## Mechanisms

| Mechanism | Source | Implementation | Status |
|---|---|---|---|
| Three phases: route discovery, route maintenance, route failure handling; purely on demand | [G02] §3, §3.4 | One `core/` router with the three phases; no periodic control traffic | as written |
| Routing table entry is the triple (destination, next hop, pheromone ϕ) | [G02] §3.1 | Pheromone table keyed (destination, neighbour); `swarm-core` `PheromoneTable` if it fits | as written |
| Forward ant (FANT): broadcast by the sender, relayed by every node, unique sequence number | [G02] §3.1, Fig. 2 | FANT carries source, destination, sequence number, hop count; flooded | as written |
| FANT duplicates (same source and sequence number) are destroyed | [G02] §3.1, §3.4 | Bounded (source, seq) history; later copies dropped without touching the table | as written |
| A node seeing a FANT for the first time records (FANT source, previous hop, ϕ = f(hops)); one trail toward the source per discovery | [G02] §3.1 | Entry toward the source over the previous hop with ϕ raised to at least 1/hops (Parameters) | deviation: [G02] §3.1 says ϕ depends on the hop count but gives no formula; 1/hops is kept from the old arm for continuity with v2.5.0's runs (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); this replaces #15's constant, [R05] p. 59's reading). [AR12] §4.3 also reads it as hop-dependent |
| Destination destroys the FANT and answers with a backward ant (BANT) that lays the trail toward the destination | [G02] §3.1, Fig. 3 | BANT handled like the FANT, roles swapped | as written |
| How the BANT travels back (unicast along the FANT's path, or flooded) | [G02] §3.1, Fig. 3; [R05] p. 59; [AR12] §4.2 | BANT unicast back along the FANT's recorded path | deviation: [G02] does not say; decided in #15 (both secondaries' reading; a flooded BANT looped in the old arm). Cost: Fig. 3's second trail at node 3 is not reproduced |
| Data may be sent once the source receives the BANT | [G02] §3.1 | Data starts after the first BANT; data that arrives before it is dropped (row below) | as written (sending after the BANT); holding data meanwhile is not in [G02] |
| Data packets waiting for a route (buffering, limits, timeouts) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4): buffering is ARA2's ([R05] pp. 61–62), not adopted (#15) | No buffering in `core/` or in the adapter: a data packet with no route at its source starts a discovery (if none is running) and is dropped; a packet in transit that loses its route follows the backtracking rule below | not implemented: [G02] is silent; the old arm's hold queue (64 packets, 30 s) is not adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)); adding buffering later is a default-off switch with its own A/B |
| Route maintenance by data: the relaying node adds Δϕ to (D, next hop); the next hop adds Δϕ to (S, previous hop) | [G02] §3.2 | Both reinforcements on every data hop: the node that hands a data packet to its next hop (the source included) adds Δϕ to (D, next hop) when it hands it over, after evaporating to now; the node that receives it (the destination included) adds Δϕ to (S, previous hop). A deposit on an entry that does not exist creates it at Δϕ, within the pheromone table's cap | as written; two details [G02] leaves open are readings accepted by the maintainer on 2026-11-11 ([#5](https://github.com/swarm-routing/ARA/issues/5#issuecomment-6783812825)): a deposit on a missing entry creates it (the old arm's behaviour; §3.2 only says the node "increases the pheromone value of the entry"), and the sender deposits when it hands the packet over, not on an acknowledgement (§3.2 says "relays") |
| Evaporation by eq. 2, ϕ := (1 − q)·ϕ, applied regularly | [G02] §2.2, eq. 2, §3.2 | Eq. 2 at q per period (values below), evaluated continuously: on read, ϕ·(1 − q)^(Δt / period) through the injected clock port, as the old arm does | deviation: [G02] §3.2 has a "regular decreasing" and [R05] p. 60 reads 2002 ARA's decay as periodic, where [R05] p. 61 dates continuous decay to ARA2; kept from the old arm for continuity with v2.5.0's runs (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); this replaces #15's periodic reading) |
| A trail decays to zero when no ant or data visits it; the route is then gone | [G02] §3.4 | Entry dropped below an expiry threshold (value below) | as written |
| Multi-path: a node keeps several next hops per destination | [G02] §3.1, §3.4 | All positive-ϕ neighbours kept | as written |
| Next-hop choice for data among the trails | [G02] §2.2, §3.4; [R05] pp. 60–61; [AR12] §4.2 | Deterministic: the neighbour with the highest ϕ; ϕ/Σϕ only as a later default-off switch | as written per [R05] p. 60's reading of 2002 ARA; decided in #15. [G02] §2.2 gives ϕ/Σϕ for ants of the generic meta-heuristic only; [AR12] §4.2 reads data forwarding as stochastic |
| Link quality folded into the pheromone or its evaporation | [G02] §2.3, §3.4 | None | not implemented: [G02] names it as a possibility ("it is possible to integrate the connection/link quality") and as an example of what the route choice may depend on, without a rule |
| Loop prevention: duplicate data (source address, sequence number) is returned with the DUPLICATE_ERROR flag; the previous node deactivates that link | [G02] §3.2 | Data dedup on (source, seq); the flag rides in the adapter's header or a tag; receiver sets that link's ϕ to 0 | as written (the data packet's "sequence number" is a repo choice, below) |
| FANT/BANT loop freedom through their sequence numbers | [G02] §3.4 | Covered by the ant dedup row; BANT duplicates dropped like FANTs | as written; BANT duplicates decided in #15 with the unicast BANT path |
| "FANT forwarding" discovery variant: a relay with a route sends the FANT to that neighbour only | [AR12] §4.2 | None: every FANT is flooded | not implemented: [AR12] attributes it to another paper (its ref. [12]), not to [G02], whose §3.1 floods |
| Route failure detected by a missing acknowledgement | [G02] §3.3 | MAC retry-limit failure from the adapter, through an `ILinkState` port | as written; [G02] does not say which acknowledgement, and also mentions a ROUTE ERROR message for a link without defining it. [R05] p. 60 reads it as "perhaps due to a MAC level communications failure" |
| On a failed link: set its ϕ to 0, send the packet on another link if one exists | [G02] §3.3 | Same | as written |
| No other link: the node "informs its neighbors"; backtracking continues toward the source, which starts a new discovery | [G02] §3.3; [R05] p. 60 | Return the packet to the previous hop, which repeats the step, up to the source | as written per [R05]'s reading; decided in #15 (backtracking, not a broadcast) |
| Discovery retries when no BANT returns | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4): kept from the old arm for continuity with v2.5.0's runs | FANT re-sent after a timeout that doubles per retry, up to a retry count (values below), only while the source keeps sending: a retry is due only if a data packet for that destination arrived since the last FANT, otherwise the discovery ends (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/ARA/issues/5)) | as written: [G02] is silent (§3.3 only says the source "has to initiate a new route discovery phase" when backtracking reaches it). The old arm retried only while data was queued; with no hold queue (above), continued sending stands in for the queue |
| Control packets carry only a sequence number and the IP header's information | [G02] §3.5 | FANT/BANT as a POD, versioned header (org rule 3); a hop-count field is needed for f(hops) | as written (header layout: repo choice under org rule 3) |
| Ants share queues with data | [R05] p. 61 | No priority for ants | as written ([R05] states it for 2002 ARA; ant priority is an ARA2 change). [G02] does not say |
| Sleep period / sleep mode: a node whose pheromone is below a threshold sleeps and only processes packets addressed to it | [G02] §3.4 | None | not implemented: a power feature, outside the anchor's metrics |
| Per-node "statistic information block" | [G02] §3.4 | None | not implemented: mentioned once, never defined |

## Parameters

Protocol constants first, then the anchor scenario. [G02] gives no value for
any protocol constant; its §5 names "analysis of the maintenance of the
pheromone concentration" as future work. The constants are repo choices by the
maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4): the old arm's values, kept for continuity with v2.5.0's runs; Δϕ and q are to be swept later (a follow-up
of #5, not a ticket of its own). The scenario values [G02] does not give are
the ones v2.5.0's anchor ran with, re-used by the maintainer's decision on #4
(2026-11-09) and accepted again on 2026-11-10; see *Anchor candidate*.

| Parameter | Value | Source | Notes |
|---|---|---|---|
| Deposit Δϕ per data packet (and per ant, generic ACO) | 0.1 | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs, to be swept later | [G02] eq. 1 and §3.2 use Δϕ without a value; §2.2 calls it "a constant amount" for the generic meta-heuristic. arm: 0.1 |
| Initial FANT/BANT pheromone f(hops) | ϕ := max(ϕ, 1/hops) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | [G02] §3.1: "depending on the number of hops"; no formula, so 1/hops stays a deviation (Mechanisms). Replaces #15's constant ([R05] p. 59, "some constant amount of pheromone"). arm: 1/hops, raised to at least that value |
| Evaporation rate q, range | (0, 1] | [G02] eq. 2 | Range only |
| Evaporation rate q, value | 0.1 per period | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs, to be swept later | arm: 0.1 per period |
| Evaporation period | 1 s, evaluated continuously | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | [G02] §3.2: "regular decreasing". [R05] p. 61 says the pre-ARA2 schemes decayed on a fixed period "such as one second", an example, not ARA's value; continuous evaluation is a deviation (Mechanisms). arm: 1 s, evaluated continuously |
| Route-expiry threshold | ϕ < 0.01 | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | [G02] §3.4 only says ϕ decreases "to zero". arm: ϕ < 0.01 |
| Sleep threshold | n/a | repo choice: not needed, sleep mode is not implemented (Mechanisms); accepted by the maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) | [G02] §3.4 gives no value |
| FANT maximum hops / TTL | 35 | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | [AR12] §4.2 mentions a maximum hop count as a way to limit flooding, without a value. arm: 35 (ns-3 AODV's NetDiameter); the BANT carries the same TTL |
| FANT relay jitter | U[0, 10 ms) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | [G02] mentions none. arm: U[0, 10 ms) (ns-3 AODV's broadcast jitter), drawn from the injected RNG port (org rule 5) |
| Discovery retry timeout and count | 2.8 s, doubling per retry; 2 retries | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | arm: 2.8 s, doubling, 2 retries (ns-3 AODV's NetTraversalTime, RreqRetries) |
| Data held during discovery (packets, time) | 0 (none: dropped) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4): buffering is ARA2's ([R05] pp. 61–62), not adopted (#15) | Not the arm's value: arm: 64 packets, 30 s (ns-3 AODV's queue; the harness's shared plumbing), not adopted |
| Ant and data dedup history size | at most 64 entries, oldest evicted first; entries also expire 30 s after they are recorded, purged every 1 s | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) (lifetime, kept from the old arm for continuity with v2.5.0's runs) and maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/ARA/issues/5) (count cap, org rule 7) | Org rule 7 requires a configured cap, which a lifetime alone is not. The same cap bounds the BANT's reverse pointers, recorded with each new FANT. `core/` has the count cap; the lifetime waits for swarm-core's `DedupHistory` to take one (swarm-routing/ns3-swarm-core#33). arm: entries expire after 30 s, without a count cap |
| Pheromone table capacity | 4096 (destination, neighbour) entries; a new entry beyond it is not stored | repo choice: org rule 7 needs a cap; sized never to bind in the anchor (50 nodes) | [G02] gives none |
| Concurrent discoveries per node | 64 destinations; a discovery beyond it is not started (the packet is dropped as with no route) | repo choice: org rule 7 needs a cap; above the anchor's 10 flows | [G02] gives none |
| Data packet "sequence number" used for duplicate detection | key (source, destination, IP protocol, IP identification) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) (the arm's key) and maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/ARA/issues/5) (the IP protocol added) | [G02] §3.2 keys on source address and sequence number; IP carries no sequence number, so the IP identification stands in. IP numbers the identification per (source, destination, protocol), so without the protocol the key could collide across protocols and wrongly flag DUPLICATE_ERROR. Known limitation: the fragments of one IP packet share its identification, so a relay that forwards them would take every fragment after the first for a duplicate; the anchor's 64 B packets are never fragmented, and fragment-aware handling (e.g. the fragment offset in the key) is to be decided in #5 before any traffic that fragments. arm: (source, destination, IP identification), no protocol, with the same limitation |
| Simulator | ns-2 | [G02] §4.1 | ns-2 implementation by the authors |
| Nodes | 50 | [G02] §4.1 | |
| Area | 1500 m × 300 m | [G02] §4.1 | |
| MAC | IEEE 802.11 | [G02] §4.1 | Rate, radio range and propagation not given |
| 802.11 data rate | 2 Mbit/s | repo choice: v2.5.0's anchor (`--scenario=paper`, the harness's default `constant2`), re-used by the 2026-11-09 decision on #4, accepted by the maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) | [G02] §4.1 does not give it |
| Radio range and propagation | 300 m, disk (range) model | repo choice: v2.5.0's anchor (`--scenario=paper`), re-used by the 2026-11-09 decision on #4, accepted by the maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) | [G02] §4.1 does not give it. The harness calls the preset the Broch et al. 1998 field; not checked against that paper (not held) |
| Mobility | random waypoint, maximum speed 10 m/s | [G02] §4.1 | |
| Minimum speed | 1 m/s | repo choice: v2.5.0's anchor (`--scenario=paper` default), re-used by the 2026-11-09 decision on #4, accepted by the maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) | [G02] §4.1 gives the maximum only |
| Pause times | 0, 30, 60, 120, 300, 600, 900 s | [G02] §4.1 | 0 s moves constantly; 900 s is static. v2.5.0's anchor ran 0, 30, 120, 600 |
| Simulated time | 900 s | [G02] §4.1 | |
| Traffic | 10 CBR connections | [G02] §4.2 | "parameters similar to those in [2]" (Broch et al., MobiCom 1998) |
| CBR packet size and rate | 64 bytes, 1 packet/s (512 bit/s) per flow, sources started in [0, 180] s | repo choice: v2.5.0's anchor (`--scenario=paper`), re-used by the 2026-11-09 decision on #4, accepted by the maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) | [G02] §4.2 only says "similar to" Broch et al. 1998, which is not in the library; these values are not claimed as Broch's or as [G02]'s |
| Runs per point | 10 | [G02] §4.2, Fig. 5 | The anchor run uses ≥ 20 paired seeds (org rule 6; maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)) |
| Confidence interval | 95 % (α = 0.05) | [G02] Fig. 5, Fig. 7, Fig. 9 | |
| Overhead metrics | routing bits / data bits; routing packets | [G02] Fig. 6, Fig. 8 | |

## Anchor candidate

| Trend | Scenario | Metric | Comparators | Source |
|---|---|---|---|---|
| At high mobility (pause 0 and 30 s), ARA delivers more than AODV and DSDV (ARA above 95 %, AODV and DSDV not) and has the lowest routing overhead in bits; at low mobility (pause ≥ 300 s) ARA and AODV are close, AODV slightly lower in overhead | 50 nodes, 1500 m × 300 m, 802.11 at 2 Mbit/s, 300 m disk range, random waypoint 1–10 m/s, pauses 0, 30, 120, 600 s (four of the paper's seven, as v2.5.0), 900 s, ≥ 20 paired seeds, 10 CBR flows of 64 B at 1 packet/s (v2.5.0's `--scenario=paper` command lines) | v2.5.0's rule, re-used: paired by seed, ARA minus AODV, PDR with 95 % CI above zero at pause 0 and 30; NRL with CI below zero at pause 0. The 120 and 600 s cells reported, not gated | AODV (stock ns-3 arm). DSDV is in the paper but was not in v2.5.0's rule; DSR is in the paper and leads ARA in PDR at high mobility, but the org has no DSR arm | [G02] §4.2, Fig. 4, Fig. 6, Fig. 8 |

**Decided (#4, 2026-11-09): re-use v2.5.0's anchor.** The rule is AntHocNet
`docs/benchmarks/v2.5.0-comparators.md`'s, where the old arm passed it (pause 0:
dPDR +9.15 [+8.44, +9.86], dNRL −49.5 [−50.8, −48.3]). The fresh `core/`
reproducing that verdict is the proof that swarm-core's API is enough (the
Phase 3 exit). Those numbers are the old arm's; the new `core/` has to earn its
own, so #5 runs the rule again.

**Decided (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)): v2.5.0's scenario and rule, run again on the new `core/`
with ≥ 20 paired seeds.** The 2026-11-09 decisions on #4 (re-use v2.5.0's
anchor, 10 seeds) and on #15 (re-register it, ≥ 20 seeds) are reconciled this
way: org rule 6's ≥ 20 paired seeds overrides the re-use of v2.5.0's 10, and the
scenario and pass rule stay v2.5.0's. #5 pre-registers the run (plan and pass
rule committed) before dispatch.

Reproducibility with the org's arms: AODV is a stock ns-3 arm in `ns3-tools`'
`routing-bench`. Its check currently runs on `ns3-dev:3.48` because ns-3.49's
AODV livelocks (ns3-containers#35), so the anchor run pins 3.48 until that is
fixed (v2.5.0 ran on 3.42). NRL in the harness counts routing packets per
delivered data packet at the IP layer: a ratio, where [G02] Fig. 8 plots the
total number of routing packets. The NRL gate alone therefore does not
establish Fig. 8's ordering; the anchor reports the raw routing-packet counts
next to it. The harness's `nrl_bytes` (control bytes per delivered data byte),
the nearest to Fig. 6's bits per data bit, is reported, not gated. Fig. 8 has
DSR, not ARA, lowest at high mobility, and AODV well above ARA.

## Differences from AntHocNet's `ara` arm

From AntHocNet `ns3/baselines/adaptive/README.md` ("ARA — fidelity sheet",
"Shared plumbing") and `model/ara-routing-protocol.{h,cc}` (attributes and
defaults). None of these is to be inherited without its own row above.

| Paper | Arm | Difference |
|---|---|---|
| FANT pheromone f(hops), no formula ([G02] §3.1) | ϕ raised to at least 1/hops | Adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)), replacing #15's constant; still a deviation from [G02], which gives no formula |
| BANT path not stated; Fig. 3 has it lay two trails at one node ([G02] §3.1) | BANT unicast back along the FANT's path, one trail per node | Same as #15's decision; loses the multi-trail reading of Fig. 3. The arm's README records that a flooded BANT looped |
| Data next-hop rule not stated in §3; original ARA deterministic per [R05] p. 60 | Proportional draw, never straight back to the previous hop while another trail exists | Proportional forwarding is ARA2's rule ([R05] p. 61): a version mix, not adopted (#15). The no-return rule is an arm deviation |
| Evaporation eq. 2 applied periodically; q and period not given ([G02] §3.2) | Same law, evaluated continuously (lazily, on read), q = 0.1 per 1 s | Adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)), continuous evaluation included, though [R05] p. 61 dates it to ARA2; q and the period to be swept later |
| Δϕ not given | 0.1 | Adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)); to be swept later |
| Route expiry not given | ϕ < 0.01 | Adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)) |
| FANT hop limit not given | TTL 35 (ns-3 AODV's NetDiameter) | Adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)) |
| Data held during discovery not described | 64 packets, 30 s (ns-3 AODV's queue) | Not adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)): buffering is an ARA2 addition ([R05] pp. 61–62, #15); the new adapter drops |
| Discovery retries not given | FANT re-sent after 2.8 s, doubling, 2 retries (ns-3 AODV) | Adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)), so the reactive arms wait alike |
| FANT relaying: no jitter mentioned | Relays jittered by U[0, 10 ms) | Adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)) |
| Dedup history: bounded only by org rule 7 | Ant and data entries expire after 30 s | Lifetime adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)); a 64-entry count cap added, oldest evicted first (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/ARA/issues/5)) |
| Duplicate data keyed on source address and sequence number ([G02] §3.2) | Key (source, destination, IP identification); flag carried as a `BacktrackTag` | Key adopted with the IP protocol added (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/ARA/issues/5)); the flag's carrier is #5's |
| Failed link: set that link's ϕ to 0 ([G02] §3.3) | A MAC failure, or a next hop that never answers ARP, erases the neighbour for every destination | Wider than "this link" for one destination; the ARP case is the arm's addition |
| No other link: "informs its neighbors" ([G02] §3.3) | Packet returned to the previous hop (backtracking) | Same as #15's decision |
| Sleep mode ([G02] §3.4) | Not implemented | Same choice here |
