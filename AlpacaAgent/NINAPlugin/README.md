# AlpacaAgent Client Plugin SDK (NINA-first)

This folder contains a .NET client library scaffold intended for a NINA plugin, but designed so the transport contract is reusable for other clients (for example SGP Pro).

## Project

`src/AlpacaBridge.NINA.AgentPlugin`

- `AgentApiClient`: HTTP client for AlpacaAgent endpoints
- `AgentCheckpointBuilder`: maps runtime snapshot data into checkpoint payloads
- `AgentContracts`: shared payload contracts
- `SequenceJsonSummaryReader`: reads exported NINA sequence JSON (for dev/testing)

## Current status

- Communication contract implementation is present.
- NINA runtime hooks are intentionally left as adapter interfaces (`ISequenceRuntimeSnapshot`) so you can bind to exact NINA APIs in the plugin host project without locking this library to one NINA SDK version.

## Next integration step

In the actual NINA plugin project:

1. Reference this library.
2. Implement `ISequenceRuntimeSnapshot` from NINA sequence state/events.
3. On step changes and heartbeat interval, build checkpoints and call `AgentApiClient.SendCheckpointAsync`.
