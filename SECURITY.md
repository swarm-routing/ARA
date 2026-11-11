# Security Policy

`swarm-routing/ARA` is research code for network simulation. This document records its
security posture honestly; it promises nothing the code does not do. The
org-wide policy is [swarm-routing/.github SECURITY.md](https://github.com/swarm-routing/.github/blob/main/SECURITY.md).

## Trust boundary

<!-- Which input is untrusted, the only code that parses it, and what that
code guarantees. A protocol: its wire-format decoder (org invariant 3). A
tool: the parsers of files it reads. If nothing parses untrusted input, say
so in one line. -->
The decoder of ARA's control headers: `core/ara-ant.cc` `decodeAnt`, which the
ns-3 adapter reaches through `DecodeAntPacket` (`ns3/model/ara-header.cc`) for
every payload on the control port. It reads exactly one 16-byte forward or
backward ant with `swarm-core`'s bounds-checked little-endian readers, and
refuses a wrong version byte, an unknown type, a short buffer or trailing bytes
(org invariant 3); a refused payload is dropped without touching the routing
state. Roundtrip and seeded fuzz tests cover it in `core/test/` and
`ns3/test/`. The duplicate-error and route-error signals of [G02] §3.2-3.3
are not written yet (#5).

## Known, by-design limitations (out of scope)

- **ARA has no authentication** (Güneş, Sorges & Bouazizi, 2002). Any node can
  forge forward or backward ants, and so pheromone, or drop the traffic it
  relays (blackhole, greyhole). Out of scope, as in AntHocNet before its
  default-off security profile (AntHocNet ADR-0020).
- A simulation protocol for ns-3, not a network stack for real radios.

<!-- org-sync:begin reporting -->
## Reporting a vulnerability

Report privately through GitHub's private vulnerability reporting: the
repository's **Security** tab, then **Report a vulnerability**. If that is
not available, open an issue that only asks for a private contact, and put
no vulnerability details in it. This is a research project maintained on a
best-effort basis: expect an acknowledgement within a few days, not an SLA.
<!-- org-sync:end reporting -->

## Verifying what it ships

Nothing is released yet, so there is nothing to verify.
