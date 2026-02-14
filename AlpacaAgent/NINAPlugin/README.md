# AlpacaAgent Client Plugin SDK (NINA-first)

This folder contains a .NET client library scaffold intended for a NINA plugin, but designed so the transport contract is reusable for other clients (for example SGP Pro).

## Project

`src/AlpacaBridge.NINA.AgentPlugin`

- `AgentApiClient`: HTTP client for AlpacaAgent endpoints
- `AgentCheckpointBuilder`: maps runtime snapshot data into checkpoint payloads
- `AgentContracts`: shared payload contracts
- `SequenceJsonSummaryReader`: reads exported NINA sequence JSON and derives planned frame counts from looped `TakeExposure` instructions
- `CheckpointSpoolStore`: local disk queue for offline buffering
- `ResilientCheckpointDispatcher`: sends checkpoints and replays buffered checkpoints after reconnect
- `AgentCheckpointSession`: heartbeat loop (`1-2s`) for continuous checkpoint updates

## Current status

- Communication contract implementation is present.
- A resilient spool/replay path is present so temporary network outages do not drop checkpoint history.
- NINA runtime hooks are intentionally left as adapter interfaces (`ISequenceRuntimeSnapshot` / `ISequenceRuntimeSnapshotProvider`) so you can bind to exact NINA APIs in the plugin host project without locking this library to one NINA SDK version.

## Next integration step

In the actual NINA plugin project:

1. Reference this library.
2. Implement `ISequenceRuntimeSnapshotProvider` from NINA sequence state/events.
3. Optionally load exported sequence JSON (`ReadPlanFromFile`) to enrich checkpoint metadata and fill missing frame totals.
4. Start an `AgentCheckpointSession` with:
   - a configured `AgentApiClient` (`HttpClient.BaseAddress = alpacaAgentUrl`)
   - `CheckpointSpoolStore` path under `%LocalAppData%\\NINA\\Plugins\\<PluginName>\\checkpoint-spool.json`
   - heartbeat interval of `1-2s`
5. Trigger `PublishNowAsync` on sequence transitions (`step change`, `exposure start/end`, `pause/resume/abort/finish`) and keep heartbeat running for continuity.

Example wiring:

```csharp
var http = new HttpClient { BaseAddress = new Uri("http://10.0.0.50:6810") };
var api = new AgentApiClient(http);
var spool = new CheckpointSpoolStore(spoolPath);
var dispatcher = new ResilientCheckpointDispatcher(api, spool);
var plan = SequenceJsonSummaryReader.ReadPlanFromFile(sequenceJsonPath);

var session = new AgentCheckpointSession(
    snapshotProvider,
    new AgentEnvironment("NINA", "NINA", ninaVersion, pluginVersion, Environment.MachineName),
    dispatcher,
    TimeSpan.FromSeconds(2),
    plan);

session.Start();
await session.PublishNowAsync();
```
