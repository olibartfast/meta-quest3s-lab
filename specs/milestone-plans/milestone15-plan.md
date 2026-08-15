# Milestone 15 — Performance and Production Quality

## Goal

Turn the completed native XR experiments into a measurable, repeatable, and
maintainable laboratory. Establish performance evidence before optimisation,
then improve only demonstrated bottlenecks while preserving the OpenXR frame
and lifecycle contracts established by earlier milestones.

This is a consolidation milestone. It does not introduce a new user-facing XR
feature or a new numbered application.

## Preconditions

Milestone 15 is blocked until Milestones 1–14 have passed their relevant
acceptance gates, matching the Definition of Done rather than only the
computer-vision ladder. Milestones 1, 2, and 3 are recorded as implemented with
Quest acceptance still pending, Milestones 6, 7, and 8 have applications on disk
but no recorded status, and the current Milestone 9 implementation still needs
Quest 3/3S permission, visual, lifecycle, long-run, private-fixture, and replay
evidence. Do not represent an Android build or host test as device acceptance.

Milestone 16 additionally blocks performance claims for the 3D perception path:
its concurrent capture gate passed, but per-device calibration, optical
synchronization, and Camera2-to-OpenXR time correlation remain explicit debt.
Milestones 13 and 14 must be revised against the branch selected by that
follow-up before their performance work can begin.

Record an explicit status for Milestones 6, 7, and 8 in `ROADMAP.md` before
Sequence 0 completes. An unrecorded status is a blocker to list, not an
omission to work around.

The instrumentation contract may be introduced before all earlier milestones
are complete when it is a small, independently validated change. Baselines,
optimisation decisions, final documentation, and Milestone 15 completion wait
for the full milestone chain.

## Scope

Create:

- `libs/perf_telemetry`, a small shared, OpenXR-independent frame/perception
  telemetry component, added to the `ROADMAP.md` target structure block;
- opt-in Perfetto/ATRACE spans for native work that needs timeline analysis;
- deterministic performance-result schema and capture instructions;
- a release-equivalent build variant in `scripts/build_deploy.sh`, because no
  optimised build path exists today;
- `docs/performance/` for committed derived results and run manifests;
- `docs/performance-validation.md`;
- `docs/architecture.md` with a native component and ownership diagram;
- application screenshots or short recordings that have been privacy reviewed.

Extend:

- `libs/xr_core`, `libs/vulkan_renderer`, camera/depth/perception/fusion
  components as they are introduced by Milestones 10–14;
- the Android CI workflow with host tests and sanitizer coverage where
  supported;
- build/deploy and documentation scripts only when they make evidence capture
  reproducible.

Remove:

- the duplicated `FrameCadenceLogger` in apps 06, 07, and 08, and the inline
  conversion timer in app 09, once `libs/perf_telemetry` covers them. The
  repository must not carry two independent frame-timing systems reporting
  different numbers for the same work.

Reuse:

- the existing bounded latest-frame queues, factory boundaries, host CMake
  tests, Android build matrix, and `build_deploy.sh` workflow;
- Meta Quest Developer Hub (MQDH), OVR Metrics Tool, Perfetto, RenderDoc Meta
  Fork, and logcat for target-device diagnosis.

Exclude:

- a speculative app `15-*`;
- a new engine, Unity, Unreal, or a production release pipeline;
- automatic performance tuning that silently changes visual quality;
- a global `new`/`delete` override in the Android app;
- an on-device inference feature before the host RF-DETR path is correct;
- temporal tracking or segmentation merely to make an overlay look stable.

## Measurement principles

1. Establish a controlled baseline before changing code. Record the Git SHA,
   app/package, debug or release-equivalent build settings, headset model,
   Horizon OS version, refresh rate, render configuration, room conditions,
   test scene, duration, active external tools, and the power and tether state
   (charging or on battery, USB-tethered or standalone). Charging and an active
   USB logcat session both change thermal and scheduling behavior, so they are
   part of run identity rather than incidental conditions.
