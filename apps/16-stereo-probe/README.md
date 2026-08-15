# Quest Stereo Capability Probe

This diagnostic app determines whether the two Quest passthrough RGB cameras
can act as a synchronized stereo source. It records Camera2 calibration
metadata and frame timestamps, exposure, and pairing statistics. It does not
store image pixels and it does not render a 3D application.

## Run on Quest

Connect one authorized Quest 3 or Quest 3S. Keep the headset awake and wear it
so Horizon can clear its launch check, then run from the repository root:

```bash
adb devices
./scripts/build_deploy.sh --app 16-stereo-probe
```

The probe finishes automatically after collecting at least 320 frames from
each camera, normally in under ten seconds. Pull and evaluate its report with:

```bash
./scripts/pull_stereo_probe.sh
```

If the optical-centre spacing has been measured physically, include it in the
strict 5 mm baseline cross-check:

```bash
./scripts/pull_stereo_probe.sh --physical-baseline-meters 0.064
```

The evaluator exits non-zero for `FAIL` or an invalid report, so it can be used
as a shell or CI gate. Device diagnostics are available with:

```bash
adb logcat -s StereoProbe:V AndroidRuntime:E CameraService:E '*:S'
```

If Horizon blocks launch because the headset is asleep, the deploy script exits
with a precise recovery command instead of claiming the app launched.

## Output and privacy

The app-private report is
`files/stereo-probe-report.json`; the pull script copies it to
`build/stereo-probe/stereo-probe-report.json`. Acquired YUV images are closed
without reading or writing their planes. No photo, thumbnail, or derived image
is saved.

## Limitations

- A synchronized capture result does not by itself provide rectification or
  metric depth.
- A `SENSOR_INFO_TIMESTAMP_SOURCE` other than `REALTIME` leaves correlation to
  the OpenXR clock unproven even when left/right relative timing is excellent.
- Missing factory distortion coefficients require a separate calibration and
  validation gate before a production stereo-depth backend can be claimed.
