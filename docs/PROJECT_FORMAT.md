# Project format

A CyberSnapper 2.x project is a portable folder:

```text
project.cybersnapper.json
captures/YYYY-MM-DD/JOB_ID/*
baselines/
.cybersnapper/
  project.sqlite
  project.lock
  previews/
  diffs/JOB_ID/*
  logs/
  tmp/
```

`project.cybersnapper.json` contains a stable project UUID, display name, schema version, database path, capture root, creation timestamp, and the per-project localhost policy. It contains no credentials.

SQLite schema v4 records profiles, named target sets and their ordered targets, jobs, ordered job events, artifacts, immutable baseline pointers, comparison results, review decisions, and schedules. Artifact metadata includes normalized target and target-set provenance alongside the original/final URL, browser engine, viewport, mode, format, relative path, dimensions, SHA-256 hash, status, and timestamp.

Every submitted target set is expanded into a snapshot stored with the job. Editing or deleting the reusable set later therefore cannot change the meaning of an existing run or retry. Comparison rows record analyzed dimensions and scale, mismatched/analyzed pixel counts, algorithm version, changed-region metadata, and the exact baseline path used. Review decisions (`unreviewed`, `accepted`, or `ignored`) have notes and monotonically increasing revisions so concurrent clients cannot silently overwrite one another.

Baseline files use capture-versioned names and are immutable. Accepting a comparison moves the active pointer atomically while old comparisons retain their original evidence; removing the active pointer does not delete a historical snapshot.

CyberSnapper 2.x upgrades its own earlier project databases in place with ordered transactional migrations. It does not import 1.x script state.

Capture files and diffs use relative paths so a closed project can be moved. Open projects are protected by `.cybersnapper/project.lock`; do not move a project while the agent has it open.

Back up the complete folder. The SQLite `-wal` and `-shm` files may exist while a project is open, so close the agent before making a filesystem-level archive.