2. Report time in milliseconds and percentile distributions, not only FPS.
   `xrWaitFrame` time is runtime pacing, not application CPU render cost; keep
   it separate from application update and render preparation.
3. Use one time domain for each record and label it. Preserve source timestamps
   and predicted-display time rather than converting or comparing them without
   documented mapping uncertainty.
4. Keep counters bounded and hot-path collection allocation-free. Aggregate on
   a worker or once-per-second reporting boundary; never emit per-frame logs in
   a production measurement run.
5. Treat the headset as the authority for display cadence, stale frames, GPU
   cost, frequency, temperature, and memory. App-local timers supplement but
   do not replace those metrics.
6. Change one controllable factor per experiment. Repeat a baseline after each
   accepted change so improvements and regressions remain attributable.
7. Store only derived metrics and privacy-reviewed diagnostics in Git, under
   `docs/performance/`. Aggregated per-window records and run manifests are
   derived data and are committed so a reader can reproduce every published
   summary. Camera, depth, and headset recordings remain app-private unless
   explicitly approved.
8. Measure the cost of measurement. Telemetry is not free, so every baseline
   that later comparisons use must be captured on a build carrying the same
   instrumentation as the builds it is compared against, with the enabled and
   disabled cost of that instrumentation known and recorded.
9. Choose the budget before collecting the evidence that is judged against it.
   Frame-delivery, queue, and memory targets are selected in Sequence 0; a
   target invented after seeing the results is a description, not a budget.

## Performance contract

Use explicit records rather than free-form performance logs. The final schema
may use JSON Lines or CSV, but every sample window must contain:

```text
schema: record schema version, incremented whenever a field changes meaning
run identity: Git SHA, app, build type (debug or release-equivalent),
              instrumentation enabled/disabled, device, Horizon OS,
              refresh rate, power state, tether state
window: start/end monotonic time, warm-up state, scenario identifier
OpenXR: session state, predicted display time, shouldRender count
CPU: update, camera/depth consume, conversion/upload, protocol, inference,
     correlation, fusion, scene build, Vulkan command-recording durations
GPU/runtime: VrApi App milliseconds, FPS/refresh, Stale, GPU%, CPU/GPU levels,
             render scale and foveation state when available
queues: current and high-water depth, overwrite/drop/reject counts
perception: capture-to-display age, RGB/depth delta, response age, invalid,
            expired, and confidence-rejected counts
memory: app/native memory and GPU-memory observations where the tool exposes
        them; allocation counter deltas for instrumented host code
thermal: temperature/performance state, frequency or refresh-rate changes, and
         any throttling event exposed by the device tools
```

Report count, mean, p50, p95, p99, and maximum for variable-duration work;
also report the test duration and sample count. Do not average away frame
deadline misses, queue growth, or thermal degradation.

## Atomic implementation sequence

### Sequence 0 — Close prerequisites and freeze a baseline

1. Complete the outstanding device acceptance evidence for each implemented
   milestone, beginning with Milestones 1–3 and 9, and record a status for
   Milestones 6–8.
2. Add a release-equivalent build variant. `scripts/build_deploy.sh` currently
   invokes only `assembleDebug`, and the gradle `release` type is unsigned and
   never built, so no optimised binary exists to measure. Provide the variant,
   its signing or local-testing arrangement, and its native optimisation
   settings, then build it in CI alongside the debug matrix.
3. Build every repository-owned app that exists and the legacy passthrough
   regression target from a clean checkout, in both variants.
4. Run all host tests and retain their commands and results.
5. Define one representative scenario per app: startup, steady state,
   interaction, pause/resume, and the relevant failure path.
6. Select the numeric budget each later gate is judged against: frame-delivery
   target for the requested refresh rate, acceptable stale-frame rate, p99
   ceilings for the instrumented CPU phases, queue high-water limits, and a
   memory high-water limit. Record the reasoning; revising a budget later
   requires stating what evidence justified the revision.
