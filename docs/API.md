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
| `POST` | `/api/v1/projects` | Create/open a project from `root` and optional `name` |
| `GET` | `/api/v1/projects/{id}/profiles` | List project profiles |
| `PUT` | `/api/v1/projects/{id}/profiles/{profileId}` | Create or replace a profile |
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

An inline `profile` object may override the saved profile for this job. URLs must be explicit public HTTP(S) URLs.

## Events

`GET /api/v1/jobs/{id}/events` returns `text/event-stream`. Each event has an integer `id` matching its persisted `sequence`, and a compact JSON `data` object. Send `Last-Event-ID` when reconnecting; the server replays later persisted events and continues live streaming until a terminal job event.

Terminal event types are `job_succeeded`, `job_partial`, `job_failed`, and `job_cancelled`.

## Token lifecycle

`cybersnapper api enable` generates a token when none exists. `cybersnapper api token` invalidates the current token and prints a replacement. Tokens are only displayed at creation/regeneration time.
