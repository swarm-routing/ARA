// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The router observer (swarm-routing/ARA#49): a recording observer sees every
// callback the router makes (control sent and received with their encoded
// size, routes added and removed, data decisions, the trail table's size), and
// attaching one changes no decision, send or random draw (org rule 5).
#include "ara-core-tests.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ara::core::test {

namespace {

using swarm::core::RouteAction;

/// Records every event as one line, in order.
struct Recorder : swarm::core::IRouterObserver {
    std::vector<std::string> log;
    void onControlSent(std::uint8_t kind, bool broadcast, std::uint32_t bytes) override {
        log.push_back("tx " + std::to_string(kind) + (broadcast ? " bcast " : " ucast ") +
                      std::to_string(bytes));
    }
    void onControlReceived(std::uint8_t kind, std::uint32_t bytes) override {
        log.push_back("rx " + std::to_string(kind) + " " + std::to_string(bytes));
    }
    void onRouteChanged(NodeAddress dest, NodeAddress nextHop, bool added) override {
        log.push_back("route " + std::to_string(dest) + " " + std::to_string(nextHop) +
                      (added ? " add" : " remove"));
    }
    void onDataDecision(NodeAddress dest, RouteAction action) override {
        log.push_back("data " + std::to_string(dest) + " " +
                      std::to_string(static_cast<int>(action)));
    }
    void onStateSize(std::uint32_t entries, std::uint32_t bytes) override {
        log.push_back("state " + std::to_string(entries) + " " + std::to_string(bytes));
    }
};

using Lines = std::vector<std::string>;

std::string action(RouteAction a) {
    return std::to_string(static_cast<int>(a));
}

const std::string kAntBytes = std::to_string(kAntWireSize);
const std::string kEntryBytes = std::to_string(Router::kTrailEntryBytes);

/// What a run did, for the with-and-without comparison.
struct RunResult {
    std::vector<std::string> trace;
    std::vector<std::string> decisions;
    int draws = 0;
    int fants = 0;
    int bants = 0;
    swarm::core::RouterStats stats;  ///< empty when not observed
};

/// A 3 x 3 grid (nodes 0..8): two discoveries, data decisions, a data packet
/// down the trail, then everything evaporates. Observed by a CountingObserver
/// over the network's own clock, or not.
RunResult run(std::uint64_t seed, bool observed) {
    Network net(seed);
    swarm::core::CountingObserver counting(net.events());
    if (observed) net.observe(&counting);
    for (int r = 0; r < 3; ++r) {
        for (int col = 0; col < 3; ++col) {
            const int n = r * 3 + col;
            if (col < 2) net.addLink(n, n + 1);
            if (r < 2) net.addLink(n, n + 3);
        }
    }
    RunResult out;
    auto note = [&](const char* what, NodeAddress at, const AntDecision& d) {
        out.decisions.push_back(std::string(what) + " " + std::to_string(at) + " " +
                                action(d.action) + " " + std::to_string(d.nextHop));
    };
    note("local", 0, net.node(0).onLocalData(8));  // no trail: a Drop, a discovery
    net.node(2).onLocalData(6);
    net.events().runUntil(1.0);
    note("local", 0, net.node(0).onLocalData(8));  // the trail is there
    note("transit", 4, net.node(4).onTransitData(8));
    note("transit", 4, net.node(4).onTransitData(4));
    note("transit", 4, net.node(4).onTransitData(77));
    NodeAddress at = 0;
    for (int hops = 0; hops < 8 && at != 8; ++hops) {
        const NodeAddress next = net.node(at).nextHop(8);
        if (next == swarm::core::kInvalidAddress) break;
        net.node(at).onDataSent(8, next);
        net.node(next).onDataReceived(0, at);
        at = next;
    }
    net.events().runUntil(80.0);
    for (int n = 0; n < 9; ++n) {
        out.decisions.push_back("next " + std::to_string(n) + " " +
                                std::to_string(net.node(n).nextHop(8)) + " " +
                                std::to_string(net.node(n).nextHop(6)));
    }
    out.trace = net.trace();
    out.draws = net.rng().draws();
    out.fants = net.sent(AntType::Forward);
    out.bants = net.sent(AntType::Backward);
    out.stats = counting.stats();
    return out;
}

}  // namespace