7. Use the same headset, room, refresh-rate request, power and tether state,
   and scenario for baseline and comparison runs where practical.
8. Warm up each application before recording its steady-state window.
9. Record an initial 15-minute run for each representative workload, without
   changing application behavior to make the metric look better. This run
   characterises the pre-instrumentation repository; the baseline that
   Sequences 2–8 compare against is re-frozen at the end of Sequence 1.

Gate: every comparison has a reproducible known-good baseline, a release-
equivalent build exists and is built in CI, each later gate has a numeric
budget chosen in advance, and incomplete earlier milestones are listed as
blockers rather than quietly omitted.

### Sequence 1 — Add bounded app-local telemetry

1. Create `libs/perf_telemetry` with portable `DurationSample`,
   `CounterSample`, `PercentileSummary`, and immutable `PerformanceSnapshot`
   types, without Android, OpenXR, or Vulkan handles. Nothing in the component
   may depend on `xr_core`; the dependency runs the other way.
2. Use `std::chrono::steady_clock` only for local CPU durations; preserve
   native source timestamps separately.
3. Add a fixed-capacity ring or fixed-bucket histogram with an explicit
   overflow counter. Do not allocate in `AddSample`.
4. Provide scoped timing only for measured blocks, with compile-time or
   runtime-disabled low overhead.
5. Publish snapshots at a bounded cadence, normally once per second.
6. Add rate-limited structured log output: category, metric name, unit, value,
   run identifier, and severity. Do not log image pixels, depth payloads, or
   raw detector outputs by default.
7. Instrument `XrSessionContext::PumpFrame` as separately labelled phases:
   runtime frame wait, event processing, frame updater, renderer submission,
   and frame end. Preserve the existing OpenXR call ordering.
8. Treat the `xr_core` change as touching all nine applications. Milestones 4
   and 5 already hold Quest acceptance, so re-run their launch/exit, lifecycle,
   and visual regression checks after the shared frame loop is modified, and
   re-run every app's build.
9. Replace the duplicated per-app timing code with the shared component:
   `FrameCadenceLogger` in apps 06, 07, and 08, and the inline YUV-conversion
   timer in app 09. Milestone 6 requires frame timing to be measured and
   logged, so the reported values must remain available; only their
   implementation is consolidated. Delete the copies rather than leaving them
   alongside the new component.
10. Add opt-in ATRACE/Perfetto spans around CPU phases that require scheduling
    correlation. The disabled build must not retain per-frame strings or heap
    allocations.
11. Add host tests for empty windows, percentile boundaries, ring overflow,
    snapshot immutability, rate limiting, and disabled instrumentation.
12. Measure the instrumentation itself: run one representative scenario per app
    with telemetry compiled out, compiled in but disabled, and fully enabled.
    Record the frame-delivery and CPU-phase difference between the three.
13. Re-freeze the Sequence 0 baselines on the instrumented release-equivalent
    build. Sequences 2–8 compare against these, not against the
    pre-instrumentation numbers.

Gate: host tests demonstrate bounded collection; headset logs show one
well-formed snapshot per reporting interval and no per-frame logging flood;
measured instrumentation overhead is within the Sequence 0 budget; apps 04 and
05 still pass their existing acceptance checks; and no per-app duplicate timing
system remains.

### Sequence 2 — Instrument data pipelines without changing their policy

1. Add stage timers and counts to `camera_source`: callback arrival, owned-copy,
   YUV conversion, latest-frame overwrite, and Vulkan upload eligibility.
2. Add equivalent stages to the future `depth_source`: acquire, metadata copy,
   owned snapshot/reduction, invalid-sample rejection, and queue replacement.
3. Instrument the Milestone 11 protocol: admission, serialization, send queue,
   transport, server queue, preprocessing, inference, postprocessing, response,
   frame-ID correlation, and stale/unknown rejection.
4. Instrument Milestones 13–14: timestamp pairing, reprojection, sample
   selection, clustering, geometry construction, render publication, and age
   at predicted display time.
