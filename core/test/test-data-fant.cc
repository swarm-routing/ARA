// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Data as the FANT (#132, the reading accepted 2026-11-24, amended
// 2026-11-25): with Config::dataAsFant on, a source's packet with no trail
// goes out as a FANT carrying it, as the 2002 version did ([GS03] §4.5): one
// FANT per packet, a new sequence number each, no retry FANT of its own.
#include "ara-core-tests.h"

#include <cstdint>

namespace ara::core::test {

namespace {

using swarm::core::RouteAction;

Config dataAsFant() {
    Config c;
    c.dataAsFant = true;
    return c;
}

}  // namespace

void TestDataAsFantAtTheSource(Checker& c) {
    Harness h(1, dataAsFant());
    const AntDecision first = h.router.onLocalData(9);
    c.check(first.action == RouteAction::Broadcast && first.message &&
                *first.message == AntMessage{AntType::Forward, 1, 9, 1, 0, 35},
            "no trail: Broadcast, the packet travels in a FANT (seq 1, hops 0, TTL 35)");
    c.check(h.out.empty(), "no separate FANT through the sink");
    c.check(h.router.discovering(9), "the discovery starts");
    const AntDecision second = h.router.onLocalData(9);
    c.check(second.action == RouteAction::Broadcast && second.message &&
                second.message->sequence == 2,
            "a packet during the discovery is a FANT too, with a new sequence number");

    // The source drops the echo of its own FANT.
    const int draws = h.rng.draws();
    h.router.onAnt(AntMessage{AntType::Forward, 1, 9, 1, 1, 34}, 2);
    c.check(h.rng.draws() == draws && h.out.empty(), "its own FANT's echo is dropped");

    // The discovery times out with no retry FANT, though the source kept sending.
    h.events.advance(2.8);
    c.check(!h.router.discovering(9) && h.out.empty(), "the timeout ends it, no retry FANT");

    // A BANT for the second FANT ends a new discovery, and data takes the trail.
    h.router.onLocalData(9);
    h.router.onAnt(AntMessage{AntType::Backward, 9, 1, 3, 1, 35}, 2);
    c.check(!h.router.discovering(9), "the BANT ends the discovery");
    const AntDecision routed = h.router.onLocalData(9);
    c.check(routed.action == RouteAction::Unicast && routed.nextHop == 2,
            "with a trail, data is unicast as usual");
}

void TestDataAsFantPrecedenceAndOff(Checker& c) {
    Config both = dataAsFant();
    both.bufferDuringDiscovery = true;
    Harness h(1, both);
    c.check(h.router.onLocalData(9).action == RouteAction::Broadcast && !h.router.holds(9),
            "with bufferDuringDiscovery too, data-as-FANT wins: nothing is held");
    h.events.advance(2.8);
    c.check(h.out.empty(), "nor is anything discarded when the discovery ends");

    Config capped = dataAsFant();
    capped.maxDiscoveries = 1;
    Harness cap(1, capped);
    cap.router.onLocalData(8);
    c.check(cap.router.onLocalData(9).action == RouteAction::Drop,
            "the discovery cap refuses a new destination: Drop");

    Harness off(1);
    const AntDecision d = off.router.onLocalData(9);
    c.check(d.action == RouteAction::Drop && !d.message && off.out.size() == 1 &&
                off.out[0].decision.action == RouteAction::Broadcast,
            "switch off: dropped, and a minimal FANT through the sink, as before");
}

}  // namespace ara::core::test