void TestObserverCallbacks(Checker& c) {
    // A relay: it hears a FANT for node 9 from node 2.
    Harness h(5);
    Recorder rec;
    h.router.setObserver(&rec);
    h.router.onAnt(AntMessage{AntType::Forward, 1, 9, 7, 0, 35}, 2);
    c.check(rec.log == Lines{"rx 1 " + kAntBytes, "route 1 2 add", "state 1 " + kEntryBytes},
            "a FANT: received, the trail toward its source added, the table's size");
    h.events.advance(0.02);  // the jittered relay leaves
    c.check(rec.log.size() == 4 && rec.log[3] == "tx 1 bcast " + kAntBytes,
            "the relayed FANT: sent as a broadcast of the encoded size");
    rec.log.clear();
    h.router.onAnt(AntMessage{AntType::Forward, 1, 9, 7, 1, 34}, 3);
    c.check(rec.log == Lines{"rx 1 " + kAntBytes},
            "a duplicate FANT is received and changes nothing else");
    rec.log.clear();
    h.router.onAnt(AntMessage{AntType::Forward, 5, 9, 1, 0, 35}, 3);
    c.check(rec.log == Lines{"rx 1 " + kAntBytes},
            "an echo of this node's own FANT is received and dropped");

    // Data decisions: Unicast down a trail, Deliver to itself, Drop with none.
    rec.log.clear();
    h.router.onLocalData(1);
    h.router.onLocalData(5);
    h.router.onTransitData(1);
    h.router.onTransitData(5);
    h.router.onTransitData(8);
    c.check(rec.log == Lines{"data 1 " + action(RouteAction::Unicast),
                             "data 5 " + action(RouteAction::Deliver),
                             "data 1 " + action(RouteAction::Unicast),
                             "data 5 " + action(RouteAction::Deliver),
                             "data 8 " + action(RouteAction::Drop)},
            "data decisions: forward, deliver, forward, deliver, drop");
    rec.log.clear();
    h.router.onLocalData(8);  // no trail: drops, and starts the discovery
    c.check(rec.log == Lines{"data 8 " + action(RouteAction::Drop), "tx 1 bcast " + kAntBytes} ||
                rec.log == Lines{"tx 1 bcast " + kAntBytes, "data 8 " + action(RouteAction::Drop)},
            "no trail: a drop, and the discovery's FANT");

    // Route maintenance by data: a new entry is an add, a raise is not.
    rec.log.clear();
    h.router.onDataReceived(9, 6);
    c.check(rec.log ==
                Lines{"route 9 6 add", "state 2 " + std::to_string(2 * Router::kTrailEntryBytes)},
            "data from a new direction adds a route and grows the table");
    rec.log.clear();
    h.router.onDataSent(1, 2);
    h.router.onDataReceived(9, 6);
    c.check(rec.log.empty(), "reinforcing an existing entry changes neither routes nor size");

    // Evaporation removes the entries, in (destination, neighbour) order.
    rec.log.clear();
    h.events.advance(60.0);
    h.router.nextHop(1);
    c.check(rec.log == Lines{"route 1 2 remove", "route 9 6 remove", "state 0 0"},
            "expiry: each removed trail and the empty table");

    // A destination answers with a BANT, which the source then receives.
    Harness dst(9);
    Recorder drec;
    dst.router.setObserver(&drec);
    dst.router.onAnt(AntMessage{AntType::Forward, 1, 9, 7, 2, 33}, 8);
    c.check(drec.log == Lines{"rx 1 " + kAntBytes, "route 1 8 add", "state 1 " + kEntryBytes,
                              "tx 2 ucast " + kAntBytes},
            "the destination: FANT in, trail, BANT out as a unicast");
    Harness src(1);
    Recorder srec;
    src.router.setObserver(&srec);
    src.router.onLocalData(9);
    srec.log.clear();
    src.router.onAnt(AntMessage{AntType::Backward, 9, 1, 1, 2, 33}, 2);
    c.check(srec.log == Lines{"rx 2 " + kAntBytes, "route 9 2 add", "state 1 " + kEntryBytes},
            "the source: the BANT in and the trail toward the destination");
    // A BANT relay forwards along the reverse pointer the FANT left.
    Harness mid(5);
    Recorder mrec;
    mid.router.setObserver(&mrec);
    mid.router.onAnt(AntMessage{AntType::Forward, 1, 9, 7, 0, 35}, 2);
    mrec.log.clear();
    mid.router.onAnt(AntMessage{AntType::Backward, 9, 1, 7, 1, 34}, 6);
    c.check(mrec.log == Lines{"rx 2 " + kAntBytes, "route 9 6 add",
                              "state 2 " + std::to_string(2 * Router::kTrailEntryBytes),
                              "tx 2 ucast " + kAntBytes},
            "a BANT relay: in, the trail toward the destination, out as a unicast");

    // A full table refuses the write (org rule 7): no route, no new size.
    Config tiny;
    tiny.pheromoneCapacity = 1;
    Harness full(5, tiny);
    Recorder frec;
    full.router.setObserver(&frec);
    full.router.onAnt(AntMessage{AntType::Forward, 1, 9, 1, 0, 35}, 2);
    full.router.onAnt(AntMessage{AntType::Forward, 3, 9, 1, 0, 35}, 4);
    c.check(frec.log == Lines{"rx 1 " + kAntBytes, "route 1 2 add", "state 1 " + kEntryBytes,
                              "rx 1 " + kAntBytes},
            "a refused trail is not reported as a route");

    // Detached, it is silent again.
    rec.log.clear();
    h.router.setObserver(nullptr);
    h.router.onAnt(AntMessage{AntType::Forward, 2, 9, 3, 0, 35}, 4);
    h.router.onLocalData(2);
    c.check(rec.log.empty(), "no observer, no report");
}