5. Count each deliberate loss separately: source overwrite, submission
   throttling, queue full, malformed message, expired record, unmatched pair,
   insufficient depth, and low-confidence fusion.
6. Report queue current depth and high-water mark, never an unbounded history.
7. Verify that enabling all counters preserves bounded queues and does not
   perform source capture, depth readback, transport, inference, or fusion on
   the render thread.

Gate: every perception result can be explained by a bounded, frame-correlated
metric trail from capture through displayed or rejected output.

### Sequence 3 — Measure frame delivery and GPU cost on Quest

1. Select the refresh rate actually requested by the test app; use the matching
   72 Hz (13.9 ms), 80 Hz (12.5 ms), 90 Hz (11.1 ms), or 120 Hz (8.3 ms) frame
   budget.
2. Record VrApi logcat statistics, including `FPS`, `Stale`, `GPU%`, and `App`
   GPU time, alongside the app-local snapshot.
3. Use the `App` time against the active frame budget; do not infer GPU
   headroom from `GPU%` when stale frames occur.
4. Use the OVR Metrics Tool or MQDH Performance Analyzer for in-headset and
   timeline observations. Disable casting during a precision run and restrict
   logcat filters to avoid tool-induced overhead.
5. Take short, targeted Perfetto traces with XR Runtime Metrics, GPU metrics,
   and application ATRACE spans when CPU scheduling or contention is suspected.
6. Use RenderDoc Meta Fork only to localize graphics bottlenecks; report its
   overhead and do not treat an instrumented capture as the production timing.
7. If exact GPU cost is required, follow Meta's temporary TimeWarp-disabled
   diagnostic procedure on a development headset. Record the command, duration,
   device state, and restoration; never ship or automate that setting.
8. Label a missed frame as an external runtime observation. Do not claim that
   application wall-clock cadence alone proves a missed display deadline.

Gate: a performance report identifies the active refresh budget, `App` GPU
time, stale-frame count, and whether the limiting evidence is CPU, GPU, or
inconclusive.

### Sequence 4 — Diagnose and improve one bottleneck at a time

1. Rank candidates by p95/p99 cost, stale frames, queue pressure, memory
   high-water mark, and user-visible impact.
2. Isolate CPU versus GPU pressure with a no-scene/no-render diagnostic build
   or an equivalent feature flag that leaves lifecycle and input paths intact.
3. For a likely GPU bottleneck, change render scale or foveation only as a
   documented measurement variable. Restore the normal configuration before
   comparing feature quality.
4. For a CPU bottleneck, use a short Perfetto trace to locate scheduling,
   synchronization, allocation, conversion, or protocol work before editing.
5. Optimise the single highest-cost confirmed path, preserving all queue bounds,
   frame IDs, timestamp semantics, confidence checks, and error states.
6. Re-run the original baseline scenario plus affected correctness tests.
7. Keep a change only when it improves the chosen primary metric without a
   regression in frame delivery, visual correctness, lifecycle, memory, or
   perception accuracy. Otherwise revert the experiment.

Gate: every retained optimisation has before/after evidence, an identified
cause, and an explicit non-regression result.

### Sequence 5 — Evaluate perception operating points

1. Freeze the correct Milestone 10 host RF-DETR model contract and reference
   detections before comparing variants.
2. Define target end-to-end age, source-frame submission rate, minimum 2D
   detection quality, and maximum acceptable queue growth for a named scenario.
3. Compare model size, input resolution, confidence threshold, quantization,
   and C++ runtime backend in a table containing quality, preprocessing,
   inference, postprocessing, transport, memory, and end-to-end age.
4. Treat quantization as a separate model artifact with its own checksum,
   manifest, reference output, and tolerances.
5. Select the sustainable host-backed operating point from measured data, not
   the single fastest benchmark result.
6. Only then benchmark an on-device path in isolation. It must preserve the
   model contract and report device thermal and frame-delivery impact; it is
   not a prerequisite for completion.
