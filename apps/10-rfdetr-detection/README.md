# RF-DETR world detection

This native Quest 3/3S application consumes the Milestone 9 Camera2 source,
runs RF-DETR through a selectable detector backend, correlates every result to
its submitted frame, and fuses it with Meta environment depth into a metric 3D
box in `LOCAL` space.

What is drawn is what is measured. When depth supplies no metric fit, nothing
is drawn — a depth failure stays visible instead of being disguised. A box
whose far face came from a prior rather than from depth is tinted so the
assumption stays legible; the near face is measured either way. Orientation
comes from a plane that survived RANSAC, and otherwise falls back to the
viewing bearing as a display convention, not a measurement.

Bearing rays were tried and rejected: they emanate from the viewer's own eye
position, so they read as a starburst rather than as a located object.

The world-space overlay is the default view. Press B to toggle the head-locked
2D diagnostic.

Build, install, provision the default model, and launch:

```bash
./scripts/build_deploy.sh --app 10-rfdetr-detection
```

See `docs/rfdetr-detection.md` for controls, backend selection, service setup,
artifact pins, validation, and known pose/range limitations.
