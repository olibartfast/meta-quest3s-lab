# Requirements: Milestone 9 Fixture and App 09 Acceptance

## What this is

App 09 captures a Quest passthrough camera and shows it on a Vulkan quad. The
code works. Nobody has verified it on a headset, and **no approved camera
fixture exists** — so Milestone 10 cannot compare its on-device detections
against the host reference. This spec is the verification session that unblocks
that.

One headset session. Nine checks. One recorded camera frame.

## In scope

| # | Check | Passes when |
|---|---|---|
| 1 | Permission denied | App says so in the log and keeps running |
| 2 | Permission granted | Preview appears |
| 3 | Camera selection | Log names a real camera id, not a hard-coded one |
| 4 | Picture correct | Right way up, right shape, right colours |
| 5 | Pause/resume | Preview comes back all 10 times |
| 6 | 15-minute run | Memory flat, frames dropped not queued |
| 7 | Fixture capture | Volume Down saves exactly one frame |
| 8 | Privacy review | A human looked at every pixel and approved it |
| 9 | Replay | Replaying the fixture gives byte-identical RGBA |

These are the checks already listed at `docs/quest-camera.md:114` and in the
Milestone 9 Definition of Done. Nothing new is invented here.

## Out of scope

- **Milestone 10 acceptance** — oracle agreement, thermal runs, backend
  choice. That is the next spec; this one unblocks it.
- Apps 01, 02, 03, 06, 07, 08.
- Anything touching depth or fusion.
- **Fixing bugs.** If a check fails, write down what happened and stop. The fix
  is a separate change with its own review.

## Decisions

**Write the number, not a tick.** Every check in `validation.md` records what
was measured, which headset, and the date. `specs/mission.md` says a claim must
be measured; a bare `[x]` is not a measurement.

**Failing is allowed. Hiding is not.** A failed check is marked `[!]` with the
cause and carried as named debt — the way Milestone 16 closed as
`PASS_WITH_DEBT` instead of being argued into a pass.

**One frame.** Volume Down arms exactly one capture. There is no bulk record
mode and this spec does not add one.

**The privacy review is a human decision.** An unreviewed capture never leaves
app-private storage, and the approval — who, when, what is in the shot — is
committed next to the fixture.

**Replay equality is byte-for-byte**, compared against the live conversion. Not
"looks the same".

## Four things must be true first

These are tasks in `plan.md` groups 1–4, not assumptions:

1. **The working tree is committed.** 41 files are uncommitted right now. A
   measurement that cannot be tied to a commit is not evidence.
2. **The stale docs are fixed.** `apps/10-rfdetr-detection/README.md` and
   `docs/rfdetr-detection.md` still describe an operator-positioned
   assumed-range box that `main.cpp:985` deleted. Checking behaviour against a
   wrong document produces wrong failures.
3. **The scene is prepared and the review agreed** before the headset goes on.
4. **The session runs start to finish in one pass.** Step 1 only works on a
   fresh install, so a restart means starting over.

## Facts you need

**Package** `com.olibartfast.questlab.questcamera` · **log tag** `QuestCamera`

**Arming a capture:** Volume Down
(`QuestCameraActivity.java:151`) writes to
`getFilesDir()/captures/frame-<sensorTimestamp>/` as `y.bin`, `u.bin`,
`v.bin`, `manifest.qcam`. The manifest is written `.tmp` then renamed, so a
half-written capture never looks valid.

**Telemetry:** once per second the app logs a line starting `PERF {` — JSON
with `counters.received`, `consumed`, `source_overwrite`, `queue_current`,
`queue_high_water`. Checks 5 and 6 read these instead of relying on your
judgement.

**Fixture format:** `QUEST_CAMERA_FIXTURE_V2`.
`ComputeQuestCameraPixelSha256` hashes plane geometry plus the exact Y/U/V
bytes *including padding*, so an approved fixture is frozen byte-for-byte.

**Source switching:** app 09 and app 10 both build through
`CreateCameraSource`; `CameraSourceKind` is `MetaCamera2`, `Replay`, or
`ExternalRgbd`. Switching must need no code edit — verified by reading, not by
running.

**Privacy review** follows the seven steps at `docs/quest-camera.md:91`. A
fixture approved here must survive Milestone 10's second gate — an independent
Python decode compared byte-for-byte with the C++ output — unchanged.

## Open question

The 15-minute run overlaps Milestone 10's thermal evidence. Here it measures
capture-side memory and frame drops only. Per-backend thermal behaviour stays
with Milestone 10.
