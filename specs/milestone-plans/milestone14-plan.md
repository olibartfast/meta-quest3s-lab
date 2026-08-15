# Milestone 14 — RF-DETR Spatial Overlay

> **Blocked for redesign after Milestone 16:** do not implement the box-only
> Environment Depth fusion below as the production path. If the per-device
> calibration and optical-sync follow-up passes, revise this milestone around
> depth from the exact stereo pair seen by RF-DETR plus object isolation. The
> existing text is retained as a rejected/alternative design record until that
> gate completes. See `docs/stereo-capability.md`.

## Goal

Fuse live, frame-correlated RF-DETR boxes with aligned Quest environment depth
to create metric 3D annotations in OpenXR `LOCAL`.

This is the integration milestone. All individual platform risks must already
have passed in Milestones 9–13.

## Scope

Create:

- `apps/14-cv-spatial-overlay`;
- reusable detection/depth fusion;
- a metric distance readout rendered beside each 3D box;
- `docs/milestone14-validation.md`.

Reuse without redesign:

- Milestone 11 live RF-DETR protocol and client;
- Milestone 13 timestamp correlation and depth reprojection;
- existing Vulkan spatial-object rendering.

Exclude:

- model fine-tuning;
- multi-object tracking;
- motion prediction;
- semantic segmentation;
- on-device inference as a requirement;
- production authentication.

Synthetic detections may exercise failure paths but cannot satisfy acceptance.

## Distance readout

A wireframe box alone shows where the fusion thinks an object is, but not how
far away it believes the object to be. The number is what makes a depth error
visible and arguable during acceptance, so each rendered box carries a metric
distance beside it.

The repository has no text rendering. `DebugLineShape` offers axes, rectangle,
ray, box, and screen rectangle, and the only textured path is a single
`RgbaImageQuad` slot that app 13 already uses for the RGB preview. Adding a
readout is therefore a real component, not a formatting change, and the choice
of mechanism is part of this milestone.

Prefer a segment-drawn numeric glyph over a font atlas. Adding a
`DebugLineShape::Digit` whose vertices are selected procedurally from
`gl_VertexIndex`, in the style of the existing shapes, reuses the line-list
pipeline and push-constant model exactly as they are: no texture, no descriptor
set, no font asset, no second image-quad slot, and one draw call per character.
A four-character readout such as `1.35` costs four draws. The limitation is that
segment glyphs render digits, a decimal point, and a unit suffix, but not class
names; if class names are wanted later, that is the point at which a glyph atlas
becomes justified, and it should be argued on its own rather than smuggled in
here.

Display conventions:

- Compute the displayed distance each frame from the current head pose to the
  stored `LOCAL` box centre, so the number stays correct as the user walks
  around a static object. The box is world-locked, so a distance frozen at
  capture time would drift from what the user sees.
- Keep that per-frame display distance separate from the capture-time depth
  measurement and its age. A stale result must not present a freshly computed
  number as though the measurement were fresh.
- State the units and fix the precision to the depth error measured in
  Milestone 12. Centimetre precision is the likely honest limit; more digits
  claim accuracy the sensor does not have.
- Billboard the readout toward the viewer and scale it with distance so its
  angular size stays legible, anchored to a consistent face of the box.
- Expect overlapping readouts to draw in submission order, because the renderer
  has no depth attachment. Order draws deliberately, nearest last, rather than
  relying on the order the fusion happens to produce.

## Atomic implementation sequence

### Sequence 0 — Integrate validated inputs

1. Create app 14 from app 13.
2. Add the Milestone 11 inference client unchanged.
3. Run RGB, depth, and inference simultaneously.
4. Confirm bounded queues.
5. Confirm all existing diagnostics remain valid.

Gate: all three streams coexist before fusion is added.

### Sequence 1 — Create a complete correlation record

1. Key RGB records by `frameId`.
2. Attach the selected depth snapshot.
3. Attach RGB and depth poses.
4. Attach temporal delta and uncertainty.
5. Attach validated RF-DETR results.
6. Expire incomplete records.
7. Bound the record ring.
8. Reject late responses after record expiry.

Gate: a diagnostic record explains every accepted or rejected inference
response.

### Sequence 2 — Select object depth samples

1. Reproject depth samples into the detection's RGB frame.
2. Select samples inside each 2D box.
3. Exclude a configurable border region.
4. Reject invalid and near-field samples.
5. Build a distance histogram or robust cluster.
6. Select the dominant foreground cluster.
7. Remove outliers.
8. Require minimum sample count and inlier ratio.
9. Compute coverage and spread statistics.

Gate: saved fixtures produce deterministic foreground sample sets.

### Sequence 3 — Construct metric 3D detections

