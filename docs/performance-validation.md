# Performance Validation

## Current status

The bounded instrumentation and repeatable build/capture foundation is
implemented. This document is not device acceptance evidence. Milestones
1–3, 6–9 still have recorded Quest acceptance work, and milestones 10–14 are
not implemented; therefore end-to-end baseline, GPU, thermal, perception, and
long-run gates remain pending.

Implemented now:

- fixed-capacity, OpenXR-independent duration telemetry with immutable
  snapshots and nearest-rank p50/p95/p99 summaries;
- one rate-limited structured XR snapshot per second for runtime wait, event
  processing, update, renderer submission, and frame end;
- app 09 YUV conversion, capture-to-scene publication age, queue depth, and
  overwrite counters;
- optional native ATRACE spans with strings and objects compiled out when
  disabled;
- an optimized, debug-key-signed `benchmark` build alongside `debug`;
- host unit and sanitizer coverage, a JSON record schema, and a privacy-safe
  bundle capture command.

No retained optimization is claimed yet. Baselines and budgets must be frozen
on a headset before changing rendering or perception policy.

## Build and host validation

Run the portable tests:

```bash
cmake -S libs/perf_telemetry -B build/perf-telemetry-tests -DBUILD_TESTING=ON
cmake --build build/perf-telemetry-tests --parallel
ctest --test-dir build/perf-telemetry-tests --output-on-failure
```

Run the supported sanitizer configuration:

```bash
cmake \
  -S libs/perf_telemetry \
  -B build/perf-telemetry-sanitized \
  -DBUILD_TESTING=ON \
  -DQUESTLAB_ENABLE_SANITIZERS=ON
cmake --build build/perf-telemetry-sanitized --parallel
ctest --test-dir build/perf-telemetry-sanitized --output-on-failure
```

Build an installable release-equivalent APK. `benchmark` uses the Gradle
release configuration, produces native `RelWithDebInfo` code, disables
debuggability, and uses the local debug signing key only so it can be installed
for laboratory measurements:

```bash
./scripts/build_deploy.sh \
  --app 09-quest-camera \
  --variant benchmark \
  --build-only
```

Do not compare a debug result with a benchmark result. Do not enable Vulkan
validation during a precision run.

An `operator` result is never performance evidence. That build is debuggable,
requests INTERNET, extracts native libraries, and runs an in-process MCP server
with observation and input-injection tooling. Use it to drive or assist an
acceptance scenario, then repeat every timing measurement with the `benchmark`
variant. `capture_performance_bundle.sh` rejects any package that does not end
in `.benchmark`.

## App-local records

Filter structured snapshots without enabling a per-frame log flood:

```bash
adb logcat -s QuestCamera:V OpenXR:V '*:S' | sed -n 's/^.*PERF //p'
```

Every duration array follows the explicit `duration_fields` order:

```text
[count, mean, p50, p95, p99, max, overflow]
```

All duration values are milliseconds. Window boundaries and camera arrival
timestamps are `steady_clock` monotonic nanoseconds. OpenXR predicted display
time is preserved as an opaque native `XrTime`; no undocumented time-domain
conversion is performed. `runtime_frame_wait` is runtime pacing and must not
be added to application update/render CPU cost.

The fixed ring retains the newest 256 XR phase samples (128 camera samples)
and increments `overflow` for every replaced sample. An overflow is evidence
that the report cadence or capacity was insufficient, not a value to ignore.
The first five seconds are marked `warmup`.

## Perfetto / ATRACE

Build and install with trace spans enabled:

```bash
./scripts/build_deploy.sh \
  --app 09-quest-camera \
  --variant benchmark \
  --perfetto-tracing
```

The emitted sections are
`questlab.xr.runtime_frame_wait`, `questlab.xr.event_processing`,
`questlab.xr.frame_update`, `questlab.xr.renderer_submission`, and
`questlab.xr.frame_end`. Capture only a short targeted Perfetto trace when
scheduling evidence is needed. A disabled build removes the span calls and
their string literals at preprocessing time.

## Reproducible local bundle

Verify exactly one authorized headset, choose the numeric budget before the
run, warm up the app, then capture derived logs:

```bash
adb devices -l
./scripts/capture_performance_bundle.sh \
  --app 09-quest-camera \
  --scenario steady-state \
  --refresh-rate-hz 72 \
  --duration-seconds 900
```

The command builds the traced benchmark APK, installs and launches it, and
writes under `build/performance/`. It records device/build/power/tether
identity, battery state, filtered logs, and enriched JSON Lines. It does not
collect screenshots, recordings, camera pixels, or depth payloads. Runtime
fields that require MQDH/OVR Metrics remain explicit `null` values until a
correlated external capture supplies them.

Before committing derived results, validate every JSON line against
`docs/performance/performance-record.schema.json`, redact the device serial,
and copy the reviewed bundle under `docs/performance/`.

## Device measurement protocol

For each app, record startup, steady state, interaction, pause/resume, and its
relevant failure path. Use the same headset, room, refresh rate, power/tether
state, render settings, scenario, and external tools for baseline and
comparison. Disable casting during precision runs.

At 72, 90, and 120 Hz, the nominal budgets are respectively 13.9, 11.1, and
8.3 milliseconds. Record the actual requested rate. Correlate app records with
Meta runtime `FPS`, `Stale`, `App` GPU milliseconds, GPU percentage, and
CPU/GPU levels. Treat `App` GPU time against the active budget as the primary
GPU-cost evidence; do not infer headroom from GPU percentage while frames are
stale.

Run a 15-minute controlled baseline only after earlier milestone acceptance
and numeric budgets are recorded. Final evidence requires two 30-minute runs
after warm-up, including cool and warmed headset conditions and a repeat after
reboot. Record memory high-water observations, frequency/refresh changes,
thermal degradation, lifecycle recovery, and clean shutdown. Device evidence
must identify whether a limit is CPU, GPU, or inconclusive.

## Evidence matrix

| Area | Repository evidence | Device evidence |
| --- | --- | --- |
| Bounded CPU telemetry | Host tests pass | Pending headset log review |
| Frame delivery/GPU | Schema and correlation instructions | Pending MQDH/OVR Metrics capture |
| Camera pipeline | Conversion/age/queue counters compile | Pending permission, visual, and long-run run |
| Memory safety | ASan/UBSan configuration | Pending lifecycle fault runs |
| Thermal | Run manifest fields | Pending cool/warm 30-minute runs |
| Screenshots/recordings | Privacy rules documented | Pending capture and human privacy review |
| Perception/depth/fusion | Future schema fields reserved | Blocked by milestones 10–14 |

## Official references

- [Meta Native & OpenXR documentation index](https://developers.meta.com/horizon/llmstxt/documentation/native/llms.txt/)
- [Basic Optimization Workflow for Meta Quest Apps](https://developers.meta.com/horizon/documentation/native/android/po-perf-opt-mobile/)
- [Accurately Measure an App's Per-Frame GPU Cost](https://developers.meta.com/horizon/documentation/native/android/po-per-frame-gpu/)
- [Missed Frames and Frame Recovery](https://developers.meta.com/horizon/documentation/native/android/os-missed-frames/)
- [MQDH Performance Analyzer and Metrics](https://developers.meta.com/horizon/documentation/native/android/ts-mqdh-logs-metrics/)
- [Android NDK tracing API](https://developer.android.com/ndk/reference/group/tracing)
- [Khronos OpenXR 1.1 frame synchronization and submission](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html#frame-synchronization)