void TestObserverIsReadOnly(Checker& c) {
    const RunResult plain = run(7, false);
    const RunResult seen = run(7, true);
    c.check(plain.trace.size() > 9 && plain.draws > 0, "the run did something");
    c.check(seen.trace == plain.trace, "the ants sent are identical with an observer");
    c.check(seen.decisions == plain.decisions, "every data decision and next hop is identical");
    c.check(seen.draws == plain.draws, "no extra random draw");

    // What the observer counted matches what the network saw leave the routers.
    const auto& kinds = seen.stats.control;
    const auto fant = kinds.find(static_cast<std::uint8_t>(AntType::Forward));
    const auto bant = kinds.find(static_cast<std::uint8_t>(AntType::Backward));
    c.check(fant != kinds.end() && bant != kinds.end() && kinds.size() == 2,
            "exactly the two ant kinds were counted");
    if (fant != kinds.end() && bant != kinds.end()) {
        c.check(fant->second.txPackets == static_cast<std::uint64_t>(seen.fants) &&
                    bant->second.txPackets == static_cast<std::uint64_t>(seen.bants),
                "sent counts equal the ants that left the routers");
        c.check(fant->second.txBroadcasts == fant->second.txPackets &&
                    bant->second.txBroadcasts == 0,
                "FANTs are broadcasts, BANTs unicasts");
        c.check(fant->second.txBytes == fant->second.txPackets * kAntWireSize,
                "every ant is its encoded size");
        c.check(fant->second.rxPackets > 0 && bant->second.rxPackets > 0, "and received");
    }
    c.check(seen.stats.routeAdds > 0 && seen.stats.routeRemoves == seen.stats.routeAdds,
            "every trail laid evaporated by t = 80 s, and each was reported both ways");
    c.check(seen.stats.stateEntries == 0 && seen.stats.peakStateEntries > 0,
            "the table grew and emptied");
    c.check(seen.stats.decisions(RouteAction::Unicast) >= 2 &&
                seen.stats.decisions(RouteAction::Drop) >= 3 &&
                seen.stats.decisions(RouteAction::Deliver) >= 1,
            "forward, drop and deliver decisions counted");
}

}  // namespace ara::core::test