7. Add temporal association only with immutable raw detections still available
   for rendering and diagnostics. Mark predicted, associated, and raw output
   distinctly.
8. Consider segmentation-assisted fusion only if the Milestone 14 measurement
   demonstrates that box-only depth samples fail its documented geometry
   tolerances. Compare it against the same recorded fixtures.

Gate: the selected perception configuration has a reproducible accuracy,
latency, memory, thermal, and queue-pressure rationale.

### Sequence 6 — Harden resource ownership and lifecycle

1. Audit every OpenXR, Vulkan, JNI, Android Camera2, thread, file, socket, and
   swapchain resource for single ownership, partial-initialization cleanup, and
   session/activity ordering.
2. Introduce narrow RAII wrappers only where they remove a demonstrated cleanup
   hazard; preserve wrapper-free data contracts and avoid a broad rewrite.
3. Ensure Android pause/resume and OpenXR stopping destroy or stop resources in
   reverse dependency order, then re-create them only after valid session state
   returns.
4. Verify workers have cancellation, bounded waits, and join behavior that
   cannot block the render loop indefinitely.
5. Replace ambiguous logs with structured severity and category fields. Keep
   transition/error messages while rate-limiting steady-state diagnostics.
6. Run repeated launch/exit, pause/resume, permission denial, server absence,
   reconnect, unavailable-depth, and malformed-data tests appropriate to every
   completed app.
7. Run host sanitizers for libraries that build outside Android. Treat a
   sanitizer report as a defect to resolve or document with a minimal,
   justified suppression.

Gate: repeated lifecycle and fault-injection runs leave no validation errors,
leaked handles, unbounded queues, deadlocked workers, or sanitizer findings.

### Sequence 7 — Make quality checks repeatable

1. Keep the existing Android build matrix for every application, extend it as
   apps 10–14 are added, and cover the release-equivalent variant from
   Sequence 0.
2. Add each portable component's host unit tests to CI, including
   `libs/perf_telemetry`; use behavior-focused test names and no
   headset-required dependencies. Standardise how the tests are configured
   while doing so: the workflow currently mixes `-S libs/<lib>
   -DBUILD_TESTING=ON` with `-S libs/<lib>/tests`, and new components should
   not add a third convention.
3. Add an opt-in host sanitizer configuration for supported CMake libraries.
4. Extend the existing lint coverage rather than introducing it. CI already
   runs `shellcheck` over the four scripts; the missing piece is a C++
   formatting or lint configuration that matches the current style. Choose and
   document the tool configuration first, and do not perform an unrelated
   repository-wide reformat.
5. Add fixture integrity checksums, schema validation, and explicit privacy
   review metadata for any approved replay asset.
6. Provide a documented command to produce a performance bundle locally without
   automatically collecting camera or depth content.
7. Record CI limitations plainly: Android builds validate compilation, whereas
   GPU timing, permissions, OpenXR runtime behavior, thermal behavior, and
   camera/depth acceptance remain headset tests.

Gate: a clean checkout can build the full supported matrix, run all portable
tests, and validate all checked-in fixture metadata without a headset.

### Sequence 8 — Thermal, long-run, and final evidence

1. Run the selected end-to-end spatial-overlay configuration for at least
   30 minutes after warm-up, then repeat after a headset reboot.
2. Fix and record the power and tether state for every long run. Prefer a
   standalone run on battery, since charging changes the thermal profile and a
   USB logcat session is itself load. If tethered collection is unavoidable,
   label those runs separately and do not compare them against untethered ones.
3. Monitor frame delivery, `App` GPU time, CPU/GPU levels, refresh-rate changes,
   queue high-water marks, memory, perception age, and thermal observations
   throughout the run.
4. Repeat the run at a representative cool state and a warmed device state;
   label the conditions rather than averaging them together.
5. Exercise normal interaction, slow head motion, loss/recovery of permission
   or service connectivity where relevant, and a clean shutdown.
