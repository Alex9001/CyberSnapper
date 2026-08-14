# Project format

A CyberSnapper 2.x project is a portable folder:

```text
project.cybersnapper.json
captures/YYYY-MM-DD/JOB_ID/*
baselines/
reports/
.cybersnapper/
  project.sqlite
  project.lock
  previews/
  diffs/JOB_ID/*
  logs/
  tmp/
```

`project.cybersnapper.json` contains a stable project UUID, display name, schema version, database path, capture root, and creation timestamp. It contains no credentials.

The SQLite schema records profiles, jobs, ordered job events, artifacts, baseline pointers, comparison results, and schedules. Artifact metadata includes the original/final URL, browser engine, viewport, mode, format, relative path, dimensions, SHA-256 hash, status, and timestamp.

Capture files and diffs use relative paths so a closed project can be moved. Open projects are protected by `.cybersnapper/project.lock`; do not move a project while the agent has it open.

Back up the complete folder. The SQLite `-wal` and `-shm` files may exist while a project is open, so close the agent before making a filesystem-level archive.
