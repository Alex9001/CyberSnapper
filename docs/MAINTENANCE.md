# Maintenance notes

This file records repository-wide decisions and procedures that do not fit a
single release. Read it before rewriting history, changing commit policies, or
touching release tags.

## Commit attribution policy

Commit messages in this repository must not carry AI-tool attribution
trailers — no "Generated with Codebuff", no "Co-Authored-By: Codebuff", and no
equivalent footers from other AI tools (Copilot, Claude, Codex, ChatGPT,
Gemini, Cursor, etc.). Commit messages consist of the subject line, an optional
body, and nothing else. Human `Co-Authored-By: Person <person@example.com>`
trailers remain allowed.

This is enforced in three layers:

1. **`.githooks/check-attribution.sh`** — the single source of truth for the
   blocklist of AI tools and the matching pattern. Add a new tool by editing
   the `ai_tools` variable here and nowhere else.
2. **`.githooks/commit-msg`** — the `commit-msg` hook delegates to the shared
   script and rejects offending commits locally. Enable it with
   `git config core.hooksPath .githooks` (see CONTRIBUTING.md).
3. **`.github/workflows/ci.yml`** — the `lint-commit-messages` job pipes every
   commit in a pull request or push through the shared script and fails with
   `::error::` annotations on any hit.

## History rewrite (August 2026)

On 2026-08-15 the history of `master` was rewritten to strip the Codebuff
attribution footer that had been appended to commit messages. Details for
future maintainers:

- 11 of 90 commits carried the footer; all 11 were rewritten with a message
  filter. Commit **trees are untouched** — `master^{tree}` is identical before
  and after, so no file content changed, only messages.
- The cleaned history was force-pushed to `origin/master` with
  `--force-with-lease`. Anyone with a clone of the old history must re-clone or
  reset.
- All local backups of the old history (the `backup/pre-attribution-strip`
  branch, the filter-branch `refs/original/refs/heads/master` ref, and the
  dangling objects) have been deliberately pruned. The old history exists
  nowhere anymore — except inside the `v2.2.2` tag line, which was kept (see
  below).

## Release tags are immutable

`v*` tags are published release anchors. Per [RELEASE.md](../RELEASE.md) and
[docs/PACKAGING.md](PACKAGING.md): **never move, replace, or delete a published
`v*` tag**. If tagged source needs a change, ship a new patch release from a
new commit and tag.

Consequence of the history rewrite: the `v2.2.2` tag still points at the
original (attributed) commit `7c27b52`, which is not reachable from `master`.
This is intentional and permanent:

- The tag backs the published v2.2.2 GitHub release; moving it would silently
  change what the release claims to be built from.
- The release packages and checksums were built and verified from that exact
  commit, and the rewrite did not change any file content.
- The footer inside that tagged commit is a historical record of what was
  shipped, not part of the active history.

The same reasoning applies to any future rewrite: even if a rewrite changes
commit messages, do not move existing `v*` tags to follow.
