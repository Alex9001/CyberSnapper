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

The agent launches one worker process per active job and exchanges protocol-v2 newline-delimited JSON. The agent assigns final event sequence numbers, transactionally applies state changes with their events, and then broadcasts them to GUI/CLI/API consumers. Heartbeats detect a hung worker. Worker crashes become terminal failed jobs; persisted queued jobs are recovered in global FIFO order, while jobs left running after an agent crash are marked interrupted when the project reopens.

## Data ownership

Only the agent opens project SQLite databases for normal operation. SQLite uses WAL mode, foreign keys, and a per-project lock file. The worker receives an agent-approved absolute project root and refuses a different root. Artifact paths are relative to that root and are containment-checked before API or desktop access.

The queue is FIFO. One job runs by default and the user may opt into two active jobs. Each profile independently controls capture concurrency up to ten page contexts. Saved target sets are resolved by the agent at submission time and embedded as a job snapshot, which keeps scheduled runs, retries, history, and review provenance deterministic even after a set is edited.

## Visual review model

The worker persists actionable comparison records for changed images, missing baselines, and analysis failures. It records the exact baseline snapshot, analyzed pixel dimensions, counts, algorithm version, and changed-region metadata. The native Review workspace renders those records rather than recomputing state in the UI.

Review decisions are separate durable rows with an optimistic revision number. Accepting a result copies its artifact to an immutable versioned baseline file and atomically updates both the active baseline pointer and the review decision. Ignoring or resetting a result changes only its review record. Old comparisons continue to reference the baseline file they were originally measured against.

## Capture boundary

The worker accepts explicit `http://` and `https://` URLs. Browsers connect through a job-local filtering proxy that resolves each destination, rejects link-local/private/documentation/multicast ranges, and connects to the exact checked address. This prevents redirects, subresources, and DNS rebinding from escaping the policy. Loopback is allowed only when the active project explicitly enables localhost; private LAN ranges remain blocked. URL credentials and non-HTTP protocols are rejected.

Jobs are capped at 10,000 artifacts. Raster work is capped at 64 million device pixels, worker messages at 16 MiB, and writes require a free-space reserve. Output filenames are allocated serially inside a job so concurrent targets cannot claim the same path.

Each browser engine is launched headlessly. Each target uses an isolated Playwright context. Browser installation is an explicit agent operation and capture jobs never install software automatically.

## REST boundary

REST v1 is disabled by default and binds to `127.0.0.1` only. All routes except health require a high-entropy bearer token. CyberSnapper stores only a SHA-256 token digest and compares digests without an early exit. Request bodies are capped at 1 MiB.

The API server runs on a dedicated native thread. Requests marshal synchronously to the agent's Qt thread, so project and queue state retain single-owner semantics.

## Scheduling

Schedules are stored in the project database. Recurrences use an IANA time zone and calculate the next UTC occurrence at runtime. Missed occurrences coalesce to one run; the scheduler does not create an unbounded backlog. A once-only schedule disables itself after it fires.

The GUI can install an OS-user login entry for the headless agent: a Run registry value on Windows, a LaunchAgent on macOS, or an XDG autostart desktop entry on Linux. No administrator access is required.
