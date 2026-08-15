# Validation: Milestone 9 Fixture and App 09 Acceptance

Fill in the **Result** column as you go. `[x]` passed · `[ ]` not run ·
`[!]` failed — write the cause, do not delete the row.

A tick with no number is not a result.

**Commit under test:** `________` · **Headset:** Quest 3 / 3S `________`
**Date:** `________` · **Session log:** `/tmp/m9-session.log`

---

## Before the headset

| # | Check | Command | Result |
|---|---|---|---|
| 1 | Tree committed | `git status --short` → empty | |
| 2 | No build output committed | `git status -uall \| grep -c "build/\|\.cxx/"` → `0` | |
| 3 | App 10 docs fixed | `grep -rn "assumed\|thumbstick" apps/10-rfdetr-detection/README.md docs/rfdetr-detection.md` → no range-box hits | |
| 4 | M12/M14 status honest | `ROADMAP.md` names the code on disk | |
| 5 | camera_source tests | `ctest --test-dir build/camera-source-tests` → all pass | |
| 6 | APK builds | `./scripts/build_deploy.sh --app 09-quest-camera --build-only` → exit 0 | |
| 7 | Source switching needs no code edit | Both apps pass `CameraSourceKind` through `CreateCameraSource` | |
| 8 | Scene prepared, reviewer agreed | | |

## In the headset

Run in order. Step 1 needs a fresh install.

| # | Check | Expected in log / view | Result |
|---|---|---|---|
| 9 | Permission denied | `Camera permission: denied`, no crash | |
| 10 | Permission granted | `Camera permission: granted`, preview appears | |
| 11 | Real camera selected | `Selected passthrough camera id=… position=… stream=WxH` — id from enumeration, not hard-coded | |
| 12 | Orientation | Scene upright, not mirrored | |
| 13 | Aspect ratio | Matches the `stream=WxH` in step 11, no stretch | |
| 14 | Colour | Neutral objects look neutral; no red/blue swap, no green cast | |
| 15 | Pause/resume ×10 | 10 × `PAUSE`+`RESUME`, preview back every time — count: ___/10 | |
| 16 | Clean exit | `Quest Camera Capture stopped cleanly` | |
| 17 | No errors all session | see command below → `0` | |

```bash
grep -ciE "error|failed|leak|validation layer" /tmp/m9-session.log
```

## 15-minute run — from the PERF counters

The app logs one `PERF {…}` JSON line per second. Read the counters:

```bash
grep -o 'PERF {.*' /tmp/m9-session.log | sed 's/^PERF //' \
  | jq -r '.counters | [.received,.consumed,.source_overwrite,.queue_current,.queue_high_water] | @tsv' \
  | awk 'NR==1 || NR%60==0'
```

| # | Check | Expected | Result |
|---|---|---|---|
| 18 | Ran the full 15 min | ~900 `PERF` lines | |
| 19 | Frames kept arriving | `received` rises to the end | first ___ → last ___ |
| 20 | Frames dropped, not queued | `source_overwrite` rises | first ___ → last ___ |
| 21 | Queue stays capacity-one | `queue_current` ≤ 1 | max ___ |
| 22 | Queue never backed up | `queue_high_water` ≤ 1 | ___ |
| 23 | No conversion failures | `conversion_failure_window` stays `0` | ___ |
| 24 | Memory flat | `adb shell dumpsys meminfo com.olibartfast.questlab.questcamera` at start / mid / end | ___ / ___ / ___ MB |

If `queue_high_water` exceeds 1, or `received` climbs while `consumed` and
`source_overwrite` both stall, the frame loop is being blocked — that is a
`[!]`, not a rounding detail.

## Fixture

| # | Check | Expected | Result |
|---|---|---|---|
| 25 | Exactly one capture armed | one dir under `files/captures/` | count ___ |
| 26 | Log shows path and size only, no pixels | `Private camera capture saved: path=… bytes=…` | |
| 27 | Four files present | `y.bin`, `u.bin`, `v.bin`, `manifest.qcam` | |
| 28 | Only that directory left the device | | |
| 29 | Every pixel reviewed | no people, screens, documents, addresses | |
| 30 | Reviewer / date / scene recorded | | ___ / ___ / ___ |
| 31 | ≥2 COCO-detectable objects for M10 | which: ___ | |
| 32 | Fixture committed | path: ___ | |

## Replay

| # | Check | Expected | Result |
|---|---|---|---|
| 33 | Replay RGBA == live RGBA | byte-for-byte identical | |
| 34 | Hash matches manifest | `ComputeQuestCameraPixelSha256` == `pixel_sha256` in `manifest.qcam` | |
| 35 | CI covers replay equality | host test added | |

---

## Done when

- Every row above has a number, a headset, and a date.
- App 09 showed a live feed from a real passthrough camera, survived 10
  pause/resume cycles and 15 minutes of capture, and exited cleanly.
- One reviewed, approved fixture is committed, and replaying it reproduces the
  live conversion byte-for-byte.
- Switching Meta ↔ replay needed only configuration.
- `ROADMAP.md`'s Milestone 9 status states the real verdict. Every `[!]` is
  carried forward as named debt, as Milestone 16 did with `PASS_WITH_DEBT`.
- Milestone 10's oracle check is unblocked and `specs/roadmap.md` Phase 1 says so.

**Verdict:** `________` **Date:** `________`
