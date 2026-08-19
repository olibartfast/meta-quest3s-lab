# Performance Evidence

This directory stores schemas, run manifests, and privacy-reviewed derived
results. Do not commit raw camera frames, depth payloads, room scans, unreviewed
screenshots, or unreviewed headset recordings.

Each committed run belongs in a directory named
`YYYYMMDDTHHMMSSZ-app-scenario/` and contains at minimum:

- `run-manifest.json` with the controlled test conditions and budgets;
- `records.jsonl` containing schema-versioned performance windows;
- a short `summary.md` that distinguishes observations from conclusions;
- privacy-review metadata for any media linked by the summary.

The local capture script writes under ignored `build/performance/` by default.
Copy only reviewed, derived evidence here. `runtime_metrics` values left as
`null` explicitly mean that an external Meta device tool was not correlated;
they must not be interpreted as zero.

Meta XR Operator is not a performance-capture path. Its variant is unavailable
for apps 05–10 because their composited view can contain the physical room. If
a synthetic app 01–04 capture is saved during development, keep it under
ignored `build/operator/`; do not promote it into this evidence tree.
