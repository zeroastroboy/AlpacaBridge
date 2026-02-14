# AlpacaBridge Agent + NINA Sync (Design Notes)

**Purpose:** Create an *agent* that can run (or at least safely “hold”) an imaging session so that **temporary network disconnects** (Wi‑Fi drops, UI crashes, laptop sleep) do not ruin the night.

This document summarizes our conversation and turns it into an actionable design spec that Codex can implement.

---

## 1) Problem Statement

Today, NINA “owns” the sequence. If the client disconnects, the *hardware* is still fine, but **the orchestration state is lost** unless there is a server-side component that persists state and can continue (or maintain safe operation).

We want:

- **Sequence continuity**: the run should keep going (best case) or at least remain safe and recoverable.
- **Reconnectability**: a client can reconnect and regain full context.
- **Cross-platform**: AlpacaBridge may run on Linux, Windows, or macOS.
- **NINA compatibility**: users can keep using NINA, but we can add resilience via a plugin and an agent.

---

## 2) Key Insight

AlpacaBridge can observe device calls (camera exposures, filter changes, focuser moves), but **it cannot reliably reconstruct NINA’s internal sequence state** unless NINA *explicitly publishes* checkpoints.

Therefore:

- **Passive tracking (no NINA changes)** is possible but limited and best-effort.
- **Checkpoint/heartbeat (NINA plugin)** gives reliable “where we are” state.
- **Full continuity (agent-owned sequencing)** requires moving orchestration to the server, or at least buffering/ownership of a capture block.

---

## 3) Guiding & PHD2: What Works

### 3.1 NINA does NOT require PHD2 to be local
NINA can connect to **PHD2 over the network** by specifying PHD2 server host/IP and port (commonly 4400), provided PHD2’s server is enabled and firewall allows it.
- PHD2: `Tools → Enable Server`
- NINA: configure **PHD2 Server URL/Port** (often default `localhost:4400`, but can be remote IP)

References (for implementer context):
- NINA guider settings mention server URL/port and server must be enabled (NINA docs). citeturn0search1
- PHD2 manual: enable server under Tools. citeturn0search11

### 3.2 “Keep guiding running” after disconnect
Guiding can continue through a network drop **if guiding is running in an independent process** (e.g., PHD2) and remains connected to mount + guide camera.

This suggests a strong default architecture:
- **PHD2 runs on the same host as AlpacaBridge/Agent** (Pi/NUC/miniPC at the scope).
- NINA connects remotely to that PHD2 instance.
- If laptop Wi‑Fi drops, **PHD2 keeps guiding**.

### 3.3 What you *don’t* get automatically
- Dithers are often triggered by NINA. If NINA drops, dithering stops (guiding can continue).
- If NINA is the only thing initiating guider state transitions, you need the agent to either:
  - keep PHD2 in “guiding” state, and/or
  - provide minimal policies (e.g., restart guiding if it stopped unexpectedly).

---

## 4) Two Operating Modes

### Mode A — “NINA-owned sequence + Agent tracking” (V1 recommended)
NINA still runs the sequence, but:
- A **NINA plugin** sends checkpoints/heartbeats to the Agent.
- The Agent persists the last-known sequence state.
- On disconnect, Agent can keep critical subsystems stable (cooling, mount tracking, guiding if PHD2 is separate).

This is the fastest path to real value.

### Mode B — “Agent-owned sequence” (V2 / endgame)
Agent becomes the sequencer:
- Clients become optional UIs (can disconnect).
- Agent handles step progression, dithering triggers, autofocus scheduling, meridian flip logic, safety policies, etc.

Mode A can later transition into Mode B without breaking the API if we design the Agent endpoints carefully.

---

## 5) Agent Architecture

### 5.1 Components
- **AlpacaBridge (existing):** device endpoints (Camera, Mount, Focuser, FilterWheel, etc.)
- **Agent (new):** sequencing state, job ownership, persistence, event stream
- **Optional PHD2 Supervisor (new):** talks to PHD2 via API and/or monitors its health
- **Client (NINA plugin / future OpenAstro UI):** sends checkpoints, receives status/events

### 5.2 Agent responsibilities (Mode A)
- Maintain **run registry**:
  - runId, startedAt, lastSeen, currentStepId/name, counters, state
- Maintain **health registry**:
  - PHD2 connected? guiding? RMS? star lost?
  - device connectivity
  - disk free
- Provide **status** to any UI
- Provide **event log** (structured) for recovery and debugging
- Optionally enforce **safety hold** policies if the client disappears for too long.

---

## 6) NINA Plugin → Agent Sync Contract (Minimal)

### 6.1 Checkpoint payload
Recommended POST payload fields (keep it stable and versioned):

```json
{
  "apiVersion": "1.0",
  "runId": "uuid",
  "sequenceName": "M42 - LRGB",
  "timestampUtc": "2026-02-14T18:12:33Z",
  "checkpointNo": 1542,

  "state": "Running", 
  "current": {
    "instructionId": "uuid-or-stable-hash",
    "instructionName": "Capture",
    "target": "M42",
    "filter": "L",
    "exposureSeconds": 300,
    "frameIndex": 14,
    "frameTotal": 60,
    "etaSeconds": 217
  },

  "guiding": {
    "required": true,
    "provider": "PHD2",
    "phd2Host": "10.0.0.50",
    "phd2Port": 4400,
    "ditherEveryNFrames": 1
  },

  "environment": {
    "clientName": "NINA",
    "clientVersion": "x.y.z",
    "pluginVersion": "a.b.c",
    "machine": "Joey-Laptop"
  }
}
```

