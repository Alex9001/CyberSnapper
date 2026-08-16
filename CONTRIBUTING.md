# Contributing to CyberSnapper

Thanks for helping improve CyberSnapper. Bug fixes, focused features,
documentation improvements, and cross-platform compatibility work are welcome.

## Before you start

- Search the existing issues before opening a new one.
- Use the bug or feature issue form so the relevant context is included.
- Report security vulnerabilities privately according to
  [SECURITY.md](SECURITY.md), never in a public issue.
- Keep proposed features centered on CyberSnapper's primary job: creating
  portfolio-ready website screenshots.

## Development workflow

1. Create a focused branch from `master`.
2. Enable the project's git hooks and follow [the build guide](docs/BUILDING.md)
   to install prerequisites and run the worker and native builds:
   ```bash
   git config core.hooksPath .githooks
   ```
   The `commit-msg` hook rejects commits that carry AI-tool attribution
   trailers ("Generated with Codebuff" / "Co-Authored-By: Codebuff" /
   "Co-Authored-By: Copilot", etc.). See
   [docs/MAINTENANCE.md](docs/MAINTENANCE.md) for the full attribution
   policy, the history rewrite, and the immutable-release-tag rule.
3. Add or update tests for behavior changes and run the relevant checks.
4. For UI changes, attach before-and-after screenshots to the pull request. If
   documentation imagery changes, regenerate it with
   `npm run screenshots:docs` and review the resulting files.
5. Keep commits scoped and explain user-visible changes in the pull request.

The CI workflow is the final cross-platform check for Linux, macOS, and Windows.
Packaging and release details are also documented in
[the build guide](docs/BUILDING.md#packages).
