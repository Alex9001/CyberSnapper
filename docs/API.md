# REST API v1

The optional API listens on `http://127.0.0.1:39071` by default. With the exception of health, requests require:

```http
Authorization: Bearer YOUR_TOKEN
```

Errors use `{"error":{"code":"…","message":"…"}}` and an appropriate HTTP status. JSON request bodies are limited to 1 MiB.

## Routes

| Method | Route | Purpose |
|---|---|---|
| `GET` | `/api/v1/health` | Version and liveness; no authentication |
| `GET` | `/api/v1/projects` | List open projects |
| `POST` | `/api/v1/projects` | Create a project in an empty `root` |
| `POST` | `/api/v1/projects/open` | Open an existing project from `root` |
| `GET` | `/api/v1/projects/{id}/profiles` | List project profiles |
| `PUT` | `/api/v1/projects/{id}/profiles/{profileId}` | Create or replace a profile |
| `DELETE` | `/api/v1/projects/{id}/profiles/{profileId}` | Delete an unused non-default profile |
| `PATCH` | `/api/v1/projects/{id}/settings` | Set options such as `allowLocalhost` |
| `GET` | `/api/v1/projects/{id}/dashboard` | Get review/failure/job/schedule counts and recent runs |
| `GET` | `/api/v1/projects/{id}/target-sets` | List reusable target sets |
| `GET` | `/api/v1/projects/{id}/target-sets/{targetSetId}` | Get one target set and its ordered targets |
| `PUT` | `/api/v1/projects/{id}/target-sets/{targetSetId}` | Create or replace a target set |
| `DELETE` | `/api/v1/projects/{id}/target-sets/{targetSetId}` | Delete a target set not used by a schedule |
| `PATCH` | `/api/v1/projects/{id}/comparisons/{comparisonId}/review` | Accept, ignore, or reset one comparison |
| `POST` | `/api/v1/projects/{id}/comparisons/review-batch` | Review up to 500 comparisons |
| `POST` | `/api/v1/jobs` | Queue a job; returns HTTP 202 |
| `GET` | `/api/v1/jobs?projectId=…` | List recent jobs |
| `GET` | `/api/v1/jobs/{id}` | Get a job and its artifacts |
| `POST` | `/api/v1/jobs/{id}/cancel` | Cancel queued/running job |
| `POST` | `/api/v1/jobs/{id}/retry` | Queue a copy of a prior job |
| `GET` | `/api/v1/jobs/{id}/artifacts` | List job artifacts |
| `GET` | `/api/v1/jobs/{id}/comparisons` | List visual comparisons |
| `GET` | `/api/v1/jobs/{id}/events` | Server-sent events |
| `GET` | `/api/v1/artifacts/{id}/content` | Download artifact bytes |
| `PUT` | `/api/v1/artifacts/{id}/baseline` | Promote an artifact to baseline |
| `GET` | `/api/v1/baselines?projectId=…` | List project baselines |
| `DELETE` | `/api/v1/baselines` | Remove the baseline identified in the JSON body |
| `GET` | `/api/v1/comparisons?projectId=…` | Filter and page project comparisons |
| `GET` | `/api/v1/schedules?projectId=…` | List schedules |
| `POST` | `/api/v1/schedules` | Create/update a schedule |
| `DELETE` | `/api/v1/schedules/{id}` | Remove a schedule |

## Queue a capture

```json
{
  "projectId": "optional-active-project-default",
  "profileId": "default",
  "source": "automation",
  "urls": ["https://example.com"]
}
```

An inline `profile` object may override the saved profile for this job. URLs must be explicit HTTP(S) URLs and follow the active project's network policy.

Profiles may enable local portfolio rendering with:

```json
{
  "presentation": {
    "enabled": true,
    "scene": "aurora",
    "frame": "auto",
    "aspect": "16:9",
    "padding": "balanced",
    "shadow": "soft",
    "solidColor": "#0B1220"
  }
}
```

Supported scenes are `clean`, `aurora`, `sunset`, `midnight`, `graphite`, and `customSolid`. Frames are `auto`, `none`, `roundedCard`, `lightBrowser`, `darkBrowser`, `lightTablet`, `darkTablet`, `lightPhone`, and `darkPhone`; aspects are `auto`, `16:9`, `4:3`, and `square`. For viewport captures, automatic framing uses tablet hardware when Mobile mode is enabled and the shorter CSS dimension is at least 600 px, phone hardware for smaller mobile-mode viewports, and browser chrome otherwise. Each non-PDF artifact produces an `original` plus a `portfolio` variant when enabled.

To capture a reusable set, send `"targetSetId":"…"` instead of `urls`. The agent resolves enabled targets once and stores their IDs, labels, URLs, order, and set identity with the job. Later edits to the reusable set do not alter that run or its retries.

## Target sets

`PUT /api/v1/projects/{id}/target-sets/{targetSetId}` accepts:

```json
{
  "name": "Production storefront",
  "description": "Public purchase journey",
  "targets": [
    {"id":"home", "name":"Homepage", "url":"https://example.com", "enabled":true},
    {"id":"pricing", "name":"Pricing", "url":"https://example.com/pricing", "enabled":true}
  ]
}
```

Names and URLs are validated, duplicate normalized URLs are rejected, and a set referenced by a schedule cannot be deleted until that schedule is changed or removed.

## Comparison review

Project comparison results include `changed`, `matched`, `missing_baseline`, and `error`. Actionable results carry a review object with a `status` (`unreviewed`, `accepted`, or `ignored`), `note`, and integer `revision`.

```json
{
  "status": "accepted",
  "note": "Approved campaign update",
  "expectedRevision": 0
}
```

Accepting atomically advances the baseline to an immutable snapshot of the current artifact. `expectedRevision` prevents a stale client from overwriting a newer decision. Use `forceBaseline: true` only when deliberately replacing a baseline that changed since the comparison was created. Batch review accepts `{"items":[...]}` and returns separate `comparisons` and `failures` arrays.

`GET /api/v1/comparisons` supports `status`, `reviewStatus`, `targetSetId`, `engine`, `viewportId`, case-insensitive `search`, `limit` (1–500), and an opaque `cursor`. Pass the returned `nextCursor` to fetch the next page.

## Events

`GET /api/v1/jobs/{id}/events` returns `text/event-stream`. Each event has an integer `id` matching its persisted `sequence`, and a compact JSON `data` object. Send `Last-Event-ID` when reconnecting; the server replays later persisted events and continues live streaming until a terminal job event.

Terminal event types are `job_succeeded`, `job_partial`, `job_failed`, `job_cancelled`, and `job_interrupted`.

## Token lifecycle

`cybersnapper-cli api enable` generates a token when none exists. `cybersnapper-cli api token` invalidates the current token and prints a replacement. Tokens are only displayed at creation/regeneration time.
