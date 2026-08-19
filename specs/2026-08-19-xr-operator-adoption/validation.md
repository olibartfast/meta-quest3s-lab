# Validation: Meta XR Operator Adoption

Fill in the **Result** column as evidence becomes available. `[x]` passed ·
`[ ]` not run · `[!]` failed. A tick with no command output or measured value is
not a result.

**Revision under test:** this change set, based on `e8801ae`
**Host date:** `2026-08-19`–`2026-08-20`
**Headset:** not connected

## Bundle gate

| # | Check | Expected | Result |
|---|---|---|---|
| 1 | Standalone archive inventoried | version, tree, SHA-256, manifest fields | `[ ]` Meta login required |
| 2 | License reviewed | local gitignored staging permitted | `[ ]` human review required |
| 3 | Checksum pinned | preparation script accepts only reviewed archive | `[ ]` blocked by checks 1–2 |

## Host build and isolation

| # | Check | Expected | Result |
|---|---|---|---|
| 4 | Pinned toolchain | strict check exits 0 | `[x]` all repository pins matched, 2026-08-19 |
| 5 | Probe layer builds | arm64 `.so` with negotiation export | `[x]` `libopenxr_probe_layer.so` produced |
| 6 | Operator APK builds | app 01 `assembleOperator` exits 0 | `[x]` app 01 in 26 s |
| 7 | Layer library packaged | one arm64 probe `.so` | `[x]` `lib/arm64-v8a/libopenxr_probe_layer.so` |
| 8 | Layer manifest packaged | exact implicit-layer asset path | `[x]` `assets/openxr/1/api_layers/implicit.d/questlab_probe_layer.json` |
| 9 | Operator manifest isolated | extracted native libs + INTERNET | `[x]` `extractNativeLibs="true"`, INTERNET and experimental feature present |
| 10 | Debug/benchmark isolated | no INTERNET except app 10 | `[x]` debug apps 01–09 clean; app 01 benchmark clean; app 10 debug/benchmark retained its existing permission |
| 11 | Excluded apps rejected | apps 05, 09, 10, 16, and legacy exit 2 | `[x]` build and session allow-lists reject before build or device access |
| 12 | Shell scripts lint | `shellcheck -x` exits 0 | `[x]` 10 scripts, exit 0 |
| 13 | Repository debug build | root `assembleDebug` exits 0 | `[x]` 375 tasks, BUILD SUCCESSFUL in 3 s |
| 14 | CI workflow | all checks green | `[ ]` not run remotely |

## Device and MCP

| # | Check | Expected | Result |
|---|---|---|---|
| 15 | Probe negotiation | `QuestLabProbeLayer` log entry | `[ ]` no headset connected |
| 16 | Meta layer negotiation | loader names `XR_APILAYER_METAX_operator` | `[ ]` blocked by bundle gate |
| 17 | Listener address | raw port 8720 line is loopback only | `[ ]` |
| 18 | MCP connection | tool list available through project endpoint | `[ ]` project SSE config parsed; server pending approval and live app |
| 19 | Synthetic capture | app 01 capture succeeds after human consent | `[ ]` |
| 20 | Controller injection | visible input action lands | `[ ]` |
| 21 | Sensitive-app boundary | app 05 cannot start Operator | `[x]` enforced by build and session allow-lists; no device run needed |
| 22 | Forward cleanup | host mapping absent after `--stop` | `[ ]` |
| 23 | Timing delta | debug and Operator frame-time difference recorded | `[ ]` |

## Documentation and acceptance boundaries

| # | Check | Expected | Result |
|---|---|---|---|
| 24 | Workflow documented | build, session, security, cleanup, limits | `[x]` `docs/xr-operator.md` |
| 25 | Performance guard documented | Operator excluded from evidence | `[x]` performance docs updated |
| 26 | M9 checks classified | checks 9–35 excluded without verdict changes | `[x]` Operator unavailable for app 09; no M9 result changed |

**Verdict:** `HOST_PACKAGING_AND_PRIVACY_BOUNDARY_PASS_DEVICE_AND_BUNDLE_PENDING`
**Date:** `2026-08-20`
