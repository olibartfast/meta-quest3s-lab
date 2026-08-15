# Plan: Milestone 9 Fixture and App 09 Acceptance

Groups 1–4 are host work and must complete before the headset goes on.
Group 5 is the single in-headset session. Groups 6–8 are host work again.

---

## Group 1: Land the working tree

1. Review the uncommitted work and split it into coherent commits:
   - `apps/10-rfdetr-detection` with `libs/object_detector`,
     `libs/detection_projection`, and the ONNX Runtime packaging
   - `libs/depth_source` and `libs/detection_fusion`
   - `libs/perf_telemetry` and `libs/artifact_integrity`
   - `tools/rfdetr_export` and `tools/rfdetr_inference`
   - `docs/` additions and the `ROADMAP.md` restructuring
   - the remaining `libs/camera_source`, `libs/xr_core`, and app edits
2. Confirm build output stays out of the commits — `.gitignore` already covers
   `apps/*/build/` and `.cxx/`; verify with `git status --untracked-files=all`.
3. Record the resulting commit SHA. Every measurement in `validation.md`
   refers to it.

---

## Group 2: Fix documentation drift

4. Rewrite `apps/10-rfdetr-detection/README.md` to match the code: bearing
   rays plus a 2D diagnostic preview, no assumed-range box, no thumbstick
   range control, no A-to-reset-distance.
5. Correct `docs/rfdetr-detection.md:39-44` the same way, and re-check line
   135's limitations paragraph against what is actually rendered.
6. Update the Milestone 12 and 14 status lines in `ROADMAP.md`: both are
   described as planned while `libs/depth_source` (including
   `meta_environment_depth_adapter.cpp`) and `libs/detection_fusion` have
   implementations and tests on disk. State what exists.
7. Reconcile `specs/roadmap.md` Phases 5 and 7, which inherited the stale
   framing.

---

## Group 3: Host verification

8. Build and run the host tests for `libs/camera_source`, including the
   fixture-manifest and YUV conversion tests. Record pass/fail and the
   command used.
9. Build the app 09 APK: `./scripts/build_deploy.sh --app 09-quest-camera --build-only`.
10. Confirm `CreateCameraSource` selects `MetaCamera2` and `Replay` from
    configuration alone, with no code edit — verify by reading the call sites
    in `apps/09-quest-camera/src/main/cpp/main.cpp` and
    `apps/10-rfdetr-detection/src/main/cpp/main.cpp`.

---

## Group 4: Privacy preparation — before the headset

11. Prepare the capture scene: no people, no screens, no documents, no
    addresses, no whiteboards, no personal objects. Prefer a plain surface
    with two or three neutral objects that later give RF-DETR something real
    to detect (Milestone 10 needs at least two physical classes).
12. Agree the review procedure and who signs off. Write the approval template
    now, so the reviewer fills it in rather than composing it under pressure.
13. Decide the fixture's repository location and name, e.g.
    `libs/camera_source/fixtures/2026-08-15-<scene>/`.

---

## Group 5: The headset session — one pass, in this order

Run `./scripts/build_deploy.sh --app 09-quest-camera` and keep `adb logcat`
capturing to a file for the whole session.

14. **Permission denial first.** Launch on a clean install, deny the camera
    permission, confirm the app reports the denial and does not crash.
15. **Permission grant.** Relaunch, grant, confirm capture starts.
16. **Source selection.** Read the log line naming the selected passthrough
    camera; confirm it is a real device chosen without a hard-coded ID.
17. **Visual check.** Confirm orientation, aspect ratio, and colour on the
    diagnostic quad. Note anything mirrored, rotated, or colour-swapped.
18. **Lifecycle.** Ten pause/resume cycles. Confirm capture returns each time
    and no handles leak in the log.
19. **Long run.** 15 minutes of continuous capture. Record memory at start,
    midpoint, and end, and confirm the overwritten-frame count advances —
    frames must be dropped, not queued.
20. **Fixture capture.** Frame the prepared scene, press Volume Down once,
    confirm the log reports the path and byte count.
21. **Close out.** Exit cleanly. Stop the logcat capture.

---

## Group 6: Pull, review, approve

22. List app-private captures with `run-as` and pull only the selected
    directory (`y.bin`, `u.bin`, `v.bin`, `manifest.qcam`).
23. Decode and review every pixel against the checklist in
    `docs/quest-camera.md:91`.
24. If the review fails, discard the capture, note why, and return to task 20
    in a follow-up session. Do not "crop and keep."
25. On approval, record reviewer, date, scene description, and calibration
    provenance in the fixture directory.
26. Commit the approved manifest and planes under the agreed fixture path.

---

## Group 7: Replay verification

27. Run the replay adapter against the committed fixture and compare its RGBA
    output byte-for-byte with the live conversion output for the same frame.
28. Confirm `ComputeQuestCameraPixelSha256` over the committed fixture matches
    the hash recorded in `manifest.qcam`.
29. Add or extend a host test so replay equality is checked by CI from now on,
    not only by hand.

---

## Group 8: Record the outcome

30. Fill in every measured value, device, and date in `validation.md`.
31. Update the Milestone 9 status line in `ROADMAP.md` with the real verdict —
    complete, or complete-with-debt naming the failing checks.
32. Tick the App 09 items in Phase 1 of `specs/roadmap.md`, and note in
    Phase 1 that Milestone 10's oracle check is now unblocked.
33. Update `docs/quest-camera.md` if the session revealed behaviour the
    document does not describe.
