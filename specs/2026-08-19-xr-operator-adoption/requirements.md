# Requirements: Meta XR Operator Adoption

## Objective

Add Meta XR Operator to repository-owned native OpenXR apps as an explicit
development-only build variant. The tool may drive and observe acceptance
scenarios, but it must not weaken privacy rules, contaminate performance
evidence, or become a production dependency.

## Version and build path

- Target devices: Meta Quest 3 and Quest 3S.
- Host: Linux with ADB; no Meta XR Simulator dependency.
- Application path: native C++17, OpenXR loader for Android 1.1.51.
- Operator path: Meta XR Operator Standalone 205.1 / Meta XR SDK v205.
- Transport: direct SSE on `http://localhost:8720/sse` through ADB forward.
- Status: experimental; never enable in debug, release, or benchmark builds.

## User decisions

1. Exclude Operator entirely from apps 05–10. These applications render
   passthrough or hold camera permissions, so a composited capture can expose
   the physical room. A consent convention is not a technical capture block.
2. Deliver both a reusable Operator workflow and an honest classification of
   how it can assist the open Milestone 9 headset checks.

## Functional requirements

1. Apps 01–04 have an opt-in `operator` build type with a distinct
   `.operator` application id and APK path.
2. Apps 05–10, app 16, and `XrPassthrough` reject `--variant operator`.
3. The layer `.so` is packaged under `lib/arm64-v8a/`; its JSON is packaged
   under `assets/openxr/1/api_layers/implicit.d/`.
4. The Operator manifest applies only to apps 01–04. Debug and benchmark
   manifests remain unchanged; app 10 retains its pre-existing INTERNET
   permission, which is unrelated to Operator.
5. Native libraries are extracted only for the Operator variant so the OpenXR
   loader can resolve the manifest's relative `library_path`.
6. Deployment sets the non-persistent Horizon OS property before app launch,
   forwards the fixed device port, and verifies a loopback-only listener.
7. The project MCP configuration uses the direct SSE endpoint. The Meta proxy
   remains an optional documented alternative.
8. CI proves shared packaging with a repository-owned no-op layer and never
   downloads or publishes Meta's binary.

## Privacy, security, and evidence requirements

- Package the Operator layer, server, manifest, and capture tool only into apps
  01–04. Never start an Operator session for apps 05–10.
- Keep any saved synthetic capture under ignored `build/operator/`; never add
  it to performance evidence.
- Fail when the device listener is wildcard or non-loopback.
- Never treat an Operator build as performance evidence.
- Never infer a Milestone 9 pass from Operator availability or screenshots.
- Keep the Meta bundle and its layer manifest out of git.

## Bundle inventory gate

The package is login-gated and was not available during host implementation.
Complete every pending field before staging or deploying Meta's layer.

| Field | Required result | Current result |
|---|---|---|
| Download source | Meta Developer Center standalone package | `[ ]` login required |
| Archive version | 205.1 | `[ ]` |
| Archive SHA-256 | 64 lowercase hex characters | `[ ]` |
| Android library | one AArch64 `android/**/libXrApiLayer_METAX_operator*.so` | `[ ]` |
| Layer manifest | one `android/**/XrApiLayer_METAX_operator*.json` | `[ ]` |
| Layer name | `XR_APILAYER_METAX_operator` | `[ ]` |
| `library_path` | basename exactly matching the shipped `.so` | `[ ]` |
| `api_version` | recorded from the manifest | `[ ]` |
| License | reviewed; local gitignored staging allowed | `[ ]` |
| Redistribution | CI/artifact publication prohibited unless license explicitly allows it | `[ ]` |

If the archive lacks the Android arm64 layer, the manifest and library do not
match, or the license forbids local staging, stop the workstream.

## Out of scope

- Meta XR Simulator, Unity, Unreal, or a production Operator dependency.
- Fixing the Milestone 9 source-switching defect.
- Changing any Milestone 9 verdict.
- Fixing app 10's pre-existing cleartext-traffic manifest scope.
- Replacing logcat, performance telemetry, fixture review, or headset judgment.
