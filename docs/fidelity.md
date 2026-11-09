# ARA: fidelity sheet

What the paper specifies, what this implementation does, and where they differ.
Every row cites its source (`[KEY] §x`, `Table N`, `Fig. N`, `eq. N`), or says
`repo choice: <reason>`, or `unknown`. Checked by
`swarm-routing/.github` `tools/fidelity/fidelity.py check`.

`core/` is not written yet (ARA#4 gates it). Until it is, the **Implementation**
column records what the fresh implementation is to do, and **Status** says how
that plan stands against the paper. Section, equation and figure numbers are the
paper's; page numbers for [G02] are the proceedings pages (79–85).

## Sources

| Key | Citation | Access |
|---|---|---|
| G02 | M. Güneş, U. Sorges, I. Bouazizi, *ARA – The Ant-Colony Based Routing Algorithm for MANETs*, Proc. International Conference on Parallel Processing Workshops (IWAHN 2002, Vancouver), pp. 79–85, 2002, DOI 10.1109/ICPPW.2002.1039715 (verified against Crossref) | in hand |
| R05 | M. H. Roth, *Termite: A Swarm Intelligent Routing Algorithm for Mobile Wireless Ad-Hoc Networks*, PhD dissertation, Cornell University, 2005; §3.5.2 restates ARA (pp. 59–61) and the 2003 "ARA2" (pp. 61–62). Secondary | in hand |
| AR12 | M. Arif, T. Rani, *ACO Based Routing for MANETs*, arXiv:1205.1604 (IJWMN), 2012; §4.2 restates ARA. Secondary | in hand |
| GKB03 | M. Güneş, M. Kähmer, I. Bouazizi, *Ant-Routing-Algorithm (ARA) for Mobile Multi-Hop Ad-Hoc Networks – New Features and Results*, Med-Hoc-Net 2003, pp. 9–20 (no DOI). The follow-up ("ARA2"); **not held** (not online; ask the authors). No row cites it directly | author |

## Which version of ARA

Two versions exist:

- **ARA (2002)**, [G02] §3: the protocol this sheet describes, and the one
  whose ns-2 results ([G02] §4, Figs. 4–9) give the anchor.
- **ARA2 (2003)**, [GKB03]: probabilistic data forwarding, continuous
  pheromone decay, ant priority over data, backward-ant flooding, data
  buffering during discovery, neighbour eavesdropping. We do not hold the
  paper; what we know of it is Roth's summary, [R05] pp. 61–62, which says
  several of these are given without implementation detail.

Every row below is the **2002 version** unless it says otherwise. AntHocNet's
`ara` arm was written from [G02] but took several behaviours that [R05]
attributes to ARA2 (see *Differences from AntHocNet's `ara` arm*). Because the
anchor comes from [G02], the fresh `core/` should implement the 2002 version and
treat any ARA2 behaviour as a stated deviation. The maintainer confirms this
choice, and the open readings below, in #15 before code.

## Mechanisms

| Mechanism | Source | Implementation | Status |
|---|---|---|---|
| Three phases: route discovery, route maintenance, route failure handling; purely on demand | [G02] §3, §3.4 | One `core/` router with the three phases; no periodic control traffic | as written |
| Routing table entry is the triple (destination, next hop, pheromone ϕ) | [G02] §3.1 | Pheromone table keyed (destination, neighbour); `swarm-core` `PheromoneTable` if it fits | as written |
| Forward ant (FANT): broadcast by the sender, relayed by every node, unique sequence number | [G02] §3.1, Fig. 2 | FANT carries source, destination, sequence number, hop count; flooded | as written |
| FANT duplicates (same source and sequence number) are destroyed | [G02] §3.1, §3.4 | Bounded (source, seq) history; later copies dropped without touching the table | as written |
| A node seeing a FANT for the first time records (FANT source, previous hop, ϕ = f(hops)); one trail toward the source per discovery | [G02] §3.1 | Entry toward the source over the previous hop; f is the open parameter below | unknown: f is not given. [R05] p. 60 instead reads the FANT as depositing a constant amount; a pre-registered reading is needed |
| Destination destroys the FANT and answers with a backward ant (BANT) that lays the trail toward the destination | [G02] §3.1, Fig. 3 | BANT handled like the FANT, roles swapped | as written |
| How the BANT travels back (unicast along the FANT's path, or flooded) | [G02] §3.1, Fig. 3; [R05] p. 60; [AR12] §4.2 | To be pre-registered (#15) | unknown: [G02] does not say. Its Fig. 3 text has the BANT create **two** trails at node 3 (via 4 and via 6), which a single unicast retrace cannot do; both secondaries read it as returned along the forward path |
| Data may be sent once the source receives the BANT | [G02] §3.1 | Data starts after the first BANT; what happens to data that arrives before it is the open buffering row below | as written (sending after the BANT); holding data meanwhile is not in [G02] |
| Data packets waiting for a route (buffering, limits, timeouts) | unknown | To be chosen | unknown: [G02] is silent; [R05] p. 62 lists data buffering as an ARA2 addition |
| Route maintenance by data: the relaying node adds Δϕ to (D, next hop); the next hop adds Δϕ to (S, previous hop) | [G02] §3.2 | Both reinforcements on every data hop | as written |
| Evaporation by eq. 2, ϕ := (1 − q)·ϕ, applied regularly | [G02] §2.2, eq. 2, §3.2 | Periodic decay with an injected timer port | as written (q and period open, below) |
| A trail decays to zero when no ant or data visits it; the route is then gone | [G02] §3.4 | Entry dropped below an expiry threshold | as written (threshold open, below) |
| Multi-path: a node keeps several next hops per destination | [G02] §3.1, §3.4 | All positive-ϕ neighbours kept | as written |
| Next-hop choice for data among the trails | [G02] §2.2, §3.4; [R05] pp. 60–61 | To be pre-registered (#15) | unknown: §3 never states the data rule. §2.2 gives p = ϕ/Σϕ for ants in the generic ACO; §3.4 says the choice "depends on the environment". [R05] p. 60 says original ARA forwarded deterministically on the highest ϕ, and p. 61 that probabilistic forwarding arrived with ARA2 |
| Loop prevention: duplicate data (source address, sequence number) is returned with the DUPLICATE_ERROR flag; the previous node deactivates that link | [G02] §3.2 | Data dedup on (source, seq); the flag rides in the adapter's header or a tag; receiver sets that link's ϕ to 0 | as written (which "sequence number" a data packet carries is a repo choice, below) |
| FANT/BANT loop freedom through their sequence numbers | [G02] §3.4 | Covered by the ant dedup row | as written for FANTs; unknown: whether a second BANT copy may still update the table (Fig. 3's two trails at node 3 need it) is open with the BANT path (#15) |
| Route failure detected by a missing acknowledgement | [G02] §3.3 | MAC retry-limit failure from the adapter, through an `ILinkState` port | as written; [G02] does not say which acknowledgement, and also mentions a ROUTE ERROR message for a link without defining it |
| On a failed link: set its ϕ to 0, send the packet on another link if one exists | [G02] §3.3 | Same | as written |
| No other link: the node "informs its neighbors"; backtracking continues toward the source, which starts a new discovery | [G02] §3.3; [R05] p. 60 | Return the packet to the previous hop, which repeats the step | unknown: "informs its neighbors" can mean a broadcast or a return to the previous hop; [R05] reads the latter |
| Discovery retries when no BANT returns | unknown | To be chosen | unknown: [G02] is silent |
| Control packets carry only a sequence number and the IP header's information | [G02] §3.5 | FANT/BANT as a POD, versioned header (org rule 3); a hop-count field is needed for f(hops) | as written (header layout: repo choice under org rule 3) |
| Ants share queues with data | [R05] p. 61 | No priority for ants | as written ([R05] states it for 2002 ARA; ant priority is an ARA2 change) |
| Sleep period / sleep mode: a node whose pheromone is below a threshold sleeps and only processes packets addressed to it | [G02] §3.4 | None | not implemented: a power feature, outside the anchor's metrics |
| Per-node "statistic information block" | [G02] §3.4 | None | not implemented: mentioned once, never defined |

## Parameters

| Parameter | Value | Source | Notes |
|---|---|---|---|
| Deposit Δϕ per data packet (and per ant, generic ACO) | — | unknown | [G02] eq. 1 and §3.2 use Δϕ without a value |
| Initial FANT/BANT pheromone f(hops) | — | unknown | [G02] §3.1: "depending on the number of hops"; no formula. [R05] p. 60 reads a constant |
| Evaporation rate q, range | (0, 1] | [G02] eq. 2 | Range only |
| Evaporation rate q, value | — | unknown | |
| Evaporation period | — | unknown | [G02] §3.2: "regular decreasing". [R05] p. 61 says the pre-ARA2 schemes decayed on a fixed period "such as one second", not as ARA's value |
| Route-expiry threshold | — | unknown | [G02] §3.4 only says ϕ decreases "to zero" |
| Sleep threshold | — | unknown | [G02] §3.4; sleep mode not implemented |
| FANT maximum hops / TTL | — | unknown | [AR12] §4.2 mentions a maximum hop count as an option, without a value |
| Discovery retry timeout and count | — | unknown | |
| Data held during discovery (packets, time) | — | unknown | |
| Ant and data dedup history size | — | unknown | Must be bounded (org rule 7) |
| Data packet "sequence number" used for duplicate detection | — | unknown | IP carries none; candidates are the IP identification or a sequence number in our header |
| Simulator | ns-2 | [G02] §4.1 | ns-2 implementation by the authors |
| Nodes | 50 | [G02] §4.1 | |
| Area | 1500 m × 300 m | [G02] §4.1 | |
| MAC | IEEE 802.11 | [G02] §4.1 | Rate, radio range and propagation not given |
| Radio range | — | unknown | Not stated |
| Mobility | random waypoint, maximum speed 10 m/s | [G02] §4.1 | Minimum speed not given |
| Pause times | 0, 30, 60, 120, 300, 600, 900 s | [G02] §4.1 | 0 s moves constantly; 900 s is static |
| Simulated time | 900 s | [G02] §4.1 | |
| Traffic | 10 CBR connections | [G02] §4.2 | "parameters similar to those in [2]" (Broch et al., MobiCom 1998) |
| CBR packet size and rate | — | unknown | [G02] §4.2 only says "similar to" Broch et al. 1998. Read that paper rather than recalling it; values taken from it become `repo choice: Broch et al. 1998's values, which [G02] calls similar`, never [G02]'s |
| Runs per point | 10 | [G02] §4.2, Fig. 5 | |
| Confidence interval | 95 % (α = 0.05) | [G02] Fig. 5, Fig. 7, Fig. 9 | |
| Overhead metrics | routing bits / data bits; routing packets | [G02] Fig. 6, Fig. 8 | |

## Anchor candidate

| Trend | Scenario | Metric | Comparators | Source |
|---|---|---|---|---|
| At high mobility (pause 0 and 30 s), ARA delivers more than AODV and DSDV (ARA above 95 %, AODV and DSDV not) and has the lowest routing overhead in bits; at low mobility (pause ≥ 300 s) ARA and AODV are close, AODV slightly lower in overhead | 50 nodes, 1500 m × 300 m, 802.11, random waypoint up to 10 m/s, pauses 0–900 s (seven values), 900 s, 10 CBR connections (rate and size from Broch et al. 1998) | Paired by seed, ARA minus AODV: PDR with 95 % CI above zero at pause 0 and 30; routing bits per data bit (and routing packets) with CI below zero at pause 0. Other pauses reported, not gated | AODV and DSDV (stock ns-3 arms); DSR is in the paper and leads ARA in PDR at high mobility, but the org has no DSR arm | [G02] §4.2, Fig. 4, Fig. 6, Fig. 8 |

Reproducibility with the org's arms: AODV and DSDV are stock ns-3 arms in
`ns3-tools`' `routing-bench`. The AODV check currently runs on `ns3-dev:3.48`
because ns-3.49's AODV livelocks (ns3-containers#35), so the anchor run pins
3.48 until that is fixed. DSR has no org arm; it stays out of the gated rule.
The paper used 10 runs per point; the org rule asks for ≥ 20 paired seeds.

## Differences from AntHocNet's `ara` arm

From AntHocNet `ns3/baselines/adaptive/README.md` ("ARA — fidelity sheet",
"Shared plumbing"). None of these is to be inherited without its own row above.

| Paper | Arm | Difference |
|---|---|---|
| FANT pheromone f(hops), no formula ([G02] §3.1) | ϕ raised to at least 1/hops | The arm's choice; if kept it is a repo choice, not the paper's |
| BANT path not stated; Fig. 3 has it lay two trails at one node ([G02] §3.1) | BANT unicast back along the FANT's path, one trail per node | Loses the multi-trail reading of Fig. 3; the arm's README records that a flooded BANT looped. Pre-register before code |
| Data next-hop rule not stated in §3; original ARA deterministic per [R05] p. 60 | Proportional draw, never straight back to the previous hop while another trail exists | Proportional forwarding is ARA2's rule ([R05] p. 61): a version mix. The no-return rule is an arm deviation |
| Evaporation eq. 2 applied periodically; q and period not given ([G02] §3.2) | Same law, evaluated continuously (lazily, on read), q = 0.1 per 1 s | Continuous decay is an ARA2 feature ([R05] p. 61); q and the 1 s period are the arm's numbers |
| Δϕ not given | 0.1 | The arm's number |
| Route expiry not given | ϕ < 0.01 | The arm's number |
| Data held during discovery not described | 64 packets, 30 s (ns-3 AODV's queue) | Buffering is listed as an ARA2 addition ([R05] p. 62); here a harness-wide choice |
| Discovery retries not given | FANT re-sent after 2.8 s, doubling, 2 retries (ns-3 AODV) | Repo choice so the reactive arms wait alike |
| FANT relaying: no jitter mentioned | Relays jittered by U[0, 10 ms) | Implementation detail (AODV's) |
| Duplicate data keyed on source address and sequence number ([G02] §3.2) | Key (source, destination, IP identification); flag carried as a `BacktrackTag` | Equivalent intent; the key and the flag's carrier are the arm's choices |
| Failed link: set that link's ϕ to 0 ([G02] §3.3) | A MAC failure, or a next hop that never answers ARP, erases the neighbour for every destination | Wider than "this link" for one destination; the ARP case is the arm's addition |
| No other link: "informs its neighbors" ([G02] §3.3) | Packet returned to the previous hop (backtracking) | One of two readings |
| Sleep mode ([G02] §3.4) | Not implemented | Same choice here |
