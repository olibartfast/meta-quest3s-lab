# Validation: Milestone 9 Fixture and App 09 Acceptance

**A bare `[x]` is not a result.** Every check records what was measured, on
which device, on what date:

```
- [x] Pause/resume cycles — 10/10 capture returned, Quest 3, 2026-08-15
- [ ] Peak RSS over 15 min — not run
- [!] Colour on diagnostic quad — R/B swapped, Quest 3S, 2026-08-15 (see note)
```

`[x]` passed · `[ ]` not run · `[!]` failed, with the cause named. A `[!]`
does not block closing this spec; hiding it does.

**Tree under test:** commit `________` (from plan task 3)
**Devices:** Quest 3 `________` · Quest 3S `________` (build/serial)
**Session log:** `________` (path to the retained logcat capture)

---

## Prerequisites

- [ ] Working tree committed; `git status` clean apart from ignored build output
- [ ] App 10 README no longer describes the assumed-range box or thumbstick range control
- [ ] `docs/rfdetr-detection.md` corrected the same way
- [ ] `ROADMAP.md` Milestone 12 and 14 status lines reflect the code on disk
- [ ] `specs/roadmap.md` Phases 5 and 7 reconciled
- [ ] Capture scene prepared and privacy review procedure agreed

## Automated

- [ ] `libs/camera_source` host tests pass — command: ________ , result: ________
- [ ] Fixture-manifest tests pass — ________
- [ ] YUV conversion tests pass — ________
- [ ] App 09 APK builds — `./scripts/build_deploy.sh --app 09-quest-camera --build-only`
- [ ] Source switching needs no code edit — `CreateCameraSource` selects `MetaCamera2` and `Replay` from configuration alone

## On-headset

Run in the order given by `plan.md` Group 5, in one session.

- [ ] Permission denied path — app reports denial, does not crash: ________
- [ ] Permission granted path — capture starts: ________
- [ ] Passthrough source selected without a hard-coded ID — log line: ________
- [ ] Orientation correct on the diagnostic quad: ________
- [ ] Aspect ratio correct: ________
- [ ] Colour correct (no R/B swap, no chroma plane mix-up): ________
- [ ] Pause/resume — ____/10 cycles returned capture
- [ ] 15-minute run completed — actual duration: ________
- [ ] Memory bounded — start ____ MB, mid ____ MB, end ____ MB
- [ ] Overwritten-frame count advances under load — start ____, end ____
- [ ] Clean exit, no leaked OpenXR handles: ________
- [ ] No Camera2, OpenXR, Vulkan, or lifecycle errors in the session log: ________

## Fixture and privacy

- [ ] Exactly one frame armed by Volume Down — captures written: ________
- [ ] Log reported path and byte count only, no pixel contents: ________
- [ ] Capture pulled via `run-as`; only the selected directory left the device
- [ ] Every pixel reviewed for people, screens, documents, addresses, other private content
- [ ] Reviewer: ________ · Date: ________ · Scene: ________
- [ ] Calibration provenance recorded: ________
- [ ] Scene contains at least two physical object classes usable by Milestone 10: ________
- [ ] Approved fixture committed at: ________

## Replay

- [ ] Replay RGBA output byte-for-byte identical to live conversion: ________
- [ ] `ComputeQuestCameraPixelSha256` matches the hash in `manifest.qcam`: ________
- [ ] Replay equality covered by a host test that CI runs: ________

## Definition of Done

Milestone 9 closes when:

- Every check above carries a measured value, a device, and a date.
- App 09 showed a live feed from a real passthrough camera, logged correct
  metadata, survived the lifecycle and long-run checks, and exited cleanly.
- Exactly one approved, privacy-reviewed fixture is committed, and replaying
  it reproduces the live conversion byte-for-byte.
- Switching between Meta and replay sources required only configuration.
- The Milestone 9 status line in `ROADMAP.md` states the real verdict. Any
  `[!]` is carried forward as named debt, in the manner of Milestone 16's
  `PASS_WITH_DEBT` — not quietly dropped.
- Milestone 10's host-oracle agreement check is unblocked, and Phase 1 of
  `specs/roadmap.md` says so.

**Verdict:** ________ **Date:** ________