6. Capture one screenshot or short recording for each completed application.
   Review every artifact for private surroundings before committing a selected
   sample or linking it from documentation.
7. Commit the derived per-window records and run manifests under
   `docs/performance/` so every published summary can be recomputed from
   checked-in data. Where a summary rests on material that cannot be committed,
   say so explicitly instead of citing a location the reader cannot reach.
8. Write `docs/performance-validation.md` with methods, result locations,
   summaries, failures, selected defaults, known limitations, and rejected
   experiments.
9. Write `docs/architecture.md` with module boundaries, factories, ownership,
   worker threads, main data flow, platform-specific adapters, and the OpenXR
   lifecycle relation.

Gate: the long-run evidence meets the operating budget selected in Sequence 0.
If it does not, the milestone stays incomplete and the exact remaining
limitation is recorded. Documenting a shortfall is how it is reported, not a
way to satisfy the gate.

## Target validation matrix

| Area | Minimum evidence |
| --- | --- |
| Frame delivery | Active refresh rate, `FPS`, `Stale`, `App` GPU time, app CPU phase percentiles |
| Rendering | Debug/release-equivalent comparison, GPU bottleneck trace when indicated, visual non-regression |
| Camera/depth | Capture/acquire age, queue high-water marks, drop/reject causes, lifecycle recovery |
| Perception | Per-stage latency, result age at display, accuracy versus pinned fixtures, overload/reconnect behavior |
| Memory | Long-run high-water observations, bounded queue proof, host sanitizer results |
| Thermal | Cool/warm 30-minute runs, recorded power and tether state, frequency or refresh-rate changes, degradation trend |
| Reliability | Launch/exit, pause/resume, permissions, unavailable data/service, malformed inputs, shutdown |
| Reproducibility | Clean CI build/test matrix in both build variants, tool versions, run manifest, committed derived results, privacy-reviewed artifacts |
| Instrumentation | Enabled-versus-disabled overhead, budget compliance, single frame-timing implementation |

## Official references

- [Basic Optimization Workflow for Meta Quest Apps](https://developers.meta.com/horizon/documentation/native/android/po-perf-opt-mobile/)
- [Accurately Measure an App’s Per-Frame GPU Cost](https://developers.meta.com/horizon/documentation/native/android/po-per-frame-gpu/)
- [Missed Frames and Frame Recovery](https://developers.meta.com/horizon/documentation/native/android/os-missed-frames/)
- [MQDH Performance Analyzer and Metrics](https://developers.meta.com/horizon/documentation/native/android/ts-mqdh-logs-metrics/)

## Definition of Done

- all earlier milestones, 1 through 14, have their required host and Quest
  acceptance evidence and a recorded status;
- every supported application builds in CI in both the debug and the
  release-equivalent variant, and every portable library has its relevant host
  tests;
- frame delivery, CPU phases, GPU `App` time, stale frames, queues, perception
  age, memory, and thermal behavior are measured with the documented schema,
  on the release-equivalent build, against budgets chosen in Sequence 0;
- instrumentation is bounded, opt-in where appropriate, allocation-free in
  the hot path, does not alter OpenXR frame ordering, and has a measured
  enabled-versus-disabled cost within its budget;
- `libs/perf_telemetry` is the single frame-timing implementation, with the
  former per-app copies removed and their reported values preserved;
- each retained optimisation has before/after evidence and correctness
  non-regression results;
- the selected perception operating point is justified with reproducible
  accuracy, latency, queue, memory, and thermal data;
- lifecycle, fault-injection, and sanitizer validation show no unresolved
  cleanup, boundedness, or memory-safety defect;
- `docs/performance-validation.md` and `docs/architecture.md` describe the
  final evidence, limitations, component boundaries, and ownership;
- screenshots or recordings exist for completed applications and have received
  privacy review;
- the repository remains native C++/OpenXR/Vulkan-first, with no new engine or
  unapproved external runtime dependency.
