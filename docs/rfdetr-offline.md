# Offline RF-DETR detection

This pipeline is Milestone 10's host reference oracle. It consumes an approved,
checksummed Quest Camera fixture and owns the pinned numeric contract used to
validate the Android and streaming backends. The deployable result lives in
`apps/10-rfdetr-detection`; this tool is deliberately not the application.

## Pinned contract

The machine-readable source of truth is
`tools/rfdetr_export/model-manifest.json`:

- RF-DETR `1.9.0`, tag commit
  `a700efc750c0f8763e0e0828ba6525e9e7e0b4aa`;
- RF-DETR Nano's official COCO checkpoint, MD5
  `fb6504cce7fbdc783f7a46991f07639f` and SHA-256
  `d8d6b9ee57d4d0ed2b1f305163624712a0532cb7bce0c747317984fc5457440d`;
- exported `rfdetr-nano.onnx` SHA-256
  `1ac37bb3429380c7aee6b2c2778d20026db0ad18579cac119255d31f736dc760`;
- Python `3.11.15`, CPU-only PyTorch `2.10.0`, torchvision `0.25.0`, ONNX `1.20.1`,
  and the complete transitive `uv.lock`;
- static `float32` input `input`, shape `[1,3,384,384]`, RGB NCHW;
- direct square squash resize, half-pixel bilinear coordinates, no padding,
  and `antialias=false`, matching RF-DETR 1.9.0's own `predict()` path;
- `1/255` scaling followed by ImageNet RGB mean and standard deviation;
- `dets` `[1,300,4]` normalized `cxcywh` and `labels` `[1,300,91]`
  logits;
- COCO's sparse category-ID index space. Class ID `90` is `toothbrush`;
  it is not dense class index `79`, and gaps such as ID `12` remain gaps;
- global top-300 query/class pairs after sigmoid, confidence strictly above
  `0.5`, and no NMS;
- ONNX Runtime `1.21.0`, CPU execution provider, one intra-op thread, one
  inter-op thread, sequential execution, and extended graph optimization.

RF-DETR's export guide documents ONNX export, opset 17, custom shapes, and the
generated `rfdetr-nano.onnx` artifact. The preprocessing details above
come from the pinned RF-DETR source, not from a deployment blog or a different
checkpoint.

Sources:

- [RF-DETR export guide](https://rfdetr.roboflow.com/latest/learn/export/)
- [RF-DETR source](https://github.com/roboflow/rf-detr/tree/1.9.0)
- [C++ inference integration reference](https://github.com/olibartfast/rf-detr-cpp-inference)

## Integration decision

The repository ports the small relevant contract rather than vendoring or
submoduling the reference implementation. This keeps the repository on C++17,
removes its C++20 `std::span` dependency, avoids FFmpeg/SDL/OpenCV requirements
for fixture input, and removes build-time downloads. The port also preserves
the official global query/class top-k and sparse COCO IDs rather than applying
a dense-label offset.

ONNX Runtime is not fetched by CMake. The explicit preparation script downloads
the pinned official prebuilt archive into `build/dependencies`, verifies SHA-256
`7485c7e7aac6501b27e353dcbe068e45c61ab51fbaf598d13970dfae669d20bf`,
and prints the root passed to CMake. Model and runtime artifacts remain outside
Git and source directories.

## Fixture and colour gate

Only `QUEST_CAMERA_FIXTURE_V2` is accepted. The app writes a canonical SHA-256
over dimensions, strides, plane lengths, and exact Y/U/V bytes. The host loader
verifies it before allocation-dependent processing, then C++ preprocessing
calls the existing limited-range BT.601 `ConvertYuv420ToRgba` directly.

The official-reference script independently decodes the stored planes and
requires byte-for-byte equality with pixels dumped by the C++ tool. It also
requires an explicit `--approved-fixture` affirmation. This prevents an
arbitrary public image from silently bypassing the Quest colour/stride risk.

No real fixture is currently checked in, so reference generation, correct
known-object label acceptance, C++/PyTorch comparison, annotated real-scene
review, and local timing remain gated on Milestone 9's privacy approval.

## Build and run

Export the model:

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

Build the C++ path:

```bash
onnxruntime_root="$(tools/rfdetr_inference/prepare_onnxruntime.sh)"
cmake -S tools/rfdetr_inference -B build/rfdetr-inference \
  -DBUILD_TESTING=ON \
  -DQUESTLAB_ENABLE_ONNXRUNTIME=ON \
  -DONNXRUNTIME_ROOT="$onnxruntime_root"
cmake --build build/rfdetr-inference --parallel
ctest --test-dir build/rfdetr-inference --output-on-failure
```

Run with an approved fixture using the command in
`tools/rfdetr_inference/README.md`. The result includes JSON detections, a
labeled PPM preview, and preprocessing/inference/postprocessing distributions
with count, mean, p50, p95, p99, and maximum.

## Acceptance tolerances

Expected and C++ detections are greedily matched in descending reference
confidence to the highest-IoU unused detection with the same exact class ID.
Counts must be identical, confidence delta must be no more than `0.0001`, and
box IoU must be at least `0.999`. These limits absorb CPU floating-point
ordering differences between PyTorch and ONNX Runtime. A larger discrepancy is
a contract defect to diagnose, not a reason to widen the limits.

CI runs all weight-free tests: preprocessing reference values, source-space
coordinate mapping, sparse labels, empty output, NaN rejection, manifest
mismatch, corrupt model hash, comparison rules, timing summaries, fixture V2
checksum validation, and rejection of fixture V1. Model/fixture reference and
performance tests are explicitly local-only.
