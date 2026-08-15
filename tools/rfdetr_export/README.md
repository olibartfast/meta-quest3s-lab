# RF-DETR export

This directory pins the RF-DETR Nano export contract used by Milestone 10.
Python dependencies are resolved by `uv.lock`; model weights and ONNX files are
generated under `build/models/rfdetr` and are not committed.

From the repository root:

```bash
UV_PROJECT_ENVIRONMENT="$PWD/build/rfdetr-export-venv" \
  uv sync --project tools/rfdetr_export --python 3.11
UV_PROJECT_ENVIRONMENT="$PWD/build/rfdetr-export-venv" \
  uv run --project tools/rfdetr_export \
  tools/rfdetr_export/export_model.py \
  --checkpoint build/models/rfdetr/rf-detr-nano.pth \
  --download-checkpoint \
  --output-dir build/models/rfdetr \
  --manifest tools/rfdetr_export/model-manifest.json \
  --update-manifest
```

The script verifies the official checkpoint SHA-256, exports a static-batch
ONNX model, runs the ONNX checker, fails if tensor names or shapes differ from
the manifest contract, hashes the model, and writes `export-record.json` beside
the generated artifact.

After a real Quest fixture has passed privacy review, create the reference and
prove the independent host decoder is byte-identical to the shared C++
converter:

```bash
UV_PROJECT_ENVIRONMENT="$PWD/build/rfdetr-export-venv" \
  uv run --project tools/rfdetr_export \
  tools/rfdetr_export/reference_detections.py \
  --checkpoint build/models/rfdetr/rf-detr-nano.pth \
  --model-manifest tools/rfdetr_export/model-manifest.json \
  --fixture path/to/approved/manifest.qcam \
  --cpp-tool build/rfdetr-m10-tests/rfdetr_inference \
  --output-dir build/rfdetr-reference/frame-001 \
  --approved-fixture
```

The approval flag is intentional: arbitrary photos and public stand-ins do not
satisfy Milestone 10's real-camera colour and geometry gate.
