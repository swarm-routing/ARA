# ARA: fidelity sheet

What the paper specifies, what this implementation does, and where they differ.
Every row cites its source (`[KEY] §x`, `Table N`, `Fig. N`, `eq. N`, `p. N`),
or says `repo choice: <reason>`, or `unknown`. Checked by
`swarm-routing/.github` `tools/fidelity/fidelity.py check`; the section,
figure and equation locators are checked against the sources' extracted text by
`fidelity.py verify`, and the page locators of [R05] against the dissertation's
printed page numbers.

`core/` was written in three slices under [#5](https://github.com/swarm-routing/ARA/issues/5).
The first slice is route discovery: the FANT/BANT rows, evaporation, the
next-hop choice, the discovery retries and the ant wire header, with their
constants in `core/ara-config.h`. The second is route maintenance by data
(row "Route maintenance by data", with Δϕ). The third is loop prevention and
route failure (rows "Loop prevention", "On a failed link", "No other link"). The ns-3 module `ara` (`ns3/`) carries these slices out on
swarm-core's `SwarmRoutingBase` with no constant of its own: a data packet with
no trail is dropped at once, by the adapter as by the core (row "Data packets
waiting for a route"). The anchor missed; its written infeasibility verdict is
[`benchmarks/v0.2.0-anchor.md`](benchmarks/v0.2.0-anchor.md) ("Anchor candidate").
For a row that is not implemented, the **Implementation** column records what
the fresh implementation is to do, and **Status** says how that plan stands
against the paper. Section, equation and figure numbers are the
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
from. After the first anchor run missed its PDR gates, the maintainer decision 2026-11-13 on [#5](https://github.com/swarm-routing/ARA/issues/5#issuecomment-6844076630)
added the hold queue back as a default-off switch, `bufferDuringDiscovery`,
for an A/B and a second anchor run; the default is unchanged. Neither run passed
the PDR gates, and the switch stays off by default (maintainer decision
2026-11-13, [`benchmarks/v0.2.0-anchor.md`](benchmarks/v0.2.0-anchor.md)).

## Sources

<!-- fidelity:sources:begin -->
| Key | Citation | Link |
|---|---|---|
| AR12 | Arif & Rani, *ACO Based Routing for MANETs*, International Journal of Wireless & Mobile Networks 4(2), 2012, pp. 163–174 | <https://doi.org/10.5121/ijwmn.2012.4211> |
| BMJ98 | Broch et al., *A performance comparison of multi-hop wireless ad hoc network routing protocols*, Proceedings of the 4th annual ACM/IEEE international conference on Mobile computing and networking, 1998, pp. 85–97 | <https://doi.org/10.1145/288235.288256> |
| G02 | Gunes, Sorges & Bouazizi, *ARA-the ant-colony based routing algorithm for MANETs*, Proceedings. International Conference on Parallel Processing Workshop, 2002, pp. 79–85 | <https://doi.org/10.1109/icppw.2002.1039715> |
| GS03 | Günes & Spaniol, *Ant-Routing-Algorithm for Mobile Multi-Hop Ad-Hoc Networks*, Network Control and Engineering for QoS, Security and Mobility II, 2003, pp. 120–138 | <https://doi.org/10.1007/978-0-387-35703-4_9> |
| R05 | Roth, *Termite: A Swarm Intelligent Routing Algorithm For Mobile Wireless Ad-Hoc Networks*, PhD thesis, Cornell University, 2005 | <https://hdl.handle.net/1813/240> |
| SwarmCore | Joppi, *ns3-swarm-core*, Zenodo, 2026 | <https://doi.org/10.5281/zenodo.24138628> |
<!-- fidelity:sources:end -->

The table is generated from [`docs/references.bib`](references.bib) by
`fidelity.py bib sources`: change an entry there, never here. [G02] is the
paper this sheet follows. [R05] §3.5.2 restates ARA (pp. 59–60) and the 2003
"ARA2" (pp. 60–62); [AR12] §4.2 restates ARA and §4.3 its EARA variant; both
are secondary. [GS03] is the authors' 2003 revision: it calls the 2002
version "ANT", lists what the 2003 version changed (§4.5) and compares the two
(§5.2), so where [G02] is silent it is the authors' own account of what the
2002 version did (#27). [BMJ98] is the traffic reference [G02] §4.2 and [GS03]
§5.3 cite.
`swarm-core` is the org release `core/` and `ns3/` build on.

The follow-up paper ("ARA2"), M. Güneş, M. Kähmer, I. Bouazizi,
*Ant-Routing-Algorithm (ARA) for Mobile Multi-Hop Ad-Hoc Networks – New
Features and Results*, Second Mediterranean Workshop on Ad-Hoc Networks
(Med-Hoc-Net 2003), Mahdia, pp. 9–20, has no DOI and no registry or
publisher record that confirms it (`lit_search.py confirm --title`,
2026-11-12), so it is not in the bibliography; it was not read, and no row
cites it. It is still not held (#27, 2026-11-13: its access is the authors);
[GS03], by two of the same authors, lists the six 2003 features [R05]
attributes to it.

Also read, and setting nothing: S. A. Thakare, S. A. Ladhake, *Performance
Comparison of ARA Protocol with DSR and AODV Protocol*, Software Engineering
3(1):17–20, 2012, restates [G02] §3 without any constant (no DOI found in
Crossref, so not in the bibliography). A Crossref search for other ARA papers
by Güneş (2026-11-10) found none besides [G02]; the Med-Hoc-Net 2003 paper
above is the only known follow-up besides [GS03]. [BMJ98] was read for #27
(2026-11-13); the scenario rows in *Parameters* say where it agrees.

## Which version of ARA

Two versions exist:

- **ARA (2002)**, [G02] §3: the protocol this sheet describes, and the one
  whose ns-2 results ([G02] §4, Figs. 4–9) give the anchor.
- **ARA (2003)**, the authors' revision, [GS03] §4.5 and §5.2: statistical
  path selection, continual pheromone decrease, prioritized FANTs and BANTs,
  BANTs flooded like FANTs, a send buffer with minimal FANTs, and a MAC-Tap
  that reads packets from the neighbourhood. [R05] pp. 60–62 summarises the
  same six for the Med-Hoc-Net 2003 paper ("ARA2", Sources; not held). In
  saying what each feature changed, [GS03] says what the 2002 version ("ANT")
  did where [G02] is silent; the rows below cite it (#27).

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
| How the BANT travels back (unicast along the FANT's path, or flooded) | [G02] §3.1, Fig. 3; [R05] p. 59; [AR12] §4.2; [GS03] §4.5 | BANT unicast back along the FANT's recorded path | as written: [G02] does not say, and [GS03] §4.5 does ("In ANT the BANT were sent by unicast from the target to the source node"); flooding is the 2003 change (#125). Decided in #15 on both secondaries' reading, which the authors' account confirms (#27). Cost: [G02] Fig. 3's second trail at node 3 is not reproduced |
| Data may be sent once the source receives the BANT | [G02] §3.1 | Data starts after the first BANT; data that arrives before it is dropped (row below) | as written (sending after the BANT); holding data meanwhile is not in [G02] |
| Data packets waiting for a route (buffering, limits, timeouts) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4): buffering is ARA2's ([R05] pp. 61–62), not adopted (#15); a default-off switch for the anchor's A/B, maintainer decision 2026-11-13 on [#5](https://github.com/swarm-routing/ARA/issues/5#issuecomment-6844076630); what the 2002 version did: [GS03] §4.5 | By default no buffering in `core/` or in the adapter: a data packet with no route at its source starts a discovery (if none is running) and is dropped; a packet in transit that loses its route follows the backtracking rule below. With `Config::bufferDuringDiscovery` (the ns-3 attribute `BufferDuringDiscovery`), a source holds its own data it has no trail for while its discovery for the destination runs (`onLocalData` answers Queue), in swarm-core's deferred queue; an ant or a data packet that lays a trail toward the destination releases it, and a discovery that ends with no trail drops it (DiscardPending). Data in transit is never held: with no trail it is dropped (`onTransitData`), even at a node that holds its own data for the same destination. A packet whose route fails is never held, with the switch or without: it takes another trail or goes back to its previous hop (rows below), and is dropped only when it has neither, which at its source starts a discovery (`onRouteFailure`) | deviation (#27): [G02] is silent, and [GS03] §4.5 says what the 2002 version did: "ARA used the first packet of a connection as the FANT without considering the packet length", and packets arriving during discovery created more FANTs. So in 2002 such a packet was flooded toward its destination, not dropped; data carried as the FANT is not implemented ([#132](https://github.com/swarm-routing/ARA/issues/132)). The default drops it; the switch holds it, which with the minimal FANT is the 2003 version's send buffer (#125). The switch is off by default; its limits are the old arm's (64 packets, 30 s); off, the output is byte-identical to the code without the switch |
| Route maintenance by data: the relaying node adds Δϕ to (D, next hop); the next hop adds Δϕ to (S, previous hop) | [G02] §3.2 | Both reinforcements on every data hop: the node that hands a data packet to its next hop (the source included) adds Δϕ to (D, next hop) when it hands it over, after evaporating to now; the node that receives it (the destination included) adds Δϕ to (S, previous hop). A deposit on an entry that does not exist creates it at Δϕ, within the pheromone table's cap | as written; two details [G02] leaves open are readings accepted by the maintainer on 2026-11-11 ([#5](https://github.com/swarm-routing/ARA/issues/5#issuecomment-6783812825)): a deposit on a missing entry creates it (the old arm's behaviour; §3.2 only says the node "increases the pheromone value of the entry"), and the sender deposits when it hands the packet over, not on an acknowledgement (§3.2 says "relays") |
| Evaporation by eq. 2, ϕ := (1 − q)·ϕ, applied regularly | [G02] §2.2, eq. 2, §3.2; [GS03] §4.2, §4.5 | Eq. 2 at q per period (values below), evaluated continuously: on read, ϕ·(1 − q)^(Δt / period) through the injected clock port, as the old arm does | deviation: [G02] §3.2 has a "regular decreasing", and the authors confirm it was periodic: [GS03] §4.5 says that in ANT "the decreasing of the pheromone values were simulated in intervals of a second", and makes continual decrease a 2003 change (#27). [R05] pp. 60–61 reads it the same way; kept from the old arm for continuity with v2.5.0's runs (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); this replaces #15's periodic reading) |
| A trail decays to zero when no ant or data visits it; the route is then gone | [G02] §3.4 | Entry dropped below an expiry threshold (value below) | as written |
| Multi-path: a node keeps several next hops per destination | [G02] §3.1, §3.4 | All positive-ϕ neighbours kept | as written |
| Next-hop choice for data among the trails | [G02] §2.2, §3.4; [R05] pp. 60–61; [AR12] §4.2; [GS03] §5.2 | Deterministic: the neighbour with the highest ϕ; ϕ/Σϕ only as a later default-off switch | as written per [R05] p. 60's reading of 2002 ARA; decided in #15. [GS03] §5.2 supports it: statistical path selection ("ARAstat") is the first of the 2003 extensions it compares with ANT (#27). [G02] §2.2 gives ϕ/Σϕ for ants of the generic meta-heuristic only; [AR12] §4.2 reads data forwarding as stochastic |
| Link quality folded into the pheromone or its evaporation | [G02] §2.3, §3.4 | None | not implemented: [G02] names it as a possibility ("it is possible to integrate the connection/link quality") and as an example of what the route choice may depend on, without a rule |
| Loop prevention: duplicate data (source address, sequence number) is returned with the DUPLICATE_ERROR flag; the previous node deactivates that link | [G02] §3.2 | Data dedup on (source, destination, IP protocol, IP identification), bounded by the history cap; a duplicate goes back to its sender flagged DUPLICATE_ERROR, which sets that link's ϕ to 0 and drops the copy | as written (the data packet's "sequence number" is a repo choice, below); the flag is a packet tag, `DataFlagTag`, with no bytes on the air (repo choice, maintainer decision 2026-11-13 on [#5](https://github.com/swarm-routing/ARA/issues/5)) |
| FANT/BANT loop freedom through their sequence numbers | [G02] §3.4 | Covered by the ant dedup row; BANT duplicates dropped like FANTs | as written; BANT duplicates decided in #15 with the unicast BANT path |
| "FANT forwarding" discovery variant: a relay with a route sends the FANT to that neighbour only | [AR12] §4.2 | None: every FANT is flooded | not implemented: [AR12] attributes it to another paper (its ref. [12]), not to [G02], whose §3.1 floods |
| Route failure detected by a missing acknowledgement | [G02] §3.3 | MAC retry-limit failure from the adapter, through an `ILinkState` port | as written; [G02] does not say which acknowledgement, and also mentions a ROUTE ERROR message for a link without defining it. [R05] p. 60 reads it as "perhaps due to a MAC level communications failure" |
| On a failed link: set its ϕ to 0, send the packet on another link if one exists | [G02] §3.3 | Same | as written. The 2003 version limits it, [GS03] §4.3: "only one additional try makes sense", then the source is informed; a 2003 change, for #125 |
| No other link: the node "informs its neighbors"; backtracking continues toward the source, which starts a new discovery | [G02] §3.3; [R05] p. 60 | Return the packet to the previous hop, which repeats the step, up to the source | as written per [R05]'s reading; decided in #15 (backtracking, not a broadcast) |
| Discovery retries when no BANT returns | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4): kept from the old arm for continuity with v2.5.0's runs | FANT re-sent after a timeout that doubles per retry, up to a retry count (values below), only while the source keeps sending: a retry is due only if a data packet for that destination arrived since the last FANT, otherwise the discovery ends (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/ARA/issues/5)) | as written: [G02] is silent (§3.3 only says the source "has to initiate a new route discovery phase" when backtracking reaches it). The old arm retried only while data was queued; with no hold queue (above), continued sending stands in for the queue |
| Control packets carry only a sequence number and the IP header's information | [G02] §3.5 | FANT/BANT as a POD, versioned header (org rule 3); a hop-count field is needed for f(hops) | as written (header layout: repo choice under org rule 3) |
| Ants share queues with data | [R05] p. 61; [GS03] §4.5 | No priority for ants | as written: [GS03] §4.5 says that in ANT "these packets were handled like ordinary data packets", and makes FANT and BANT priority a 2003 change (#125); [R05] p. 61 reads it the same way. [G02] does not say |
| Sleep period / sleep mode: a node whose pheromone is below a threshold sleeps and only processes packets addressed to it | [G02] §3.4 | None | not implemented: a power feature, outside the anchor's metrics |
| Per-node "statistic information block" | [G02] §3.4 | None | not implemented: mentioned once, never defined |

## Parameters

Protocol constants first, then the anchor scenario. [G02] gives no value for
any protocol constant; its §5 names "analysis of the maintenance of the
pheromone concentration" as future work. The constants are repo choices by the
maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4): the old arm's values, kept for continuity with v2.5.0's runs; Δϕ is the authors' own value ([GS03] §4.2), so
it is not swept; q's two readings in [GS03] §4.2 were swept for #140
([benchmarks/q-sweep.md](benchmarks/q-sweep.md)). Each constant is an attribute of
`ns3::ara::RoutingProtocol`, named in its row (#108): its default is the value
here, read from `core::Config`, and a script, `routing-compare` or a campaign
plan's arm arguments set it with `--ns3::ara::RoutingProtocol::<Name>=<value>`
or `Config::SetDefault`; times are in seconds, as in the core. The scenario values [G02] does not give are
the ones v2.5.0's anchor ran with, re-used by the maintainer's decision on #4
(2026-11-09) and accepted again on 2026-11-10; see *Anchor candidate*.

| Parameter | Value | Source | Notes |
|---|---|---|---|
| Deposit Δϕ per data packet (and per ant, generic ACO) | 0.1 | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs, to be swept later; the authors' value, [GS03] §4.2 | [GS03] §4.2, on the authors' own simulations: "In our simulations Δϕ has a value of 0.1" (#27). [G02] eq. 1 and §3.2 use Δϕ without a value; §2.2 calls it "a constant amount" for the generic meta-heuristic. arm: 0.1 ns-3 attribute: `Deposit` (#108) |
| Initial FANT/BANT pheromone f(hops) | ϕ := max(ϕ, 1/hops) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | [G02] §3.1: "depending on the number of hops"; no formula, so 1/hops stays a deviation (Mechanisms). Replaces #15's constant ([R05] p. 59, "some constant amount of pheromone"). arm: 1/hops, raised to at least that value |
| Evaporation rate q, range | (0, 1] | [G02] eq. 2 | Range only |
| Evaporation rate q, value | 0.1 per period | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs, to be swept later; [GS03] §4.2 is ambiguous | [GS03] §4.2 says the values are decreased every T = 1 s, multiplicatively "with a factor of (1 − q) = 0.1": read literally that is q = 0.9, nine times this q; read as q = 0.1 (factor 0.9) it is this value. The text does not settle which. Swept for #140 ([benchmarks/q-sweep.md](benchmarks/q-sweep.md), 2026-11-13): q = 0.9 against 0.1 is −1.94 pp PDR on the near-static mesh, and within noise or a little routing load elsewhere; neither reading changes the anchor's comparison with AODV, so the default stays 0.1 pending the maintainer's decision on #140. arm: 0.1 per period ns-3 attribute: `EvaporationRate` (#108) |
| Evaporation period | 1 s, evaluated continuously | [GS03] §4.2, §4.5 | [GS03] §4.2 gives T = 1 s, and §4.5 says ANT decreased the values in intervals of a second: the period is the authors' value for the 2002 version (#27). Evaluating it continuously is the 2003 change, here a repo choice (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4), kept from the old arm for continuity with v2.5.0's runs; Mechanisms). [G02] §3.2: "regular decreasing". [R05] p. 61's fixed period "such as one second" was read as an example; [GS03] shows it was ARA's own. arm: 1 s, evaluated continuously ns-3 attribute: `EvaporationPeriod` (seconds) (#108) |
| Route-expiry threshold | ϕ < 0.01 | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | [G02] §3.4 only says ϕ decreases "to zero". arm: ϕ < 0.01 ns-3 attribute: `ExpiryThreshold` (#108) |
| Sleep threshold | n/a | repo choice: not needed, sleep mode is not implemented (Mechanisms); accepted by the maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) | [G02] §3.4 gives no value |
| FANT maximum hops / TTL | 35 | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | [AR12] §4.2 mentions a maximum hop count as a way to limit flooding, without a value. arm: 35 (ns-3 AODV's NetDiameter); the BANT carries the same TTL ns-3 attribute: `AntTtl` (#108) |
| FANT relay jitter | U[0, 10 ms) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | [G02] mentions none. arm: U[0, 10 ms) (ns-3 AODV's broadcast jitter), drawn from the injected RNG port (org rule 5) ns-3 attribute: `RelayJitterMax` (seconds) (#108) |
| Discovery retry timeout and count | 2.8 s, doubling per retry; 2 retries | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4); kept from the old arm for continuity with v2.5.0's runs | arm: 2.8 s, doubling, 2 retries (ns-3 AODV's NetTraversalTime, RreqRetries) ns-3 attribute: `DiscoveryTimeout` (seconds), `MaxDiscoveryRetries` (#108) |
| Data held during discovery (packets, time) | 0 (none: dropped); with `bufferDuringDiscovery`, at most 64 packets for at most 30 s each (swarm-core `DeferredPacketQueue`'s `MaxPackets` and `MaxAge` defaults) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4): buffering is ARA2's ([R05] pp. 61–62), not adopted (#15); the switch: maintainer decision 2026-11-13 on [#5](https://github.com/swarm-routing/ARA/issues/5#issuecomment-6844076630) | The default is not the arm's value; the switch's limits are: arm: 64 packets, 30 s (ns-3 AODV's queue; the harness's shared plumbing) ns-3 attribute: `BufferDuringDiscovery`; the limits are swarm-core's `ns3::DeferredPacketQueue` `MaxPackets` and `MaxAge` (#108) |
| Ant and data dedup history size | at most 64 entries, oldest evicted first; entries also expire 30 s after they are recorded, the lifetime evaluated when a history is read or written (no purge timer) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) (lifetime, kept from the old arm for continuity with v2.5.0's runs), maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/ARA/issues/5) (count cap, org rule 7) and maintainer decision 2026-11-13, [#109](https://github.com/swarm-routing/ARA/issues/109) (expiry on access in place of the 1 s purge decided in #4) | Org rule 7 requires a configured cap, which a lifetime alone is not. The same cap bounds the BANT's reverse pointers, recorded with each new FANT. `core/` has both: `historyCapacity` and `historyLifetime`, on the FANT and BANT histories (swarm-core's `DedupHistory`, swarm-routing/ns3-swarm-core#33), the reverse pointers and the data history. The lifetime is evaluated when a history is read or written, against the clock, as evaporation is: there is no 1 s purge timer, so no entry is honoured past 30 s (a periodic purge honours one for up to 1 s longer), no timer event enters a run, and the count cap alone bounds memory. The value decided on 2026-11-10 read "purged every 1 s"; the maintainer accepted expiry on access in its place on 2026-11-13 ([#109](https://github.com/swarm-routing/ARA/issues/109)). arm: entries expire after 30 s, without a count cap ns-3 attribute: `HistoryCapacity`, `HistoryLifetime` (seconds) (#108) |
| Pheromone table capacity | 4096 (destination, neighbour) entries; a new entry beyond it is not stored | repo choice: org rule 7 needs a cap; sized never to bind in the anchor (50 nodes) | [G02] gives none ns-3 attribute: `PheromoneCapacity` (#108) |
| Concurrent discoveries per node | 64 destinations; a discovery beyond it is not started (the packet is dropped as with no route) | repo choice: org rule 7 needs a cap; above the anchor's 10 flows | [G02] gives none ns-3 attribute: `MaxDiscoveries` (#108) |
| Data packet "sequence number" used for duplicate detection | key (source, destination, IP protocol, IP identification) | repo choice: maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) (the arm's key) and maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/ARA/issues/5) (the IP protocol added) | [G02] §3.2 keys on source address and sequence number; IP carries no sequence number, so the IP identification stands in. IP numbers the identification per (source, destination, protocol), so without the protocol the key could collide across protocols and wrongly flag DUPLICATE_ERROR. Known limitation: the fragments of one IP packet share its identification, so a relay that forwards them would take every fragment after the first for a duplicate; the anchor's 64 B packets are never fragmented, and fragment-aware handling (e.g. the fragment offset in the key) is open in [#106](https://github.com/swarm-routing/ARA/issues/106) and is needed before any traffic that fragments. arm: (source, destination, IP identification), no protocol, with the same limitation |
| Simulator | ns-2 | [G02] §4.1 | ns-2 implementation by the authors |
| Nodes | 50 | [G02] §4.1 | |
| Area | 1500 m × 300 m | [G02] §4.1 | |
| MAC | IEEE 802.11 | [G02] §4.1 | Rate, radio range and propagation not given |
| 802.11 data rate | 2 Mbit/s | repo choice: v2.5.0's anchor (`--scenario=paper`, the harness's default `constant2`), re-used by the 2026-11-09 decision on #4, accepted by the maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) | [G02] §4.1 does not give it. [BMJ98] §4 models its radio on the Lucent WaveLAN and states no rate in its text (#27) |
| Radio range and propagation | 300 m, disk (range) model | repo choice: v2.5.0's anchor (`--scenario=paper`), re-used by the 2026-11-09 decision on #4, accepted by the maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) | [G02] §4.1 does not give it. [BMJ98], whose field the harness names the preset after, differs (#27): free-space propagation within a reference distance and two-ray ground reflection beyond it (§2.1), and a nominal transmission range of 250 m (§4.3). The 300 m disk is the harness's, not Broch's |
| Mobility | random waypoint, maximum speed 10 m/s | [G02] §4.1 | [GS03] §5.1 uses 10 m/s too; [BMJ98] §4.1 reports 20 m/s, and 1 m/s for comparison, not 10 (#27) |
| Minimum speed | 1 m/s | repo choice: v2.5.0's anchor (`--scenario=paper` default), re-used by the 2026-11-09 decision on #4, accepted by the maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) | [G02] §4.1 gives the maximum only. [GS03] §5.1 (the 2003 version's runs, in the same field) draws the speed "uniformly distributed between 0 and the maximal velocity", as [BMJ98] §4.1 does: a minimum of 0, where this is 1 m/s (#27) |
| Pause times | 0, 30, 60, 120, 300, 600, 900 s | [G02] §4.1 | 0 s moves constantly; 900 s is static. v2.5.0's anchor ran 0, 30, 120, 600. [BMJ98] §4.1 and [GS03] §5.1 use the same seven (#27) |
| Simulated time | 900 s | [G02] §4.1 | |
| Traffic | 10 CBR connections | [G02] §4.2 | "parameters similar to those in [2]" ([BMJ98]); [GS03] §5.3 runs 10 CBR connections too, "similar to those in [20]" ([BMJ98] again) |
| CBR packet size and rate | 64 bytes, 1 packet/s (512 bit/s) per flow, sources started in [0, 180] s | repo choice: v2.5.0's anchor (`--scenario=paper`), re-used by the 2026-11-09 decision on #4, accepted by the maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4) | [G02] §4.2 only says "similar to" [BMJ98]. [BMJ98] §4.2 (read for #27): 64-byte packets, and connections started uniformly between 0 and 180 s, as here, but 4 packets per second per source (with 10, 20 or 30 sources), where this sends 1. [GS03] Fig. 6 labels its traffic "CBR 4x512Byte". So the size and the start window are Broch's; the rate is not, and is not claimed as [G02]'s |
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

**Verdict (2026-11-13): the anchor misses.** The NRL gate passes. Both PDR
gates fail, with the default (anchor-3) and with `bufferDuringDiscovery` on
(anchor-buffer); the first pre-registration's outputs are byte-identical to
anchor-3's. The written infeasibility verdict, its numbers and its threats to
validity are [`benchmarks/v0.2.0-anchor.md`](benchmarks/v0.2.0-anchor.md)
(decided on [#5](https://github.com/swarm-routing/ARA/issues/5)). The next
anchor, on the 2003 version, is #125's.

**Checked against [BMJ98] (#27, 2026-11-13).** The field, node count,
simulated time, pause times, packet size and start window are Broch's. Four
scenario values are not (*Parameters*): the radio (a 300 m disk here; Broch
has 250 m nominal range, two-ray ground), the minimum speed (1 m/s here, 0 in
Broch and in [GS03] §5.1), the maximum speed (10 m/s here, as [G02] and
[GS03]; Broch reports 20 m/s and 1 m/s) and the rate (1 packet/s here, 4 in
Broch). The v0.2.0 verdict stands as pre-registered. A new anchor plan (#125)
says which values it uses.

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
| Data held during discovery not described | 64 packets, 30 s (ns-3 AODV's queue) | Not adopted as the default (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)): buffering is an ARA2 addition ([R05] pp. 61–62, #15); the new adapter drops. Available as the default-off switch `bufferDuringDiscovery`, with the arm's limits (maintainer decision 2026-11-13 on [#5](https://github.com/swarm-routing/ARA/issues/5#issuecomment-6844076630)) |
| Discovery retries not given | FANT re-sent after 2.8 s, doubling, 2 retries (ns-3 AODV) | Adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)), so the reactive arms wait alike |
| FANT relaying: no jitter mentioned | Relays jittered by U[0, 10 ms) | Adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)) |
| Dedup history: bounded only by org rule 7 | Ant and data entries expire after 30 s | Lifetime adopted (maintainer decision 2026-11-10, [#4](https://github.com/swarm-routing/ARA/issues/4)); a 64-entry count cap added, oldest evicted first (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/ARA/issues/5)) |
| Duplicate data keyed on source address and sequence number ([G02] §3.2) | Key (source, destination, IP identification); flag carried as a `BacktrackTag` | Key adopted with the IP protocol added (maintainer decision 2026-11-10, [#5](https://github.com/swarm-routing/ARA/issues/5)); the flag is a packet tag as in the arm (`DataFlagTag`, DUPLICATE_ERROR and Backtrack; maintainer decision 2026-11-13, [#5](https://github.com/swarm-routing/ARA/issues/5)): no bytes on the air, where a real one-bit flag would cost a header |
| Failed link: set that link's ϕ to 0 ([G02] §3.3) | A MAC failure, or a next hop that never answers ARP, erases the neighbour for every destination | Wider than "this link" for one destination; the ARP case is the arm's addition |
| No other link: "informs its neighbors" ([G02] §3.3) | Packet returned to the previous hop (backtracking) | Same as #15's decision |
| Sleep mode ([G02] §3.4) | Not implemented | Same choice here |
