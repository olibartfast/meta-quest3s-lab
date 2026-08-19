# Plan: Meta XR Operator Adoption

Run commands from the repository root. A group is complete only when its result
is recorded in `validation.md`.

## Group 0 — Review the standalone bundle

1. Download Meta XR Operator Standalone 205.1 with a Meta developer account.
2. Complete the inventory and license table in `requirements.md`.
3. Pin the reviewed archive SHA-256 in
   `tools/xr_operator/prepare_xr_operator.sh`.
4. Stop if the Android arm64 API layer is absent or local staging is forbidden.

Status: **blocked on the login-gated archive and human license review**.

## Group 1 — Prove loader packaging

1. Build the repository-owned no-op OpenXR layer.
2. Stage its `.so` and manifest in the same layout as Meta's bundle.
3. Build app 01 with the Operator variant.
4. Verify the APK entries, INTERNET isolation, and
   `extractNativeLibs="true"`.
5. On a headset, confirm `QuestLabProbeLayer` negotiation in logcat.

Status: host half implemented; device log pending.

## Group 2 — Add the opt-in variant and provisioning

1. Add an explicit apps 01–04 allow-list and property-gated `operator` build
   type in the shared Gradle convention; reject apps 05–10 and other targets.
2. Add the shared Operator-only manifest.
3. Add a fail-closed bundle preparation script.
4. Extend `build_deploy.sh` and preserve the `.operator` package identity.

Status: implemented; real-bundle validation pending Group 0.

## Group 3 — Secure the device session

1. Set experimental and capture properties before launching apps 01–04.
2. Reject apps 05–10 before device access so the capture tool is never loaded.
3. Forward a selectable host port to fixed device port 8720.
4. Poll `/proc/net/tcp*` after launch and accept only loopback binds.
5. Remove the forward explicitly after the session.

Status: implemented; the allow-list enforces the sensitive-app boundary and
device validation remains pending.

## Group 4 — Guard CI and performance evidence

1. Lint every new shell script and repair the pre-existing continuation bug.
2. Build one Operator probe APK in CI.
3. Assert that representative apps 05, 09, and 10 reject Operator builds.
4. Assert debug/benchmark INTERNET isolation, allowing app 10's existing use.
5. Reject non-benchmark packages in the performance capture script.
6. Do not upload the Operator APK as a CI artifact.

Status: implemented; workflow execution pending.

## Group 5 — Document and pilot

1. Document provisioning, build, session, cleanup, privacy, security, and tool
   limitations in `docs/xr-operator.md`.
2. Update development and performance documentation.
3. Record that Operator cannot serve Milestone 9 checks 9–35 because app 09 is
   outside the allow-list, without changing their verdicts.

Status: implemented; headset measurements remain pending.

## Group 6 — Device acceptance

1. Install app 01 with the reviewed Meta layer and verify MCP connection,
   screenshot consent, controller injection, and cleanup.
2. Record the raw port-listener line and prove loopback-only binding.
3. Compare debug and Operator frame timings on the same app, but retain only
   the benchmark run as performance evidence.
4. Confirm host-side build and session rejection for apps 05, 09, and 10; do
   not install an Operator variant of any sensitive app.

Status: pending Group 0 and a connected headset.