**Notes**
- `checkpointNo` should be monotonic per run to detect old/out-of-order updates.
- `instructionId` can be a stable ID or hash derived from the sequence step.
- Keep `apiVersion` for future compatibility.

### 6.2 Heartbeat interval
- Heartbeat every **1–2 seconds** while running.
- Always send a checkpoint on **step change**, **exposure start**, **exposure end**, **pause**, **resume**, **abort**, **sequence finished**.

### 6.3 Failure detection
Agent marks NINA disconnected when:
- `now - lastSeen > disconnectThresholdSeconds` (e.g., 10–20 seconds configurable)

---

## 7) Agent HTTP API (Draft)

### 7.1 Endpoints (Mode A)
- `POST /agent/nina/checkpoint`
  - Accepts payload above
  - Returns `{ accepted: true, serverTimeUtc, runState }`
- `GET /agent/runs`
  - Lists active and recent runs
- `GET /agent/runs/{runId}`
  - Detailed last checkpoint + derived state
- `GET /agent/runs/{runId}/events?since=<id|time>`
  - Stream/poll events (server-side log)
- `POST /agent/runs/{runId}/action`
  - (Optional) request actions like `PauseRequested`, `StopRequested`
  - In Mode A these are *advisory* unless the agent is also enforcing holds.

### 7.2 Events
Agent emits events like:
- `RunStarted`, `CheckpointReceived`, `ClientDisconnected`, `ClientReconnected`
- `ExposureStarted/Ended` (if inferred from traffic and/or checkpoint)
- `GuidingStateChanged`, `SafetyHoldEngaged`, `SafetyHoldReleased`

---

## 8) Guiding Strategy (Mode A)

### 8.1 Recommended “best reliability” layout
- Run **PHD2 on the AlpacaBridge host** (same machine controlling devices)
- Enable PHD2 server
- Configure firewall to allow inbound TCP port (e.g., 4400)

This ensures:
- Laptop Wi‑Fi drop does not stop guiding
- Agent can monitor guiding locally

### 8.2 Agent → PHD2 integration (optional but valuable)
Add a PHD2 supervisor that:
- Reads PHD2 state (guiding, paused, star lost)
- Emits events to Agent event log
- Can attempt recovery actions (reconnect / resume guiding) under policy

Policy examples:
- If `ClientDisconnected` and guiding stops unexpectedly → attempt `resumeGuiding`
- If star lost persists > N seconds → engage safety hold (stop exposures / park)

---

## 9) Passive Tracking (Optional Enhancement)

Even with NINA plugin checkpoints, AlpacaBridge can improve confidence by observing device calls and correlating them with checkpoints.

Examples:
- When Camera `StartExposure` observed, emit `ExposureStarted`
- When `ImageReady` / download complete observed, emit `ExposureEnded`
- When FilterWheel position changes, update derived filter state

This gives:
- Better timelines
- Better root-cause logs (“NINA said it started exposure, but device never started”)

---

## 10) “Safety Hold” Concept (Recommended)

If the client disconnects, the agent should have a configurable behavior:

- **Hold Policy**
  - Keep cooler on
  - Keep mount tracking (or park after a timeout)
  - Keep guiding running (if independent)
  - Do NOT start new exposures unless the client is connected (strict mode)
  - OR continue current exposure only (soft mode)

Because AlpacaBridge may be serving multiple clients, “hold” needs careful coordination:
- Per-run ownership model
- Session token / runId + authentication (future)

---

## 11) Authentication (Future)

We discussed preventing other people from controlling someone’s gear. For the agent API:
- Start with simple API key / bearer token
- Tie token to runId ownership
- Optional: local-only default + explicit remote enable

---

## 12) Implementation Milestones

### Milestone 1 — Agent skeleton + persistence
- Run registry, checkpoint receiver, event log
- JSON file persistence or SQLite (SQLite recommended)

### Milestone 2 — NINA plugin MVP
- Send heartbeat/checkpoints
- Include runId, step, frame counters, guiding info

### Milestone 3 — UI / status
- Minimal status endpoint and a simple web dashboard (optional)
- Show lastSeen, current step, exposure ETA, guiding status

### Milestone 4 — PHD2 supervisor
- Monitor PHD2 guiding state via API
- Log events; optional recovery actions

### Milestone 5 — Safety hold policies
- “Soft hold” and “strict hold”
- Configurable timeouts and actions

### Milestone 6 — Agent-owned sequencing (V2)
- Create a “Sequencer” device-like API or dedicated orchestrator API
- Start by supporting a minimal capture plan (filter + exposure + count)
- Expand to autofocus, dithering, meridian flips, plate solving

---

## 13) Design Principles

- **Alpaca-shaped** endpoints and predictable state models
- **Idempotent actions**: repeated calls shouldn’t duplicate actions
- **Monotonic checkpoints**: tolerate retries and out-of-order packets
- **Server-owned persistence**: reboot-safe where possible
- **Event-first observability**: logs and events are first-class output

---

## 14) Open Questions (Non-blocking)

- Where should the agent live: inside AlpacaBridge process vs sidecar service?
- How to model “ownership” when multiple clients exist?
- What minimal set of sequence metadata does NINA plugin need to expose?
- Should we implement a PHD2 API compatibility shim if we ever embed guiding?

---

## 15) Quick “Recommended Default” Summary

**Best first release:**
- Agent service on same host as AlpacaBridge
- NINA plugin sends checkpoints/heartbeats
- PHD2 runs on same host; NINA connects to PHD2 by IP:4400
- If Wi‑Fi drops, guiding continues and agent preserves sequence context for reconnect

