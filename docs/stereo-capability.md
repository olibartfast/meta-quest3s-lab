# Quest Stereo Capability Result

## Decision

**Verdict: `PASS_WITH_DEBT`.** Quest 3 exposes the left and right passthrough
RGB cameras as concurrently usable Camera2 devices, and the measured timestamp
and exposure behaviour is strong enough to justify a bounded stereo
calibration milestone. It is not yet sufficient to claim a production
stereo-depth source.

The non-surrogable platform gates passed. The remaining work is calibration
that can be produced per device, plus an optical sanity test needed because the
runtime does not advertise a calibrated logical-camera sync relationship.

## Measured run

The accepted run was captured on 2026-08-01 with:

- device: Oculus Quest 3 (`eureka`), Android API 34;
- Horizon build display: `UP1A.231005.007.A1`;
- Camera2 IDs: general camera `1`, passthrough left `50`, passthrough right `51`;
- topology: cameras `50` and `51` are separate top-level devices in concurrent
  set `[1, 50, 51]`; neither is a logical multi-camera device;
- Meta positions: `50 = 0` (left), `51 = 1` (right);
- sync declaration: `LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE` absent;
- timestamp source: `UNKNOWN` for both cameras;
- shared lens-pose reference: `GYROSCOPE`;
- Camera2-derived optical-centre baseline: `0.0638414 m`;
- common YUV sizes: 11, including the selected `640×480`;
- factory intrinsics: present for both cameras;
- factory distortion arrays: absent for both cameras.

The simultaneous run produced:

| Measurement | Result |
|---|---:|
| Left/right frames | 320 / 320 |
| Matched pairs | 319 |
| Unmatched frames | 1 left / 1 right |
| Matched rate | 49.9978 pairs/s |
| Timestamp skew, median | 0 ns |
| Timestamp skew, p95 | 1,000 ns |
| Timestamp skew, max | 2,000 ns |
| Frame interval, median | 20,001,000 ns on both streams |
| Frame-interval p95 jitter | 1,000 ns on both streams |
| Comparable exposure pairs | 319 |
| Exposure/sensitivity parity | 100% |

No pixel, thumbnail, or derived image was written. The app acquired each YUV
image only to read its timestamp, then closed it.

## Gate result

| Gate | Result | Meaning |
|---|---|---|
| P1A platform topology | Pass | distinct left/right cameras and a common concurrent configuration exist |
| P1B factory calibration | Debt | distortion is not exposed; a complete per-device stereo model is absent |
| P2 simultaneous capture | Pass | more than 300 pairs at nearly 50 pairs/s, unmatched fraction below 1% |
| P3 timestamp skew | Pass | reported timestamp distribution is comfortably inside the numeric limits |
| P3O optical synchronization | Debt | sync type is absent; equal timestamps do not independently prove equal physical exposure time |
| P4 photometric parity | Pass | exposure and sensitivity matched for every comparable pair |

The derived `63.8414 mm` baseline is plausible but has not yet been compared
with a physical optical-centre measurement. More importantly, `UNKNOWN`
timestamp sources do not establish the Camera2-to-OpenXR clock relationship
needed to place stereo geometry in `LOCAL` at capture time.

## Consequence for Milestones 12–14

Do not continue with box-only RF-DETR plus Meta environment-depth sampling as
the primary object-ranging design. The probe found a materially better
candidate: depth computed from the same left/right image pair seen by the
detector. Environment Depth remains useful for occlusion, raycasts, and coarse
scene geometry, but it must not be treated as registered object depth merely
because its samples fall inside a 2D detection box.

Before implementing stereo disparity, complete one bounded follow-up gate:

1. Validate physical exposure synchronization with a shared irregular optical
   stimulus. Compute and retain only per-frame luminance/correlation statistics;
   do not save scene pixels.
2. Calibrate intrinsics, distortion, stereo rotation, and translation for this
   individual headset and exact stream size/ROI using a ChArUco or checkerboard
   target. Persist a versioned, checksummed calibration artifact keyed to the
   device and stream configuration.
3. Verify physical baseline, reprojection error, rectified vertical disparity,
   and metric scale against explicit tolerances before producing depth.
4. Invalidate or revalidate the artifact after firmware, stream crop/ROI,
   distortion-correction, or mechanical geometry changes.
5. Establish the Camera2-to-OpenXR time mapping before transforming a stereo
   result into world space.

This calibration is not a universal Quest constant. It is per-device and valid
only while the rig and image mapping remain stable. Dynamic correction or ROI
changes can make a static calibration drift even when the housing appears
rigid.

## Reproduce

With one authorized, awake, and worn Quest connected:

```bash
./scripts/build_deploy.sh --app 16-stereo-probe
./scripts/pull_stereo_probe.sh
```

Optionally add the measured optical-centre spacing:

```bash
./scripts/pull_stereo_probe.sh --physical-baseline-meters 0.064
```

The raw pulled report is generated under
`build/stereo-probe/stereo-probe-report.json` and is intentionally excluded
from source deliverables.
