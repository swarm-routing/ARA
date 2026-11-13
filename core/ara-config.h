// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// ARA's protocol constants. [G02] (Güneş, Sorges & Bouazizi 2002) gives no
// value for any of them; each default cites its docs/fidelity.md Parameters
// row, which records the maintainer decisions of 2026-11-10 on ARA#4 (the old
// arm's values, kept for continuity with v2.5.0's runs) and ARA#5 (retries
// only while the source keeps sending, the 64-entry history cap).
#ifndef ARA_CORE_CONFIG_H
#define ARA_CORE_CONFIG_H

#include "swarm-types.h"

#include <cstddef>
#include <cstdint>

namespace ara::core {

using swarm::core::Time;

struct Config {
    /// Evaporation rate q of eq. 2, ϕ := (1 − q)·ϕ, per evaporationPeriod.
    /// Fidelity "Evaporation rate q, value": 0.1 per period (#4); range (0, 1]
    /// from [G02] eq. 2.
    double evaporationRate = 0.1;
    /// Fidelity "Evaporation period": 1 s, evaluated continuously (#4).
    Time evaporationPeriod = 1.0;
    /// Fidelity "Deposit Δϕ per data packet": 0.1 (#4). [G02] §3.2's route
    /// maintenance adds it on every data hop, toward both ends.
    double deposit = 0.1;
    /// Fidelity "Route-expiry threshold": an entry below 0.01 is gone (#4).
    double expiryThreshold = 0.01;
    /// Fidelity "FANT maximum hops / TTL": 35; the BANT carries the same (#4).
    std::uint8_t antTtl = 35;
    /// Fidelity "FANT relay jitter": U[0, 10 ms) from the injected RNG (#4).
    Time relayJitterMax = 0.010;
    /// Fidelity "Discovery retry timeout and count": 2.8 s, doubling per
    /// retry (#4) ...
    Time discoveryTimeout = 2.8;
    /// ... and 2 retries, each only if data for the destination arrived since
    /// the last FANT (#4, #5).
    int maxDiscoveryRetries = 2;
    /// Fidelity "Ant and data dedup history size": at most 64 entries, oldest
    /// evicted first (#5). The same cap bounds the BANT's reverse pointers,
    /// which are recorded with the FANT history entries.
    std::size_t historyCapacity = 64;
    /// The same row: an entry also expires 30 s after it is recorded (#4),
    /// in the four histories alike (FANT, BANT, reverse pointers, data). The
    /// age is read against the clock when a history is read or written; there
    /// is no purge timer.
    Time historyLifetime = 30.0;
    /// Fidelity "Pheromone table capacity": 4096 (destination, neighbour)
    /// entries, repo choice under org rule 7.
    std::size_t pheromoneCapacity = 4096;
    /// Fidelity "Concurrent discoveries per node": 64, repo choice under
    /// org rule 7.
    std::size_t maxDiscoveries = 64;
};

}  // namespace ara::core

#endif  // ARA_CORE_CONFIG_H
