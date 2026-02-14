# AlpacaAgent

AlpacaAgent is a client-agnostic sequencing continuity service for AlpacaBridge.

## Why this exists

When a control client disconnects (Wi-Fi drop, app crash, laptop sleep), the run context should survive. AlpacaAgent stores checkpoint state server-side and keeps reconnect context available.

## Current scope (Mode A)

- Receives checkpoint heartbeats from clients (NINA now, extensible to SGP Pro and other Alpaca clients)
- Tracks run state with monotonic checkpoint handling
- Persists run/event state to disk
- Provides run/event query endpoints for reconnect workflows
- Includes a NINA-side SDK (`AlpacaAgent/NINAPlugin`) with local spool/replay so transient network outages do not lose checkpoints

## API

- `POST /agent/v1/checkpoints` (canonical, client-agnostic)
- `POST /agent/nina/checkpoint` (NINA compatibility alias)
- `POST /agent/{clientType}/checkpoint` (generic compatibility alias)
- `POST /agent/runs/{runId}/action` (run control: pause/resume/acknowledge)
- `GET /agent/runs`
- `GET /agent/runs/{runId}`
- `GET /agent/runs/{runId}/events?since=<id>`
- `GET /agent/v1/capabilities`
- `GET /health`

### Required checkpoint fields

```json
{
  "runId": "uuid-or-stable-id",
  "checkpointNo": 42,
  "state": "Running"
}
```

Strongly recommended fields (used in state summaries): `sequenceName`, `timestampUtc`, `current`, `guiding`, `environment`.

## Build

```sh
cmake -S AlpacaAgent -B AlpacaAgent/build
cmake --build AlpacaAgent/build --parallel
ctest --test-dir AlpacaAgent/build --output-on-failure
```

## Run

```sh
./AlpacaAgent/build/alpacaagent_server --config AlpacaAgent/agent_config.json
```

If no config file exists, defaults are used.

## Config file example

See `AlpacaAgent/agent_config.example.json`.

### Run control policy settings

- `autoPauseOnDisconnect` (default `true`)
- `autoResumeOnReconnect` (default `true`)
- `holdEngageAfterDisconnectSeconds` (default `10`)
