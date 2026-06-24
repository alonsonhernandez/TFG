# Multi-Channel Operation strategies for V2X — TFG

This repository contains the code developed for a Bachelor's Thesis (TFG) at
ETSIT-UPM on channel-selection strategies for Multi-Channel Operation (MCO) in
V2X / ITS-G5 networks.

## Background and attribution

The work builds on two layers of prior code, and it is worth being clear about
where each part comes from:

- **Vanetza** ([riebl/vanetza](https://github.com/riebl/vanetza)) is the
  open-source implementation of the ETSI C-ITS protocol stack used as the base.
- On top of it, **Sepulcre et al.** published a platform that integrates a
  Facilities-layer congestion control (MCO_FAC) and a virtualized testing
  environment over Docker, released together with their paper [1] at
  [msepulcre/mcoVanetza](https://github.com/msepulcre/mcoVanetza). That platform
  is the starting point of this project.

This repository extends Sepulcre's platform to implement and evaluate different
**channel-selection strategies**. The contributions below are the work of this
TFG; everything else (the MCO_FAC entity, the adaptive DCC, the δ computation,
the Traffic-Class resource sharing and the Docker environment) belongs to the
platforms cited above.

> M. Sepulcre, Y. Guadalcazar, M. A. Fornell, G. Thandavarayan,
> F. Paredes Vera, J. Gozalvez, A. Mohammadisarab, "V2X Congestion Control for
> Multi-Channel Operation: a Scalable Validation in Virtualized Environments",
> Proc. IEEE VTC2025-Spring, Oslo, Norway, June 2025.

## What this TFG adds

The extensions developed in this repository, all integrated into the `socktap`
tool, are:

1. **Four channel-selection modes.** Traffic is split between the control
   channel (CCH) and a service channel (SCH) according to the selected mode:
   `cch_only`, `sch_only`, `mco_static` (fixed split by criticality) and
   `mco_dynamic` (adaptive).

2. **Dynamic mode with hysteresis.** In `mco_dynamic`, non-critical traffic is
   offloaded to the SCH when the CCH load crosses an activation threshold (0.68)
   and returns to the CCH only when it drops below a lower recovery threshold
   (0.58). The two-threshold design, together with a short dead-band before
   reverting, avoids ping-pong between channels. Critical CAM traffic
   (port 2001) always stays on the CCH.

3. **Per-channel CBR measurement.** The CBR is now estimated separately for each
   channel, using independent byte counters for CCH and SCH.

4. **Asynchronous logging.** The telemetry logging was redesigned with a
   producer–consumer scheme and one CSV file per container, so that disk writes
   no longer block the main loop. This removed the timing errors that appeared
   when scaling to tens of concurrent containers under WSL2.

5. **Node entry/exit scenarios.** Added Docker Compose setups and orchestration
   scripts to inject and remove vehicles during a running simulation, plus the
   Python tools used to aggregate the per-node CSVs and plot CBR and δ.

## Building and running

Build follows the original Vanetza instructions
([prerequisites](https://www.vanetza.org/how-to-build/#prerequisites),
[compilation](https://www.vanetza.org/how-to-build/#compilation)). The test
scenarios run on [Docker](https://www.docker.com/); the compose files and
scripts to reproduce the experiments are included in this repository.
