# Plan: Milestone 9 Fixture and App 09 Acceptance

Groups 1–4 are desk work, no headset. Group 5 is the headset session. Groups
6–8 are desk work again.

Run everything from the repository root.

---

## Group 1 — Commit the working tree

41 files are uncommitted. Land them so results can name a commit.

```bash
git status --short
git status --untracked-files=all | grep -c "build/\|\.cxx/"   # expect 0
```

Suggested split, one commit each:

| Commit | Contents |
|---|---|
| RF-DETR detection app | `apps/10-rfdetr-detection`, `libs/object_detector`, `libs/detection_projection` |
| Depth and fusion groundwork | `libs/depth_source`, `libs/detection_fusion` |
| Telemetry and integrity | `libs/perf_telemetry`, `libs/artifact_integrity` |
| Host tooling | `tools/rfdetr_export`, `tools/rfdetr_inference` |
| Camera and core updates | `libs/camera_source`, `libs/xr_core`, `apps/06`, `07`, `08`, `09` edits |
| Docs and roadmap | `docs/`, `ROADMAP.md`, `Readme.md`, CI, build files |

```bash
git rev-parse --short HEAD    # write this into validation.md
```

---

## Group 2 — Fix the stale docs

The assumed-range box was deleted from the code but still appears in the docs.

```bash
grep -rn "assumed\|thumbstick" apps/10-rfdetr-detection/README.md docs/rfdetr-detection.md
```

1. `apps/10-rfdetr-detection/README.md` — remove the assumed-range box, the
   right-thumbstick distance control, and "A to reset to 2 metres". What ships
   is bearing rays plus a 2D diagnostic preview.
2. `docs/rfdetr-detection.md` lines 39–44 — same correction. Re-read line 135's
   limitations paragraph against what the code renders.
3. `ROADMAP.md` — Milestone 12 and 14 say "Planned" while
   `libs/depth_source/src/meta_environment_depth_adapter.cpp` and
   `libs/detection_fusion/src/detection_fusion.cpp` exist with tests. State
   what is really on disk.
4. `specs/roadmap.md` Phases 5 and 7 inherited the same stale framing.

---

## Group 3 — Host checks

```bash
cmake -S libs/camera_source -B build/camera-source-tests -DBUILD_TESTING=ON
cmake --build build/camera-source-tests --parallel
ctest --test-dir build/camera-source-tests --output-on-failure
```

```bash
./scripts/build_deploy.sh --app 09-quest-camera --build-only
```

Confirm source switching needs no code edit — read, do not run:

```bash
grep -n "CreateCameraSource" -A4 apps/09-quest-camera/src/main/cpp/main.cpp
grep -n "CreateCameraSource" -A4 apps/10-rfdetr-detection/src/main/cpp/main.cpp
```

Both must pass `CameraSourceKind` from configuration, not hard-code an adapter.

---

## Group 4 — Prepare the scene and the review

**The scene.** A desk or table with 2–3 ordinary objects — a bottle, a cup, a
chair in frame. Milestone 10 later needs at least two real COCO classes out of
this same fixture, so choose objects worth detecting.

**Not in shot:** people, screens, monitors, phones, documents, whiteboards,
post-its, addresses, name badges, windows with people beyond them.

Good even lighting. No backlight.

**The review.** Agree now who signs off and paste this template into the
fixture directory so it gets filled in rather than composed later:

```
Reviewer:
Date:
Scene contents:
Checked for: people, screens, documents, addresses, other private content
Calibration provenance:
Approved: yes / no
```

Decide the fixture path now, e.g.
`libs/camera_source/fixtures/2026-08-15-desk-objects/`.

---

## Group 5 — The headset session

One pass, in this order. Step 1 needs a fresh install, so a restart means
starting over.

**Before putting the headset on:**

```bash
adb devices                                              # must list the Quest
adb uninstall com.olibartfast.questlab.questcamera       # clean permission state
adb logcat -c
adb logcat -s QuestCamera > /tmp/m9-session.log &        # leave running
./scripts/build_deploy.sh --app 09-quest-camera
```

**Headset on:**

| Step | Do | Watch for |
|---|---|---|
| 1 | **Deny** the camera permission | `Camera permission: denied`, app still alive |
| 2 | Relaunch, **grant** it | `Camera permission: granted`, preview appears |
| 3 | Read the selection log | `Selected passthrough camera id=… position=… stream=…` |
| 4 | Look at the quad | Upright, correct aspect, correct colour |
| 5 | Pause/resume **10 times** | `Android lifecycle: PAUSE` / `RESUME`, preview returns each time |
| 6 | Leave running **15 minutes** | Nothing — this is the long wait |
| 7 | Frame the scene, press **Volume Down once** | `Private camera capture saved: path=… bytes=…` |
| 8 | Exit | `Quest Camera Capture stopped cleanly` |

**After:**

```bash
kill %1                     # stop logcat
grep -c "PERF {" /tmp/m9-session.log      # ~900+ lines after 15 min
```

---

## Group 6 — Pull, review, approve

```bash
adb shell run-as com.olibartfast.questlab.questcamera ls files/captures
adb exec-out run-as com.olibartfast.questlab.questcamera \
  tar c files/captures/frame-<TIMESTAMP> > /tmp/m9-capture.tar
tar xf /tmp/m9-capture.tar -C /tmp/
```

Decode the YUV planes to a viewable image and **look at every pixel**. If the
review fails: delete the capture, note why, recapture in a later session. Do
not crop and keep it.

On approval, fill in the template from Group 4 and commit the fixture
directory — `y.bin`, `u.bin`, `v.bin`, `manifest.qcam`, and the approval file.

---

## Group 7 — Replay

```bash
grep pixel_sha256 /tmp/files/captures/frame-*/manifest.qcam
```

1. Run the replay adapter against the committed fixture and compare its RGBA
   output byte-for-byte with the live conversion.
2. Confirm `ComputeQuestCameraPixelSha256` over the committed fixture equals
   the `pixel_sha256` in `manifest.qcam`.
3. Add a host test so CI checks replay equality from now on, not just you once.

---

## Group 8 — Record the result

1. Fill in every value in `validation.md`.
2. Update the Milestone 9 status line in `ROADMAP.md` with the real verdict —
   complete, or complete-with-debt naming each `[!]`.
3. Tick the app 09 items in `specs/roadmap.md` Phase 1 and note that Milestone
   10's oracle check is unblocked.
4. Correct `docs/quest-camera.md` if the session showed behaviour it does not
   describe.
