# Meta XR Operator

## Status and scope

Meta XR Operator is an experimental OpenXR API layer from Meta XR SDK v205. It
runs an MCP server inside an OpenXR application and exposes session, tracking,
frame, controller, spatial, and image-capture tools to an MCP-capable agent.
This repository integrates the standalone Android layer as an opt-in
`operator` build variant for synthetic apps 01–04 only. It does not introduce
Unity, Unreal, or a second XR runtime.

The real Meta bundle is not provisioned yet. It requires a Meta developer login,
and its archive checksum and license must be reviewed before use. Host-side
packaging is validated with the repository-owned no-op layer; runtime behavior
remains pending a connected Quest 3/3S and the reviewed Meta bundle.

## Loader and process model

The Android activity passes its VM and activity to
`xrInitializeLoaderKHR` before creating the OpenXR instance. The Khronos loader
then discovers implicit API-layer manifests from this APK asset path:

```text
assets/openxr/1/api_layers/implicit.d/
```

The manifest's relative `library_path` resolves against Android's native
library directory. The `operator` variant therefore sets legacy JNI packaging,
which produces `android:extractNativeLibs="true"`; the layer must exist as a
real extracted file for the loader to find it. The layer forwards OpenXR calls
to the next layer or runtime and hosts its MCP server on device port 8720.

Apps 01–04 are an explicit allow-list. The variant is enabled only when
`questXrOperator=true`, which `build_deploy.sh --variant operator` supplies.
Debug, release, and benchmark source sets do not receive the shared Operator
manifest, assets, or native library. Apps 05–10 render passthrough or hold
camera permissions and are excluded as a strict privacy boundary. App 16
creates no OpenXR instance, and the legacy baseline is a separate Gradle build,
so neither supports the variant.

## Provision the standalone bundle

Download Meta XR Operator Standalone 205.1 from Meta Developer Center. Inspect
the archive and complete the bundle-inventory gate in
`specs/2026-08-19-xr-operator-adoption/requirements.md` before changing the
empty checksum pin in `tools/xr_operator/prepare_xr_operator.sh`.

After review:

```bash
./tools/xr_operator/prepare_xr_operator.sh \
  --archive /path/to/meta-xr-operator-standalone.zip
```

The script fails closed until the archive SHA-256 is pinned. It accepts only
the documented Android layer names, validates the API-layer name and
`library_path`, and stages the reviewed files under ignored
`build/dependencies/meta-xr-operator-205.1/`. Never commit the Meta `.so` or
layer JSON.

To validate packaging without Meta's binary:

```bash
./tools/xr_operator/build_probe_layer.sh
ORG_GRADLE_PROJECT_questXrOperatorRoot=build/dependencies/questlab-probe-layer \
  ./scripts/build_deploy.sh \
    --app 01-openxr-bootstrap \
    --variant operator \
    --build-only
```

The probe verifies the same JNI and asset layout and logs
`QuestLabProbeLayer` when the OpenXR loader negotiates with it. It does not
implement any Operator tools.

## Build, deploy, and connect

Verify one authorized headset, then build and launch:

```bash
adb devices -l
./scripts/build_deploy.sh \
  --app 01-openxr-bootstrap \
  --variant operator
```

The deploy script installs the `.operator` package, enables Horizon OS
experimental features, applies the capture policy, creates
`tcp:8720 -> device tcp:8720`, launches the app, waits for the listener, and
rejects wildcard or non-loopback device binds. Experimental and capture
properties reset on reboot, so this happens on every deployment.

The committed `.mcp.json` selects the advanced direct-SSE route:

```text
http://localhost:8720/sse
```

Claude Code asks for approval before using a project-scoped MCP server. Meta's
standalone MCP proxy is the preferred alternative when automatic reconnect or
an offline tool list matters. The direct endpoint keeps Linux development
independent of the Windows/macOS proxy binary.

For an already-running app, reconnect and verify with:

```bash
./scripts/xr_operator_session.sh --app 01-openxr-bootstrap
```

If host port 8720 is occupied, choose a different host-side port while keeping
the device side fixed:

```bash
./scripts/xr_operator_session.sh \
  --app 01-openxr-bootstrap \
  --host-port 8721
```

Point the MCP client at `http://localhost:8721/sse` for that session. Remove a
forward when finished:

```bash
./scripts/xr_operator_session.sh --stop
```

## Privacy and security boundaries

Operator is unavailable for apps 05–10. They render passthrough or hold a
camera permission, and Meta states that Operator captures combine
physical-environment and virtual content. Meta documents no per-app switch
that removes the MCP capture tool, so the Gradle convention, build wrapper, and
session helper all reject these apps. No Operator layer, server, manifest, or
capture tool is packaged into them. Revisit this boundary only if Meta adds an
auditable tool-disable control or the repository adopts a separately reviewed
filtering proxy.

Any saved synthetic capture belongs under ignored `build/operator/` and is not
performance or acceptance evidence. No Operator capture is promoted into
`docs/performance/`.

The MCP server exposes observation and input injection without an application
authentication layer. The session script reads `/proc/net/tcp*` after launch
and accepts only IPv4 or IPv6 loopback. If it reports a wildcard or other bind,
stop the application and do not use Operator on that network.

The `.operator` suffix creates a distinct Android package. Runtime permissions
and in-headset consent do not carry over from debug or benchmark installs.

## Evidence limits

Operator can drive controller buttons, triggers, grips, thumbsticks, and poses;
it can query OpenXR session/tracking/frame state and spatial entities. On a
physical headset it cannot override head pose, which remains tied to the
device. It also does not replace a person for:

- camera orientation, aspect, color, subtle visual defects, or motion quality;
- per-finger hand-tracking behavior;
- audio or thermal judgment;
- timing-sensitive behavior and moving-target quality;
- privacy approval of camera pixels;
- numeric performance, memory, queue, or integrity evidence from existing
  logs and tools.

Operator is barred from performance evidence. Use it to reach a state or drive
an interaction, then measure with the repository's existing log, ADB, and
benchmark workflow.

## Milestone 9 boundary

App 09 is outside the strict allow-list, so Operator cannot drive or assist
checks 9–35. Milestone 9 remains a human, ADB, logcat, and `PERF` workflow.
This is deliberate: no Operator layer or capture capability is present in its
APK, and no existing Milestone 9 verdict changes.

| Checks | Classification | Reason |
|---|---|---|
| 9–35 | Operator cannot serve | App 09 cannot be built or launched with Operator; use the existing human and device-tooling evidence path. |

## Official references

- [Meta XR Operator overview](https://developers.meta.com/horizon/llmstxt/documentation/unity/meta-xr-operator/index.md)
- [Connecting AI agents to Meta XR Operator standalone](https://developers.meta.com/horizon/llmstxt/documentation/unity/meta-xr-operator/connecting-ai-agents.md)
- [Using Meta XR Operator with Meta Quest](https://developers.meta.com/horizon/llmstxt/documentation/unity/meta-xr-operator/quest.md)
- [Khronos OpenXR loader design](https://registry.khronos.org/OpenXR/specs/1.1/loader.html)
