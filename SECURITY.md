# Security Policy

`swarm-routing/ARA` is research code for network simulation. This document records its
security posture honestly; it promises nothing the code does not do. The
org-wide policy is [swarm-routing/.github SECURITY.md](https://github.com/swarm-routing/.github/blob/main/SECURITY.md).

## Trust boundary

<!-- Which input is untrusted, the only code that parses it, and what that
code guarantees. A protocol: its wire-format decoder (org invariant 3). A
tool: the parsers of files it reads. If nothing parses untrusted input, say
so in one line. -->
The decoder of ARA's control headers (forward and backward ants, the
duplicate-error and route-error signals), to be built on `swarm-core`'s
bounds-checked little-endian readers with roundtrip and fuzz tests (org
invariant 3). It is not written yet (#5): until it is, nothing in this
repository parses untrusted input.

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