1. Compute a robust metric centre.
2. Compute conservative percentile extents.
3. Transform the centre into `LOCAL`.
4. Define box orientation as an explicit display convention.
5. Compute fusion confidence from coverage, spread, and time delta.
6. Preserve source class, confidence, frame ID, and capture time.
7. Reject insufficient-confidence results.
8. Add host geometry tests.

Gate: measured fixtures produce plausible centres and extents.

### Sequence 4 — Render world-space overlays

1. Render full-size wireframe boxes in `LOCAL`.
2. Colour by fusion confidence.
3. Modulate or indicate detector confidence separately.
4. Fade by capture age.
5. Add the numeric glyph shape described above to `libs/vulkan_renderer`, with
   host tests for glyph selection and for formatting a distance at the fixed
   precision, including the rounding boundary.
6. Render the distance readout beside each box, recomputed each frame from the
   current head pose to the stored `LOCAL` centre.
7. Apply the same visibility rules to the readout as to its box. A hidden,
   expired, or low-confidence result must not leave a number floating in the
   room, which is the failure a separately drawn label invites.
8. Indicate measurement age on the readout distinctly from the box fade, so a
   confidently rendered number cannot misrepresent a stale measurement.
9. Hide expired results.
10. Clear on a valid empty detection frame.
11. Show camera/depth/inference/correlation status.
12. Keep all fusion work, distance formatting, and glyph selection off the
    render loop; the render loop consumes prepared draws only.
13. Rate-limit logs.

Gate: boxes remain world-locked while the user moves around static objects, and
each visible box carries a legible distance that tracks the user's movement and
disappears with its box.

### Sequence 5 — Recorded replay validation

1. Add a synchronized RGB/depth fixture.
2. Add expected RF-DETR results.
3. Replay the complete pipeline without a live server.
4. Verify deterministic correlation.
5. Verify deterministic sample clustering.
6. Verify centre and extent tolerances.
7. Inject stale, missing, malformed, and low-depth cases.
8. Verify none are rendered as valid.

Gate: CI covers the complete geometry path.

### Sequence 6 — Measure real accuracy

1. Define at least two physical object classes.
2. Measure object position and dimensions.
3. Record distance, lighting, and occlusion.
4. Measure RF-DETR 2D IoU.
5. Measure depth error.
6. Measure 3D centre error in centimetres.
7. Measure extent error per axis.
8. Compare the displayed distance against a tape-measured headset-to-object
   distance at several standoffs. The readout is a claim to the user, so it is
   validated as a measurement rather than accepted as a formatting detail.
9. Confirm the displayed precision does not exceed the error measured here and
   in Milestone 12; reduce the precision if it does.
10. Check readout legibility at the nearest and furthest working distance, and
    with two objects close enough that their readouts overlap.
11. Measure static jitter, including numeric flicker in the readout's last
    displayed digit.
12. Measure slow-head-motion jitter.
13. Record false positives and fusion failures.

Gate: validation contains numeric results and representative failures, and the
displayed distance is shown to agree with physical measurement within a stated
error at the precision it displays.

### Sequence 7 — Measure latency and stability

1. Measure camera-frame age.
2. Measure outbound transport.
3. Measure RF-DETR inference.
4. Measure inbound transport.
5. Measure RGB/depth pair delta.
6. Measure fusion time.
7. Measure age at predicted display time.
8. Measure OpenXR frame cadence.
9. Measure queue high-water marks.
10. Measure memory high-water mark.
11. Run for 15 minutes.
12. Repeat after headset reboot.

Gate: the sustainable default submission rate and full latency breakdown are
documented.

### Sequence 8 — Regression acceptance

1. Run all new host tests.
2. Run all existing host tests.
3. Build apps 01–14 that exist.
4. Build legacy passthrough.
5. Test permission denial.
6. Test server absence and reconnect.
7. Test depth unavailable.
8. Run three clean launch/exit cycles.
9. Review every captured artifact for privacy.
10. Complete the validation document.

Gate: every Definition of Done item has objective evidence.

## Definition of Done

- live RF-DETR detections remain keyed to their originating Quest RGB frames;
- those frames are paired with validated environment-depth snapshots;
- robust depth fusion produces metric centres and extents;
- results transform correctly into OpenXR `LOCAL`;
- at least two real object classes receive world-locked 3D overlays;
- every visible box carries a legible metric distance that tracks the user's
  movement, agrees with physical measurement within a stated error, displays no
  more precision than that error supports, and disappears with its box;
- stale, incomplete, malformed, and low-confidence results remain hidden;
- 2D, depth, 3D, jitter, latency, cadence, queue, and memory metrics are
  documented;
- replay tests cover the complete fusion path;
- a 15-minute run and post-reboot run pass;
- synthetic detections alone cannot complete the milestone.
