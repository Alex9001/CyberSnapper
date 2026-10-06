# Native screenshot provenance

The native application images were refreshed on 2026-10-06 from the button
appearance update based on source revision `05c3ae00c32fea73a859c661c1c485b6e7110b44`.

The screenshots show the actual Qt 6.11.2 application widgets, not an HTML or
painted interface recreation. Capture, Targets, History, Review, Dashboard and
Schedules use 1280 × 800 logical pixels with 2× native display scaling, saved as
2560 × 1600 PNG files. The Presentation dialog is 1560 × 906 pixels. No application
image was enlarged after capture.

The repository's `DocFixture` supplies the demonstration project and its existing
public CYBER BRAND website captures. Profile settings, completed-job history and
browser/service readiness are synthetic demonstration state. No personal project,
credentials or private customer data are included.

The constrained capture runtime does not permit local IPC sockets, so an external,
capture-only in-process transport adapter supplied the same fixture snapshots to
the current widgets. Production networking, persistence and application code were
not replaced by that adapter. End-to-end IPC/browser capture was not revalidated
in this environment; native appearance/layout and worker tests were run locally.

The original deterministic `npm run screenshots:docs` command remains available
for a runtime that supports its isolated IPC socket. No hosted workflow was run.
CI, Pages and release workflows are manual-only.
