# RF-DETR world detection

This native Quest 3/3S application consumes the Milestone 9 Camera2 source,
runs RF-DETR through a selectable detector backend, correlates every result to
its submitted frame, and renders its bearing as LOCAL-space rays plus an
operator-positioned assumed-range box.

The world-space overlay is the default view. Use the right thumbstick to adjust
the assumed distance, A to reset it to 2 metres, and B only when the flat 2D
diagnostic is needed.

Build, install, provision the default model, and launch:

```bash
./scripts/build_deploy.sh --app 10-rfdetr-detection
```

See `docs/rfdetr-detection.md` for controls, backend selection, service setup,
artifact pins, validation, and known pose/range limitations.
