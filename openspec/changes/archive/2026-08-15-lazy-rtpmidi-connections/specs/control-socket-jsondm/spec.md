# Control socket jsondm Specification

## Purpose

Control socket command dispatch and response composition built on typed command structs, preserving the legacy wire protocol byte-compatibly.

## ADDED Requirements

### Requirement: Status response includes an exports section
The daemon status response SHALL include an `exports` object alongside the existing `router` and `mdns` sections. It SHALL list, per entity: generic Network servers, waiting remote ports (`connect_to` and discovered, with their target address), rawmidi exports (name and device path), and auto-exported seq ports (name and local port). Each entry SHALL carry its current state (e.g. waiting, subscribed, connected). Existing status sections and peer status shapes SHALL be unchanged.

#### Scenario: Exports listed in status
- **WHEN** a client requests the daemon status with waiting ports, rawmidi exports, and auto-exported seq ports present
- **THEN** the response contains an `exports` object with an entry per entity and each entry's state

#### Scenario: Existing sections unchanged
- **WHEN** a client requests the daemon status
- **THEN** the `router` and `mdns` sections serialize exactly as before
