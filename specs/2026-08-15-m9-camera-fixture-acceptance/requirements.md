# Requirements: Milestone 9 Fixture and App 09 Acceptance

Phase 1 of `specs/roadmap.md` covers device acceptance for seven applications.
This spec covers only the first item, because everything else in the phase
queues behind it: **app 09 has no approved RGB fixture**, and without one,
Milestone 10's host-oracle agreement check cannot run at all.

## Scope

### In scope

| Area | What is required |
|---|---|
| Permission paths | First-run grant and denial both behave correctly |
| Source selection | Logs show a real passthrough camera chosen without a hard-coded ID |
| Visual correctness | Orientation, aspect ratio, and colour correct on the Vulkan quad |
| Lifecycle | Ten pause/resume cycles, capture returns every time |
| Long run | 15 minutes of capture with bounded memory and advancing overwritten-frame counts |
| Fixture capture | One opt-in frame armed with Volume Down, pulled, privacy-reviewed, approved |
| Replay | Replay adapter output byte-for-byte identical to the live conversion |
| Source switching | Meta and replay selectable through `CreateCameraSource` configuration alone |
| Log hygiene | No Camera2, OpenXR, Vulkan, or lifecycle errors in logcat |

That list is the checklist already written at `docs/quest-camera.md:114`, plus
the Milestone 9 Definition of Done in `ROADMAP.md`. This spec does not invent
new criteria; it makes the existing ones executable and recorded.

### Prerequisites inside this spec

Four conditions must hold before any measurement is trusted. All four were
called out as constraints, and all four are tasks in `plan.md`, not
assumptions:

1. **The working tree must land first.** App 10, six libraries, `tools/`, and
   the ROADMAP restructuring are still uncommitted. An acceptance result is
   worthless if it cannot be traced to a known commit.
2. **Doc/code drift must be fixed first.** `apps/10-rfdetr-detection/README.md`
   and `docs/rfdetr-detection.md` still describe the operator-positioned
   assumed-range box that `main.cpp:985` removed. Verifying behaviour against
   stale documentation produces false failures.
3. **The privacy protocol gates the capture**, not the other way round. The
   scene is prepared and the review procedure is agreed before the headset
   goes on.
4. **Headset time is scarce.** All on-device work is one ordered session, run
   start to finish without removing the headset.

### Out of scope

- Milestone 10 acceptance — fixture agreement with the host oracle, the
  15-minute thermal runs per backend, and backend default selection. That is
  the next spec, and this one unblocks it.
- Acceptance for apps 01, 02, 03, 06, 07, and 08.
- Any Milestone 12/13/14 work, including the `libs/depth_source` and
  `libs/detection_fusion` code that already exists on disk.
- Any change to capture, conversion, or replay behaviour. If a check fails,
  the failure is recorded and the fix is a separate change.

## Decisions

- **Evidence is the measured value, not a tick.** Every check in
  `validation.md` carries its measured result, the device, and the date
  inline. A bare `[x]` is not an accepted result. This follows the rule in
  `specs/mission.md`: a claim must be measured.
- **Failures are recorded, not fixed in place.** A failed check keeps its
  measured value and a one-line cause. This spec closes with an honest state,
  the way Milestone 16 closed as `PASS_WITH_DEBT` rather than being talked
  into a pass.
- **The fixture is one frame.** Volume Down arms exactly one, written
  atomically to app-private storage. Nothing bulk-records.
- **Approval is recorded in the repository**, next to the fixture, naming the
  reviewer, the date, and what the scene contains. An unreviewed capture never
  leaves app-private storage.
- **Replay equality is byte-for-byte on RGBA output**, compared against the
  live conversion — not a visual comparison, not a tolerance.
- **No behaviour changes.** Only documentation, the fixture, and its manifest
  are added.

## Context

### Repository rules that shape this spec

`specs/mission.md` — a rendered claim must be a measured claim. `ROADMAP.md`
requires three clean lifecycle cycles and explicit error handling before any
milestone is marked complete, and forbids marking a milestone done on host
evidence alone.

### How the pieces actually work

- **Arming a capture**: `KEYCODE_VOLUME_DOWN`, handled at
  `apps/09-quest-camera/.../QuestCameraActivity.java:151`.
- **Where it lands**: `getFilesDir()/captures/frame-<sensorTimestamp>/`,
  containing `y.bin`, `u.bin`, `v.bin`, and `manifest.qcam`. The manifest is
  written to `manifest.qcam.tmp` and renamed, so a partial capture is never
  visible as a valid one.
- **Fixture format**: `QUEST_CAMERA_FIXTURE_V2`
  (`libs/camera_source/include/camera_source/fixture_manifest.h`).
  `ComputeQuestCameraPixelSha256` hashes plane geometry and the exact owned Y,
  U, and V payloads including padding, so an approved fixture is immutable
  byte-for-byte. Colour conversion is defined only by `ConvertYuv420ToRgba`.
- **Source switching**: both app 09 and app 10 construct through
  `CreateCameraSource`; `CameraSourceKind` is `MetaCamera2`, `Replay`, or
  `ExternalRgbd`. Switching must need no code edit.
- **Logs reveal path and byte count, never pixel contents** — keep it that way.

### Privacy review

The seven-step workflow at `docs/quest-camera.md:91` is authoritative. The
review looks for people, screens, documents, addresses, and any other private
content. Milestone 10 later performs an independent Python decode and compares
byte-for-byte with the C++ output before pixels reach RF-DETR, so a fixture
approved here must survive that second gate unchanged.

### Build and deploy

Per `specs/tech-stack.md`: `./scripts/build_deploy.sh --app 09-quest-camera`,
with `--build-only`, `--variant benchmark`, and `--perfetto-tracing`
available. Toolchain versions come from `scripts/toolchain_config.sh`.

### Open question

The 15-minute long-run check overlaps Milestone 10's thermal evidence. This
spec measures capture-side bounded memory and overwritten-frame counts only;
per-backend thermal behaviour stays with Milestone 10.
