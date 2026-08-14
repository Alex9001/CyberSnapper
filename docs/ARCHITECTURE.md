# Architecture

## Process model

CyberSnapper separates presentation, orchestration, and untrusted web execution:

| Process | Responsibility | Persistent state |
|---|---|---|
| `CyberSnapper` | Native Qt Widgets UI | Window/UI preferences only |
| `cybersnapper-agent` | Single project owner, queue, schedules, REST API, tray | QSettings and project SQLite databases |
| `cybersnapper-cli` | Native automation CLI | None; talks to the agent |
| `worker/dist/main.cjs` | One capture job using Playwright and Sharp | Artifacts inside the selected project |

GUI and CLI requests use versioned, length-prefixed JSON over `QLocalSocket`. The OS user is the IPC security boundary. The protocol rejects frames larger than 16 MiB.

The agent launches one worker process per active job and exchanges newline-delimited JSON. The agent assigns final event sequence numbers, persists each event, and broadcasts it to GUI/CLI/API consumers. Worker crashes become terminal failed jobs; jobs left running after an agent crash are marked interrupted when the project reopens.

## Data ownership

Only the agent opens project SQLite databases for normal operation. SQLite uses WAL mode, foreign keys, and a per-project lock file. The worker receives an agent-approved absolute project root and refuses a different root. Artifact paths are relative to that root and are containment-checked before API or desktop access.

The queue is FIFO. One job runs by default and the user may opt into two active jobs. Each profile independently controls capture concurrency up to ten page contexts.

## Capture boundary

The worker accepts explicit `http://` and `https://` URLs. Before navigation it resolves host addresses and rejects loopback, link-local, private, documentation, multicast, and other non-public ranges. Request routing repeats that check for redirects and subresources. URL credentials and non-HTTP protocols are rejected.

Each browser engine is launched headlessly. Each target uses an isolated Playwright context. Browser installation is an explicit agent operation and capture jobs never install software automatically.

## REST boundary

REST v1 is disabled by default and binds to `127.0.0.1` only. All routes except health require a high-entropy bearer token. CyberSnapper stores only a SHA-256 token digest and compares digests without an early exit. Request bodies are capped at 1 MiB.

The API server runs on a dedicated native thread. Requests marshal synchronously to the agent's Qt thread, so project and queue state retain single-owner semantics.

## Scheduling

Schedules are stored in the project database. Recurrences use an IANA time zone and calculate the next UTC occurrence at runtime. Missed occurrences coalesce to one run; the scheduler does not create an unbounded backlog. A once-only schedule disables itself after it fires.
